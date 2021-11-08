#include "mesytec-mcpd.h"
#include <ios>
#include <stdexcept>
#include <spdlog/spdlog.h>

#ifndef __WIN32
#include <netinet/ip.h> // sockaddr_in
#else
#include <winsock2.h>
#endif

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>
#include <system_error>

#ifndef __WIN32
    #include <netdb.h>
    #include <sys/stat.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <unistd.h>

    #ifdef __linux__
        #include <sys/prctl.h>
        #include <linux/netlink.h>
        #include <linux/rtnetlink.h>
        #include <linux/inet_diag.h>
        #include <linux/sock_diag.h>
    #endif

    #include <arpa/inet.h>
#else // __WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <stdio.h>
    #include <fcntl.h>
    #include <mmsystem.h>
#endif

#include <iostream>

// FIXME: the timeout case does currently not work in receive_one_packet. the
// code reaches recv() and blocks


namespace
{
using namespace mesytec::mcpd;

static const unsigned DefaultWriteTimeout_ms = 500;
static const unsigned DefaultReadTimeout_ms  = 500;


// Does IPv4 host lookup for a UDP socket. On success the resulting struct
// sockaddr_in is copied to dest.
std::error_code lookup(const std::string &host, u16 port, sockaddr_in &dest)
{
    using namespace mesytec::mcpd;

    if (host.empty())
    {
        // FIXME return MVLCErrorCode::EmptyHostname;
        throw std::runtime_error("empty hostname");
    }

    dest = {};
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *result = nullptr, *rp = nullptr;

    int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(),
                         &hints, &result);

    // TODO: check getaddrinfo specific error codes. make and use getaddrinfo error category
    if (rc != 0)
    {
        //qDebug("%s: HostLookupError, host=%s, error=%s", __PRETTY_FUNCTION__, host.c_str(),
        //       gai_strerror(rc));
        // FIXME return make_error_code(MVLCErrorCode::HostLookupError);
        throw std::runtime_error("host lookup error");
    }

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        if (rp->ai_addrlen == sizeof(dest))
        {
            std::memcpy(&dest, rp->ai_addr, rp->ai_addrlen);
            break;
        }
    }

    freeaddrinfo(result);

    if (!rp)
    {
        //qDebug("%s: HostLookupError, host=%s, no result found", __PRETTY_FUNCTION__, host.c_str());
        // FIXME return make_error_code(MVLCErrorCode::HostLookupError);
        throw std::runtime_error("host lookup error");
    }

    return {};
}

struct timeval ms_to_timeval(unsigned ms)
{
    unsigned seconds = ms / 1000;
    ms -= seconds * 1000;

    struct timeval tv;
    tv.tv_sec  = seconds;
    tv.tv_usec = ms * 1000;

    return tv;
}

#ifndef __WIN32
std::error_code set_socket_timeout(int optname, int sock, unsigned ms)
{
    struct timeval tv = ms_to_timeval(ms);

    int res = setsockopt(sock, SOL_SOCKET, optname, &tv, sizeof(tv));

    if (res != 0)
        return std::error_code(errno, std::system_category());

    return {};
}
#else // WIN32
std::error_code set_socket_timeout(int optname, int sock, unsigned ms)
{
    DWORD optval = ms;
    int res = setsockopt(sock, SOL_SOCKET, optname,
                         reinterpret_cast<const char *>(&optval),
                         sizeof(optval));

    if (res != 0)
        return std::error_code(errno, std::system_category());

    return {};
}
#endif

std::error_code set_socket_write_timeout(int sock, unsigned ms)
{
    return set_socket_timeout(SO_SNDTIMEO, sock, ms);
}

std::error_code set_socket_read_timeout(int sock, unsigned ms)
{
    return set_socket_timeout(SO_RCVTIMEO, sock, ms);
}

#ifndef __WIN32
std::error_code close_socket(int sock)
{
    int res = ::close(sock);
    if (res != 0)
        return std::error_code(errno, std::system_category());
    return {};
}
#else // WIN32
std::error_code close_socket(int sock)
{
    int res = ::closesocket(sock);
    if (res != 0)
        return std::error_code(errno, std::system_category());
    return {};
}
#endif

inline std::string format_ipv4(u32 a)
{
    std::stringstream ss;

    ss << ((a >> 24) & 0xff) << '.'
       << ((a >> 16) & 0xff) << '.'
       << ((a >>  8) & 0xff) << '.'
       << ((a >>  0) & 0xff);

    return ss.str();
}

// Standard MTU is 1500 bytes
// IPv4 header is 20 bytes
// UDP header is 8 bytes
static const size_t MaxOutgoingPayloadSize = 1500 - 20 - 8;

// Note: it is not necessary to split writes into multiple calls to send()
// because outgoing MVLC command buffers have to be smaller than the maximum,
// non-jumbo ethernet MTU.
// The send() call should return EMSGSIZE if the payload is too large to be
// atomically transmitted.
#ifdef __WIN32
inline std::error_code write_to_socket(
    int socket, const u8 *buffer, size_t size, size_t &bytesTransferred)
{
    assert(size <= MaxOutgoingPayloadSize);

    bytesTransferred = 0;

    ssize_t res = ::send(socket, reinterpret_cast<const char *>(buffer), size, 0);

    if (res == SOCKET_ERROR)
    {
        int err = WSAGetLastError();

        if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK)
            return make_error_code(MVLCErrorCode::SocketWriteTimeout);

        // Maybe TODO: use WSAGetLastError here with a WSA specific error
        // category like this: https://gist.github.com/bbolli/710010adb309d5063111889530237d6d
        return make_error_code(MVLCErrorCode::SocketError);
    }

    bytesTransferred = res;
    return {};
}
#else // !__WIN32
inline std::error_code write_to_socket(
    int socket, const u8 *buffer, size_t size, size_t &bytesTransferred)
{
    assert(size <= MaxOutgoingPayloadSize);

    bytesTransferred = 0;

    ssize_t res = ::send(socket, reinterpret_cast<const char *>(buffer), size, 0);

    if (res < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            // FIXME return make_error_code(MVLCErrorCode::SocketWriteTimeout);
            throw std::runtime_error("socket write timeout");
        }

        return std::error_code(errno, std::system_category());
    }

    bytesTransferred = res;
    return {};
}
#endif // !__WIN32

#ifdef __WIN32
static inline std::error_code receive_one_packet(int sockfd, u8 *dest, size_t size,
                                                 u16 &bytesTransferred, int timeout_ms)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);

    struct timeval tv = ms_to_timeval(timeout_ms);

    int sres = ::select(0, &fds, nullptr, nullptr, &tv);

    if (sres == 0)
    {
        // FIXME return make_error_code(MVLCErrorCode::SocketReadTimeout);
        throw std::runtime_error("socket read timeout");
    }

    if (sres == SOCKET_ERROR)
    {
        // FIXME return make_error_code(MVLCErrorCode::SocketError);
        throw std::runtime_error("socket error");
    }

    ssize_t res = ::recv(sockfd, reinterpret_cast<char *>(dest), size, 0);

    //logger->trace("::recv res={}", res);

    if (res == SOCKET_ERROR)
    {
        int err = WSAGetLastError();

        if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK)
        {
            // FIXME return make_error_code(MVLCErrorCode::SocketReadTimeout);
            throw std::runtime_error("socket read timeout");
        }

        // FIXME return make_error_code(MVLCErrorCode::SocketError);
        throw std::runtime_error("socket error");
    }

#if 0
    if (res >= static_cast<ssize_t>(sizeof(u32)))
    {
        util::log_buffer(
            std::cerr,
            basic_string_view<const u32>(reinterpret_cast<const u32 *>(dest), res / sizeof(u32)),
            "32-bit words in buffer from ::recv()");
    }
#endif

    bytesTransferred = res;

    return {};
}
#else
static inline std::error_code receive_one_packet(int sockfd, u8 *dest, size_t size,
                                                 u16 &bytesTransferred, int)
{
    bytesTransferred = 0u;

    ssize_t res = ::recv(sockfd, reinterpret_cast<char *>(dest), size, 0);

    if (res < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            // FIXME return make_error_code(MVLCErrorCode::SocketReadTimeout);
            throw std::runtime_error("socket read timeout");
        }

         return std::error_code(errno, std::system_category());
    }

    bytesTransferred = res;
    return {};
}
#endif

u16 calculate_checksum(const CommandPacketHeader &cmd)
{
    u16 result = 0u;
    const u16 *b = reinterpret_cast<const u16 *>(&cmd);

	for (unsigned i = 0; i < cmd.bufferLength; i++)
		result ^= b[i];

    return result;
}

unsigned get_data_length(const CommandPacketHeader &packet)
{
    assert(packet.bufferLength >= packet.headerLength);
    unsigned dataLen = packet.bufferLength - packet.headerLength;
    return dataLen;
}

template<typename Out>
Out &format(Out &out, const CommandPacketHeader &packet)
{
    out << "CommandPacket:" << std::endl;
    out << fmt::format("  bufferLength={}", packet.bufferLength) << std::endl;
    out << fmt::format("  bufferType=0x{:04X}", packet.bufferType) << std::endl;
    out << fmt::format("  headerLength={}", packet.headerLength) << std::endl;
    out << fmt::format("  bufferNumber={}", packet.bufferNumber) << std::endl;
    bool cmdFailed = (packet.cmd & (1u << 15));
    out << fmt::format("  cmd={}/0x{:04X} ({})",
                       packet.cmd, packet.cmd, cmdFailed ? "failed" : "success")
        << std::endl;
    out << fmt::format("  deviceStatus=0x{:02X}", packet.deviceStatus) << std::endl;
    out << fmt::format("  deviceId=0x{:02X}", packet.deviceId) << std::endl;
    out << fmt::format("  time={}, {}, {}", packet.time[0], packet.time[1], packet.time[2]) << std::endl;
    out << fmt::format("  headerChecksum=0x{:04X}", packet.headerChecksum) << std::endl;

    u16 dataLen = get_data_length(packet);

    out << fmt::format("  calculated data length={}", dataLen) << std::endl;

    for (unsigned i=0; i<dataLen; ++i)
        out << fmt::format("    data[{}] = 0x{:04X}", i, packet.data[i]) << std::endl;

    return out;
}

std::string to_string(const CommandPacketHeader &packet)
{
    std::stringstream ss;
    format(ss, packet);
    return ss.str();
}

} // end anon namespace

struct McpdVersionInfo
{
    // major and minor version numbers for cpu and fpga
    u16 cpu[2];
    u8 fpga[2];
};

std::error_code command_transaction(
    int sock,
    const CommandPacketHeader &request,
    CommandPacketHeader &response)
{

    {
        spdlog::trace("request: {}", to_string(request));

        size_t bytesTransferred = 0u;

        if (auto ec = write_to_socket(
                sock,
                reinterpret_cast<const u8 *>(&request),
                sizeof(request),
                bytesTransferred))
        {
            return ec;
        }
    }

    {
        u16 bytesTransferred = 0u;

        if (auto ec = receive_one_packet(
            sock,
            reinterpret_cast<u8 *>(&response),
            sizeof(response),
            bytesTransferred,
            DefaultReadTimeout_ms))
        {
            return ec;
        }

        spdlog::trace("response: {}", to_string(response));

        if (response.cmd & CommandFailBit)
            throw std::runtime_error("cmd failbit set in response from mcpd!"); // FIXME: return ec

        if (response.cmd != request.cmd)
            throw std::runtime_error("cmd -> response mismatch!"); // FIXME: return ec
    }

    return {};
}

std::error_code prepare_command_packet(
    CommandPacketHeader &dest,
    const CommandType &cmd,
    u8 mcpdId,
    const u16 *data, u16 dataSize)
{
    if (dataSize > CommandPacketMaxDataWords)
        return std::make_error_code(std::errc::no_buffer_space);

    dest = {};
    dest.bufferType = BUFTYPE;
    dest.headerLength = CMDHEADLEN;
    dest.cmd = static_cast<u8>(cmd);
    dest.deviceId = mcpdId;

    std::copy(data, data + dataSize, dest.data);

    dest.bufferLength = dest.headerLength + dataSize;
    dest.headerChecksum = calculate_checksum(dest);

    return {};
}

void prepare_command_packet(
    CommandPacketHeader &dest,
    const CommandType &cmd,
    u8 mcpdId,
    const std::vector<u16> &data = {})
{
    prepare_command_packet(dest, cmd, mcpdId, data.data(), data.size());
}

CommandPacketHeader make_command_packet(const CommandType &cmd, u8 mcpdId, const u16 *data, u16 dataSize)
{
    CommandPacketHeader result = {};
    prepare_command_packet(result, cmd, mcpdId, data, dataSize);
    return result;
}

CommandPacketHeader make_command_packet(const CommandType &cmd, u8 mcpdId, const std::vector<u16> &data = {})
{
    return make_command_packet(cmd, mcpdId, data.data(), data.size());
}

std::error_code mcpd_get_version(int sock, u8 mcpdId, McpdVersionInfo &vi)
{
    auto request = make_command_packet(CommandType::GetVersion, mcpdId);
    CommandPacketHeader response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    // TODO: turn this into an actual error check
    assert(get_data_length(response) == 3);

    vi.cpu[0] = response.data[0];
    vi.cpu[1] = response.data[1];
    vi.fpga[0] = (response.data[2] >> 8) & 0xffffu;
    vi.fpga[1] = (response.data[2] >> 0) & 0xffffu;

    return {};
}

std::error_code mcpd_set_id(int sock, u8 mcpdId, u8 newId)
{
    auto request = make_command_packet(CommandType::SetId, mcpdId, { newId });
    CommandPacketHeader response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
}

std::error_code mcpd_set_protocol_parameters(
    int sock, u8 mcpdId,
    const std::array<u8, 4> &mcpdIpAddress,
    const std::array<u8, 4> &cmdDestAddress,
    u16 cmdDestPort,
    const std::array<u8, 4> &dataDestAddress,
    u16 dataDestPort)
{
    std::vector<u16> data;

    // Format: mcpdIp, dataIp, cmdPort, dataPort, cmdIp
    std::copy(std::begin(mcpdIpAddress), std::end(mcpdIpAddress), std::back_inserter(data));
    std::copy(std::begin(dataDestAddress), std::end(dataDestAddress), std::back_inserter(data));
    data.emplace_back(cmdDestPort);
    data.emplace_back(dataDestPort);
    std::copy(std::begin(cmdDestAddress), std::end(cmdDestAddress), std::back_inserter(data));

    auto request = make_command_packet(CommandType::SetProtoParams, mcpdId, data);

    CommandPacketHeader response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
}

// Note: assumes ipv4. Might be too naive.
std::array<u8, 4> to_array(const struct sockaddr_in &addr)
{
    u32 ipv4 = ntohl(addr.sin_addr.s_addr);

    std::array<u8, 4> ret = {};

    ret[0] = (ipv4 >> 24) & 0xFFFFu;
    ret[1] = (ipv4 >> 16) & 0xFFFFu;
    ret[2] = (ipv4 >>  8) & 0xFFFFu;
    ret[3] = (ipv4 >>  0) & 0xFFFFu;

    return ret;
}

// This variant does not modify the ip address of the mcpd itself.
std::error_code mcpd_set_protocol_parameters(
    int sock, u8 mcpdId,
    const std::array<u8, 4> &cmdDestAddress,
    u16 cmdDestPort,
    const std::array<u8, 4> &dataDestAddress,
    u16 dataDestPort)
{
    return mcpd_set_protocol_parameters(
        sock, mcpdId,
        { 0u, 0u, 0u, 0u},
        cmdDestAddress, cmdDestPort,
        dataDestAddress, dataDestPort);
}

std::error_code mcpd_set_protocol_parameters(
    int sock, u8 mcpdId,
    const std::string &mcpdAddress,
    const std::string &cmdDestAddress,
    u16 cmdDestPort,
    const std::string &dataDestAddress,
    u16 dataDestPort)
{
    struct sockaddr_in mcpdAddr = {};
    struct sockaddr_in cmdDestAddr = {};
    struct sockaddr_in dataDestAddr = {};

    if (auto ec = lookup(mcpdAddress, DefaultMcpdPort, mcpdAddr))
        throw ec;

    if (auto ec = lookup(cmdDestAddress, cmdDestPort, cmdDestAddr))
        throw ec;

    if (auto ec = lookup(dataDestAddress, dataDestPort, dataDestAddr))
        throw ec;

    return mcpd_set_protocol_parameters(
        sock, mcpdId,
        to_array(mcpdAddr),
        to_array(cmdDestAddr),
        cmdDestPort,
        to_array(dataDestAddr),
        dataDestPort);
}

std::error_code mcpd_set_protocol_parameters(
    int sock, u8 mcpdId,
    const std::string &cmdDestAddress,
    u16 cmdDestPort,
    const std::string &dataDestAddress,
    u16 dataDestPort)
{
    return mcpd_set_protocol_parameters(
        sock, mcpdId,
        "0.0.0.0", // mcpdAddress: "no change"
        cmdDestAddress,
        cmdDestPort,
        dataDestAddress,
        dataDestPort);
}

std::error_code mcpd_set_protocol_parameters(
    int sock, u8 mcpdId,
    u16 cmdDestPort,
    u16 dataDestPort)
{
    return mcpd_set_protocol_parameters(
        sock, mcpdId,
        "0.0.0.0", // mcpdAddress: "no change"
        "0.0.0.0", // cmdDestAddress: "this computer"
        cmdDestPort,
        "0.0.0.0", // dataDestAddress: "this computer"
        dataDestPort);
}

std::error_code mcpd_set_run_id(int sock, u8 mcpdId, u16 runId)
{
    auto request = make_command_packet(CommandType::SetRunId, mcpdId, { runId });
    CommandPacketHeader response = {};
    return command_transaction(sock, request, response);
}

std::error_code mcpd_reset(int sock, u8 mcpdId)
{
    auto request = make_command_packet(CommandType::Reset, mcpdId);
    CommandPacketHeader response = {};
    return command_transaction(sock, request, response);
}

std::error_code mcpd_start_daq(int sock, u8 mcpdId)
{
    auto request = make_command_packet(CommandType::StartDAQ, mcpdId);
    CommandPacketHeader response = {};
    return command_transaction(sock, request, response);
}

std::error_code mcpd_stop_daq(int sock, u8 mcpdId)
{
    auto request = make_command_packet(CommandType::StopDAQ, mcpdId);
    CommandPacketHeader response = {};
    return command_transaction(sock, request, response);
}

std::error_code mcpd_continue_daq(int sock, u8 mcpdId)
{
    auto request = make_command_packet(CommandType::ContinueDAQ, mcpdId);
    CommandPacketHeader response = {};
    return command_transaction(sock, request, response);
}

std::error_code mcpd_get_all_parameters(int sock, u8 mcpdId, McpdParams &dest)
{
    auto request = make_command_packet(CommandType::GetParams, mcpdId, { });

    CommandPacketHeader response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    dest.adc[0] = response.data[0];
    dest.adc[1] = response.data[1];
    dest.dac[0] = response.data[2];
    dest.dac[1] = response.data[3];
    dest.ttlOut = response.data[4];
    dest.ttlIn  = response.data[5];
    dest.eventCounters[0] = response.data[6];
    dest.eventCounters[1] = response.data[7];
    dest.eventCounters[2] = response.data[8];

    for (size_t i=0; i<McpdParamCount; ++i)
    {
        dest.params[i][0] = response.data[9+0+3*i];
        dest.params[i][1] = response.data[9+1+3*i];
        dest.params[i][2] = response.data[9+2+3*i];
    }

    return {};
}

std::error_code mcpd_get_bus_capabilities(int sock, u8 mcpdId, BusCapabilities &caps)
{
    auto request = make_command_packet(CommandType::GetBusCapabilities, mcpdId);

    CommandPacketHeader response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    caps.available = response.data[0];
    caps.selected = response.data[1];

    return {};
}

std::error_code mcpd_set_bus_capabilities(int sock, u8 mcpdId, u8 capBits, u8 &resultBits)
{
    auto request = make_command_packet(CommandType::SetBusCapabilities, mcpdId, { capBits });

    CommandPacketHeader response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    resultBits = response.data[0];

    return {};
}

std::error_code mcpd_set_master_clock_value(int sock, u8 mcpdId, u64 clock)
{
    std::array<u16, 3> data =
    {
        static_cast<u16>((clock >>  0) & 0xFFFFu),
        static_cast<u16>((clock >> 16) & 0xFFFFu),
        static_cast<u16>((clock >> 32) & 0xFFFFu),
    };

    auto request = make_command_packet(CommandType::SetClock, mcpdId, data.data(), data.size());
    CommandPacketHeader response = {};
    return command_transaction(sock, request, response);
}

std::error_code mcpd_setup_cell(
    int sock, u8 mcpdId,
    const CellName &cell,
    const TriggerSource &trigSource,
    u16 compareRegisterBitValue)
{
    std::array<u16, 3> data =
    {
        static_cast<u16>(cell),
        static_cast<u16>(trigSource),
        compareRegisterBitValue
    };

    auto request = make_command_packet(CommandType::SetCell, mcpdId, data.data(), data.size());
    CommandPacketHeader response = {};
    return command_transaction(sock, request, response);
}

std::error_code mcpd_setup_auxtimer(
    int sock, u8 mcpdId,
    u16 timerId,
    u16 compareRegisterValue)
{
    std::array<u16, 2> data =
    {
        static_cast<u16>(timerId),
        static_cast<u16>(compareRegisterValue),
    };

    auto request = make_command_packet(CommandType::SetAuxTimer, mcpdId, data.data(), data.size());
    CommandPacketHeader response = {};
    return command_transaction(sock, request, response);
};

std::error_code mcpd_set_param_source(
    int sock, u8 mcpdId,
    u16 param,
    const CounterSource &source)
{
    std::array<u16, 2> data =
    {
        static_cast<u16>(param),
        static_cast<u16>(source),
    };

    auto request = make_command_packet(CommandType::SetParam, mcpdId, data.data(), data.size());
    CommandPacketHeader response = {};
    return command_transaction(sock, request, response);
}

std::error_code mcpd_set_dac_output_values(
    int sock, u8 mcpdId,
    u16 dac0Value, u16 dac1Value)
{
    std::array<u16, 2> data =
    {
        dac0Value,
        dac1Value,
    };

    auto request = make_command_packet(CommandType::SetDAC, mcpdId, data.data(), data.size());
    CommandPacketHeader response = {};
    return command_transaction(sock, request, response);
}

std::error_code mcpd_send_serial_string(
    int sock, u8 mcpdId,
    const std::string &str)
{
    if (str.size() > CommandPacketMaxDataWords)
        throw std::runtime_error("serial data string too long");

    std::vector<u16> data;
    data.reserve(str.size() + 1);

    data.emplace_back(str.size());

    for (auto it=std::begin(str); it!=std::end(str); ++it)
        data.emplace_back(static_cast<u16>(*it));

    auto request = make_command_packet(CommandType::SendSerial, mcpdId, data.data(), data.size());
    CommandPacketHeader response = {};
    return command_transaction(sock, request, response);
}

std::error_code mcpd_read_serial_string(
    int sock, u8 mcpdId,
    std::string &dest)
{
    auto request = make_command_packet(CommandType::ReadSerial, mcpdId);
    CommandPacketHeader response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    u16 len = response.data[0];

    if (len >= CommandPacketMaxDataWords - 1)
        throw std::runtime_error("invalid string length from ReadSerial");

    dest.clear();
    dest.reserve(len);

    for (u16 i=0; i<len; ++i)
    {
        char c = response.data[1 + i];
        dest.push_back(c);
    }

    return {};
}

std::error_code mpsd_set_gain(
    int sock, u8 mcpdId,
    u8 mpsdId, u8 channel, u8 gain)
{
    std::array<u16, 3> data =
    {
        static_cast<u16>(mpsdId),
        static_cast<u16>(channel),
        static_cast<u16>(gain),
    };

    auto request = make_command_packet(CommandType::SetGain, mcpdId, data.data(), data.size());
    CommandPacketHeader response = {};
    return command_transaction(sock, request, response);
}

std::error_code mpsd_set_threshold(
    int sock, u8 mcpdId,
    u8 mpsdId, u8 threshold)
{
    std::array<u16, 2> data =
    {
        static_cast<u16>(mpsdId),
        static_cast<u16>(threshold),
    };

    auto request = make_command_packet(CommandType::SetThreshold, mcpdId, data.data(), data.size());
    CommandPacketHeader response = {};
    return command_transaction(sock, request, response);
}

std::error_code mpsd_set_pulser(
    int sock, u8 mcpdId,
    u8 mpsdId, u8 channel,
    const ChannelPosition &pos,
    u8 amplitude,
    const PulserState &state)
{
    std::array<u16, 5> data =
    {
        static_cast<u16>(mpsdId),
        static_cast<u16>(channel),
        static_cast<u16>(pos),
        static_cast<u16>(amplitude),
        static_cast<u16>(state),
    };

    auto request = make_command_packet(CommandType::SetPulser, mcpdId, data.data(), data.size());
    CommandPacketHeader response = {};
    return command_transaction(sock, request, response);
}

std::error_code mpsd_set_mode(
    int sock, u8 mcpdId,
    u8 mpsdId,
    const MpsdMode &mode)
{
    std::array<u16, 2> data =
    {
        static_cast<u16>(mpsdId),
        static_cast<u16>(mode),
    };

    auto request = make_command_packet(CommandType::SetMpsdMode, mcpdId, data.data(), data.size());
    CommandPacketHeader response = {};
    return command_transaction(sock, request, response);
}

std::error_code mpsd_get_params(
    int sock, u8 mcpdId,
    u8 mpsdId,
    MpsdParameters &dest)
{
    std::array<u16, 1> data =
    {
        static_cast<u16>(mpsdId),
    };

    auto request = make_command_packet(CommandType::GetMpsdParams, mcpdId, data.data(), data.size());
    CommandPacketHeader response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    dest.mpsdId = response.data[0];
    dest.busTxCaps = response.data[1];
    dest.fastTxFormat = response.data[2];
    dest.firmwareRevision = response.data[3];

    return {};
}

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::trace);

    //
    // command socket setup
    //

    struct sockaddr_in cmdAddr = {};

    if (auto ec = lookup(DefaultMcpdIpAddress, DefaultMcpdPort, cmdAddr))
        throw ec;

    int cmdSock = socket(AF_INET, SOCK_DGRAM, 0);

    if (cmdSock < 0)
    {
        auto ec = std::error_code(errno, std::system_category());
        //logger->error("socket() failed for command pipe: {}", ec.message().c_str());
        throw ec;
    }

    // bind the socket
    {
        struct sockaddr_in localAddr = {};
        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = INADDR_ANY;

        if (::bind(cmdSock, reinterpret_cast<struct sockaddr *>(&localAddr),
                   sizeof(localAddr)))
        {
                throw std::error_code(errno, std::system_category());
        }
    }

    // connect
    if (::connect(cmdSock, reinterpret_cast<struct sockaddr *>(&cmdAddr),
                            sizeof(cmdAddr)))
    {
        auto ec = std::error_code(errno, std::system_category());
        //logger->error("connect() failed for command socket: {}", ec.message().c_str());
        //close_sockets();
        throw ec;
    }

    // set the socket timeouts
    if (auto ec = set_socket_read_timeout(cmdSock, DefaultReadTimeout_ms))
        throw ec;

    if (auto ec = set_socket_write_timeout(cmdSock, DefaultWriteTimeout_ms))
        throw ec;

    //
    // -----------------------------------------------------------------------
    //

    McpdVersionInfo versionInfo = {};
    u8 mcpdId = 42u;

    if (auto ec = mcpd_get_version(cmdSock, mcpdId, versionInfo))
        throw ec;

    using std::cout;
    using std::endl;

    cout << "cpu major=" << versionInfo.cpu[0] << endl;
    cout << "cpu minor=" << versionInfo.cpu[1] << endl;
    cout << "fpga major=" << static_cast<u16>(versionInfo.fpga[0]) << endl;
    cout << "fpga minor=" << static_cast<u16>(versionInfo.fpga[1]) << endl;
    cout << endl;


    // XXX: Need to carry the mcpdId around and update it on each change
    u8 mcpdNewId = 42u;
    if (auto ec = mcpd_set_id(cmdSock, mcpdId, mcpdNewId))
        throw ec;
    mcpdId = mcpdNewId;

    //
    // data socket setup
    //

    int dataSock = socket(AF_INET, SOCK_DGRAM, 0);

    if (dataSock < 0)
    {
        auto ec = std::error_code(errno, std::system_category());
        //logger->error("socket() failed for data pipe: {}", ec.message().c_str());
        throw ec;
    }

    // bind the socket
    {
        struct sockaddr_in localAddr = {};
        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = INADDR_ANY;

        if (::bind(dataSock, reinterpret_cast<struct sockaddr *>(&localAddr),
                   sizeof(localAddr)))
        {
                throw std::error_code(errno, std::system_category());
        }
    }

    // connect
    if (::connect(dataSock, reinterpret_cast<struct sockaddr *>(&cmdAddr),
                            sizeof(cmdAddr)))
    {
        auto ec = std::error_code(errno, std::system_category());
        //logger->error("connect() failed for command socket: {}", ec.message().c_str());
        //close_sockets();
        throw ec;
    }

    // set the socket timeouts
    if (auto ec = set_socket_read_timeout(dataSock, DefaultReadTimeout_ms))
        throw ec;

    if (auto ec = set_socket_write_timeout(dataSock, DefaultWriteTimeout_ms))
        throw ec;

    struct sockaddr_in localDataAddr = {};
    socklen_t localAddrLen = sizeof(localDataAddr);

    if (::getsockname(
            dataSock, reinterpret_cast<struct sockaddr *>(&localDataAddr),
            &localAddrLen) != 0)
    {
        throw std::system_error(errno, std::system_category());
    }

    // XXX: leftoff here


    // TODO: Create and bind an extra socket for the data stream. Figure out the local
    // port of the socket and setup the mcpd to use that port
    u16 localDataPort = 0;

    /*
    mcpd_set_protocol_parameters(
        cmdSock,
        mcpdId,
        0,
        localDataPort);
        */


    return 0;
}
