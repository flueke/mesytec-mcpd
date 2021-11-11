#include <ios>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include <thread>
#include <fstream>

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

#include <iostream>

#include "mesytec-mcpd.h"

namespace
{
using namespace mesytec::mcpd;


} // end anon namespace

std::error_code command_transaction(
    int sock,
    const CommandPacket &request,
    CommandPacket &response)
{
    const unsigned MaxAttempts = 3;

    for (unsigned attempt=0; attempt<MaxAttempts; ++attempt)
    {
        {
            spdlog::trace("request (attempt={}/{}): {}",
                          attempt+1, MaxAttempts, to_string(request));

            size_t bytesWritten = 0u;

            auto ec = write_to_socket(
                    sock,
                    reinterpret_cast<const u8 *>(&request),
                    sizeof(request),
                    bytesWritten);

            if (ec == std::errc::resource_unavailable_try_again)
                continue;
            else if (ec)
                return ec;
        }

        {
            size_t bytesRead = 0u;

            auto ec = receive_one_packet(
                sock,
                reinterpret_cast<u8 *>(&response),
                sizeof(response),
                bytesRead,
                DefaultReadTimeout_ms);

            if (ec == std::errc::resource_unavailable_try_again)
                continue;
            else if (ec)
                return ec;

            spdlog::trace("response: {}", to_string(response));

            if ((response.cmd & CommandNumberMask) != request.cmd)
            {
                spdlog::warn("request/response cmd mismatch: req={}, resp={}",
                             request.cmd, response.cmd & CommandNumberMask);
                continue;
            }

#if 0
            if (response.cmd & CommandErrorMask)
                throw std::runtime_error("cmd error set in response from mcpd!");

            if (response.cmd != request.cmd)
                throw std::runtime_error("cmd -> response mismatch!"); // FIXME: return custom ec
#endif
        }
    }

    return {};
}

std::error_code prepare_command_packet(
    CommandPacket &dest,
    const CommandType &cmd,
    u8 mcpdId,
    const u16 *data, u16 dataSize)
{
    if (dataSize + 1u > CommandPacketMaxDataWords)
        return std::make_error_code(std::errc::no_buffer_space);

    dest = {};
    dest.bufferType = CMDBUFTYPE;
    dest.headerLength = CMDHEADLEN;
    dest.cmd = static_cast<u8>(cmd);
    dest.deviceId = mcpdId;

    std::copy(data, data + dataSize, dest.data);
    dest.data[dataSize] = BufferTerminator;

    dest.bufferLength = dest.headerLength + dataSize;
    dest.headerChecksum = calculate_checksum(dest);

    return {};
}

void prepare_command_packet(
    CommandPacket &dest,
    const CommandType &cmd,
    u8 mcpdId,
    const std::vector<u16> &data = {})
{
    prepare_command_packet(dest, cmd, mcpdId, data.data(), data.size());
}

CommandPacket make_command_packet(const CommandType &cmd, u8 mcpdId, const u16 *data, u16 dataSize)
{
    CommandPacket result = {};
    prepare_command_packet(result, cmd, mcpdId, data, dataSize);
    return result;
}

CommandPacket make_command_packet(const CommandType &cmd, u8 mcpdId, const std::vector<u16> &data = {})
{
    return make_command_packet(cmd, mcpdId, data.data(), data.size());
}

std::error_code mcpd_get_version(int sock, u8 mcpdId, McpdVersionInfo &vi)
{
    auto request = make_command_packet(CommandType::GetVersion, mcpdId);
    CommandPacket response = {};

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
    CommandPacket response = {};

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

    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
}

// Note: assumes ipv4.
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

std::error_code mcpd_set_ip_address(
    int sock, u8 mcpdId,
    const std::string &address)
{
    return mcpd_set_protocol_parameters(
        sock, mcpdId,
        address, // mcpdAddress
        "0.0.0.0", // cmdDestAddress (no change)
        0, // cmdDestPort (no change)
        "0.0.0.0", // dataDestAddress (no change)
        0); // dataDestPort (no change)
};

std::error_code mcpd_set_run_id(int sock, u8 mcpdId, u16 runId)
{
    auto request = make_command_packet(CommandType::SetRunId, mcpdId, { runId });
    CommandPacket response = {};
    return command_transaction(sock, request, response);
}

std::error_code mcpd_reset(int sock, u8 mcpdId)
{
    auto request = make_command_packet(CommandType::Reset, mcpdId);
    CommandPacket response = {};
    return command_transaction(sock, request, response);
}

std::error_code mcpd_start_daq(int sock, u8 mcpdId)
{
    auto request = make_command_packet(CommandType::StartDAQ, mcpdId);
    CommandPacket response = {};
    return command_transaction(sock, request, response);
}

std::error_code mcpd_stop_daq(int sock, u8 mcpdId)
{
    auto request = make_command_packet(CommandType::StopDAQ, mcpdId);
    CommandPacket response = {};
    return command_transaction(sock, request, response);
}

std::error_code mcpd_continue_daq(int sock, u8 mcpdId)
{
    auto request = make_command_packet(CommandType::ContinueDAQ, mcpdId);
    CommandPacket response = {};
    return command_transaction(sock, request, response);
}

std::error_code mcpd_get_all_parameters(int sock, u8 mcpdId, McpdParams &dest)
{
    auto request = make_command_packet(CommandType::GetParams, mcpdId, { });

    CommandPacket response = {};

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

    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    caps.available = response.data[0];
    caps.selected = response.data[1];

    return {};
}

std::error_code mcpd_set_bus_capabilities(int sock, u8 mcpdId, u8 capBits, u8 &resultBits)
{
    auto request = make_command_packet(CommandType::SetBusCapabilities, mcpdId, { capBits });

    CommandPacket response = {};

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
    CommandPacket response = {};
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
    CommandPacket response = {};
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
    CommandPacket response = {};
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
    CommandPacket response = {};
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
    CommandPacket response = {};
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
    CommandPacket response = {};
    return command_transaction(sock, request, response);
}

std::error_code mcpd_read_serial_string(
    int sock, u8 mcpdId,
    std::string &dest)
{
    auto request = make_command_packet(CommandType::ReadSerial, mcpdId);
    CommandPacket response = {};

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
    CommandPacket response = {};
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
    CommandPacket response = {};
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
    CommandPacket response = {};
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
    CommandPacket response = {};
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
    CommandPacket response = {};

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

    if (auto ec = lookup("192.168.43.42", DefaultMcpdPort, cmdAddr))
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

    using std::cout;
    using std::endl;

    //
    // data socket setup --------------------------------------------------------
    //
    struct sockaddr_in dataAddr = {};

    if (auto ec = lookup("192.168.43.42", DefaultMcpdPort+1, dataAddr))
        throw ec;

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
        localAddr.sin_port = htons(DefaultMcpdPort+1); // note: this is the local port!

        if (::bind(dataSock, reinterpret_cast<struct sockaddr *>(&localAddr),
                   sizeof(localAddr)))
        {
                throw std::error_code(errno, std::system_category());
        }
    }

    // connect
    if (::connect(dataSock, reinterpret_cast<struct sockaddr *>(&dataAddr),
                            sizeof(dataAddr)))
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


    const u8 mcpdId =  0u;
    u16 localDataPort = ntohs(localDataAddr.sin_port);

    cout << fmt::format("localDataPort={}, 0x{:04x}", localDataPort, localDataPort) << endl << endl;

#if 0
    if (auto ec = mcpd_set_ip_address(
        cmdSock,
        mcpdId,
        "192.168.43.42"))
        throw ec;

    return 0;
#endif

#if 0
    if (auto ec = mcpd_set_protocol_parameters(
        cmdSock,
        mcpdId,
        0,
        localDataPort))
        throw ec;
#endif
#if 0
    if (auto ec = mcpd_set_ip_address(cmdSock, mcpdId, "192.168.168.42"))
        throw ec;
#endif
#if 0
    McpdVersionInfo versionInfo = {};
    u8 mcpdId = 42u;

    if (auto ec = mcpd_get_version(cmdSock, mcpdId, versionInfo))
        throw ec;


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
#endif
#if 0

    if (auto ec = mcpd_stop_daq(cmdSock, mcpdId))
        throw ec;
#endif

#if 1

    for (u8 mpsdId=1; mpsdId<2; ++mpsdId)
    {

        // sets the gain for all channels
#if 1
        if (auto ec = mpsd_set_gain(
                cmdSock, mcpdId,
                mpsdId, 8, 200))
            throw ec;
#endif

#if 0
        // threshold
        if (auto ec = mpsd_set_threshold(
                cmdSock, mcpdId, mpsdId, 255))
            throw ec;

#endif

#if 0
        // enable pulser for all channels
        for (u8 channel = 0; channel < 8; ++channel)
        {
            u8 amplitude = 255;

            if (auto ec = mpsd_set_pulser(
                    cmdSock, mcpdId, mpsdId, channel, ChannelPosition::Center,
                    amplitude, PulserState::On))
                throw ec;
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

#endif

    return 0;

    if (auto ec = mcpd_start_daq(cmdSock, mcpdId))
        throw ec;

    cout << "daq started, writing data to ./mcpd.lst" << endl;
    std::ofstream lstOut("./mcpd.list");
    //getchar();

    const int PacketsToReceive = 10000;

    for (int i=0; i<PacketsToReceive; ++i)
    {
        DataPacket dataPacket = {};

        size_t bytesTransferred = 0u;
        if (auto ec = receive_one_packet(
                dataSock,
                reinterpret_cast<u8 *>(&dataPacket),
                sizeof(dataPacket),
                bytesTransferred,
                DefaultReadTimeout_ms))
        {
            spdlog::error("data packet read loop: {}", ec.message());
        }
        else
        {
            //spdlog::trace("data packet #{}: {}", i, to_string(dataPacket));
            lstOut.write(reinterpret_cast<const char *>(&dataPacket), sizeof(dataPacket));
        }
    }

    if (auto ec = mcpd_stop_daq(cmdSock, mcpdId))
        throw ec;

    return 0;
}
