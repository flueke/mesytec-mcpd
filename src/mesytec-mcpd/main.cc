#include "mesytec-mcpd.h"
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
        size_t bytesTransferred = 0u;

        if (auto ec = write_to_socket(
                sock,
                reinterpret_cast<const u8 *>(&request),
                MaxPacketSize,
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
    }

    return {};
}

// Note: internally adds the BufferTerminator to the packets data array. This
// means the BufferTerminator should not be included in the data parameter.
void prepare_command_packet(
    CommandPacketHeader &dest,
    const CommandType &cmd,
    const u16 *data, u16 dataSize)
{
    dest = {};
    dest.bufferType = BUFTYPE;
    dest.headerLength = CMDHEADLEN;
    dest.cmd = static_cast<u8>(cmd);

    // FIXME: make sure we do not overflow
    std::copy(data, data + dataSize, dest.data);
    dest.data[dataSize] = BufferTerminator;

    dest.bufferLength = dest.headerLength + dataSize + 1;
    dest.headerChecksum = calculate_checksum(dest);
}

void prepare_command_packet(
    CommandPacketHeader &dest,
    const CommandType &cmd,
    const std::vector<u16> &data = {})
{
    prepare_command_packet(dest, cmd, data.data(), data.size());
}

CommandPacketHeader make_command_packet(const CommandType &cmd, const u16 *data, u16 dataSize)
{
    CommandPacketHeader result = {};
    prepare_command_packet(result, cmd, data, dataSize);
    return result;
}

CommandPacketHeader make_command_packet(const CommandType &cmd, const std::vector<u16> &data = {})
{
    return make_command_packet(cmd, data.data(), data.size());
}

std::error_code mcpd_get_version(int sock, McpdVersionInfo &vi)
{
    auto request = make_command_packet(CommandType::GetVersion);
    CommandPacketHeader response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    // TODO: turn these into actual error checks
    // use the cmd fail bit to return a CommandFailed error status
    assert(response.cmd == request.cmd);
    assert(get_data_length(response) == 3);

    vi.cpu[0] = response.data[0];
    vi.cpu[1] = response.data[1];
    vi.fpga[0] = (response.data[2] >> 8) & 0xffffu;
    vi.fpga[1] = (response.data[2] >> 0) & 0xffffu;

    return {};
}

std::error_code mcpd_set_id(int sock, u8 mcpdId)
{
    auto request = make_command_packet(CommandType::SetId, { mcpdId });
    CommandPacketHeader response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    // XXX: leftoff here!
}

int main(int argc, char *argv[])
{
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

    // TODO: set socket read and write timeouts


#if 0
    // GetVersion
    CommandPacketHeader request = {};
    request.bufferType = BUFTYPE;
    request.headerLength = CMDHEADLEN;
    request.cmd = static_cast<u8>(CommandType::GetVersion);
    request.data[0] = BufferTerminator;
    u16 cmdDataLen = 1;

    request.bufferLength = request.headerLength + cmdDataLen;
    request.headerChecksum = calculate_checksum(request);

    std::cout << "Request: ";
    format(std::cout, request) << std::endl;

    // send getver command
    {
        size_t bytesTransferred = 0u;

        auto ec = write_to_socket(
            cmdSock,
            reinterpret_cast<const u8 *>(&request),
            MaxPacketSize,
            bytesTransferred);

        if (ec) throw ec;
    }

    CommandPacketHeader response = {};

    // receive response
    {

        u16 bytesTransferred = 0u;

        auto ec = receive_one_packet(
            cmdSock,
            reinterpret_cast<u8 *>(&response),
            sizeof(response),
            bytesTransferred,
            DefaultReadTimeout_ms);

        if (ec) throw ec;
    }

    std::cout << "Response: ";
    format(std::cout, response);
#endif

    McpdVersionInfo versionInfo = {};
    mcpd_get_version(cmdSock, versionInfo);

    using std::cout;
    using std::endl;

    cout << "cpu major=" << versionInfo.cpu[0] << endl;
    cout << "cpu minor=" << versionInfo.cpu[1] << endl;
    cout << "fpga major=" << static_cast<u16>(versionInfo.fpga[0]) << endl;
    cout << "fpga minor=" << static_cast<u16>(versionInfo.fpga[1]) << endl;


    return 0;
}
