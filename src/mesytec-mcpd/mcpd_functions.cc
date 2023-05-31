#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>
#include <thread>

#include "mcpd_functions.h"

namespace mesytec
{
namespace mcpd
{

std::error_code send_command(int sock, const CommandPacket &request)
{
    size_t bytesWritten = 0u;
    const size_t bytesToWrite = request.bufferLength * sizeof(u16);

    auto ec = write_to_socket(
        sock,
        reinterpret_cast<const u8 *>(&request),
        bytesToWrite,
        bytesWritten);

    return ec;
}

std::error_code receive_response(int sock, CommandPacket &response)
{
    size_t bytesRead = 0u;

    auto ec = receive_one_packet(
        sock,
        reinterpret_cast<u8 *>(&response),
        sizeof(response),
        bytesRead,
        DefaultReadTimeout_ms);

    return ec;
}

namespace
{

// Internal version of command_transaction() which allows to ignore the
// packet.cmd error status. Useful for e.g. the SetGain command as that returns
// an error for all non-zero mpsdIds.
std::error_code command_transaction_(
    int sock,
    const CommandPacket &request,
    CommandPacket &response,
    bool ignoreProtoError)
{
    const unsigned MaxAttempts = 5;

    for (unsigned attempt=0; attempt<MaxAttempts; ++attempt)
    {
        {
            spdlog::trace("request (attempt={}/{}): {}",
                          attempt+1, MaxAttempts, to_string(request));
            spdlog::trace("request: {}", raw_data_to_string(request));

            auto ec = send_command(sock, request);

            if (ec == SocketErrorType::Timeout)
                continue;
            else if (ec)
                return ec;
        }

        {
            auto ec = receive_response(sock, response);

            if (ec == SocketErrorType::Timeout)
                continue;
            else if (ec)
                return ec;

            spdlog::trace("response: {}", to_string(response));
            spdlog::trace("response: {}", raw_data_to_string(response));

            if (response.bufferType != CommandPacketBufferType)
            {
                spdlog::warn("unexpected response buffer type 0x{:04X}",
                             response.bufferType);
                continue;
            }

            if ((response.cmd & CommandNumberMask) != request.cmd)
            {
                spdlog::warn("request/response cmd mismatch: req={}, resp={}",
                             request.cmd, response.cmd & CommandNumberMask);
                continue;
            }

            if (!ignoreProtoError && has_error(response))
                return make_error_code(static_cast<CommandError>(get_error_value(response)));

            // success
            return {};
        }
    }

    return make_error_code(std::errc::protocol_error);
}

} // end anon namespace

std::error_code command_transaction(
    int sock,
    const CommandPacket &request,
    CommandPacket &response)
{
    return command_transaction_(sock, request, response, false);
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
    dest.bufferType = CommandPacketBufferType;
    dest.headerLength = CommandPacketHeaderWords;
    dest.cmd = static_cast<u8>(cmd);
    dest.deviceId = mcpdId;

    std::copy(data, data + dataSize, dest.data);
    dest.data[dataSize] = BufferTerminator;

    dest.bufferLength = dest.headerLength + dataSize + 1;
    dest.headerChecksum = calculate_checksum(dest);

    return {};
}

CommandPacket make_command_packet(const CommandType &cmd, u8 mcpdId, const u16 *data, u16 dataSize)
{
    CommandPacket result = {};
    prepare_command_packet(result, cmd, mcpdId, data, dataSize);
    return result;
}

std::error_code mcpd_get_version(int sock, u8 mcpdId, McpdVersionInfo &vi)
{
    auto request = make_command_packet(CommandType::GetVersion, mcpdId);
    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    if (get_data_length(response) < 3)
    {
        spdlog::error("GetVersion response too short, expected 3 data words, got {}", get_data_length(response));
        return make_error_code(std::errc::protocol_error);
    }

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

std::error_code mcpd_set_network_parameters(
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

    auto ec = command_transaction(sock, request, response);

    // Setting the network parameters causes a write of the flash memory which
    // takes time so delay here for a bit.
    if (!ec)
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

    return ec;
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
std::error_code mcpd_set_network_parameters(
    int sock, u8 mcpdId,
    const std::array<u8, 4> &cmdDestAddress,
    u16 cmdDestPort,
    const std::array<u8, 4> &dataDestAddress,
    u16 dataDestPort)
{
    return mcpd_set_network_parameters(
        sock, mcpdId,
        { 0u, 0u, 0u, 0u},
        cmdDestAddress, cmdDestPort,
        dataDestAddress, dataDestPort);
}

std::error_code mcpd_set_network_parameters(
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

    if (auto ec = lookup(mcpdAddress, McpdDefaultPort, mcpdAddr))
        return ec;

    if (auto ec = lookup(cmdDestAddress, cmdDestPort, cmdDestAddr))
        return ec;

    if (auto ec = lookup(dataDestAddress, dataDestPort, dataDestAddr))
        return ec;

    return mcpd_set_network_parameters(
        sock, mcpdId,
        to_array(mcpdAddr),
        to_array(cmdDestAddr),
        cmdDestPort,
        to_array(dataDestAddr),
        dataDestPort);
}

std::error_code mcpd_set_network_parameters(
    int sock, u8 mcpdId,
    const std::string &cmdDestAddress,
    u16 cmdDestPort,
    const std::string &dataDestAddress,
    u16 dataDestPort)
{
    return mcpd_set_network_parameters(
        sock, mcpdId,
        "0.0.0.0", // mcpdAddress: "no change"
        cmdDestAddress,
        cmdDestPort,
        dataDestAddress,
        dataDestPort);
}

std::error_code mcpd_set_network_parameters(
    int sock, u8 mcpdId,
    u16 cmdDestPort,
    u16 dataDestPort)
{
    return mcpd_set_network_parameters(
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
    return mcpd_set_network_parameters(
        sock, mcpdId,
        address, // mcpdAddress
        "0.0.0.0", // cmdDestAddress (no change)
        0, // cmdDestPort (no change)
        "0.0.0.0", // dataDestAddress (no change)
        0); // dataDestPort (no change)
};

std::error_code mcpd_set_data_dest_port(int sock, u8 mcpdId, u16 dataDestPort)
{
    return mcpd_set_network_parameters(
        sock, mcpdId,
        "0.0.0.0", // mcpdAddress: "no change"
        "0.0.0.0", // cmdDestAddress (no change)
        0, // cmdDestPort (no change)
        "0.0.0.0", // dataDestAddress (no change)
        dataDestPort);
}

std::error_code mcpd_set_ip_address_and_data_dest(
    int sock, u8 mcpdId, const std::string &address,
    const std::string &dataDestAddress, u16 dataDestPort)
{
    return mcpd_set_network_parameters(
        sock, mcpdId,
        address, // mcpdAddress
        "0.0.0.0", // cmdDestAddress (no change)
        0, // cmdDestPort (no change)
        dataDestAddress, // data destination address
        dataDestPort);
}

std::error_code mcpd_set_run_id(int sock, u8 mcpdId, u16 runId)
{
    auto request = make_command_packet(CommandType::SetRunId, mcpdId, { runId });
    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
}

std::error_code mcpd_reset_daq(int sock, u8 mcpdId)
{
    auto request = make_command_packet(CommandType::Reset, mcpdId);
    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
}

std::error_code mcpd_start_daq(int sock, u8 mcpdId)
{
    auto request = make_command_packet(CommandType::StartDAQ, mcpdId);
    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
}

std::error_code mcpd_stop_daq(int sock, u8 mcpdId)
{
    auto request = make_command_packet(CommandType::StopDAQ, mcpdId);
    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
}

std::error_code mcpd_continue_daq(int sock, u8 mcpdId)
{
    auto request = make_command_packet(CommandType::ContinueDAQ, mcpdId);
    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
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

std::error_code mcpd_set_timing_options(int sock, u8 mcpdId, TimingRole role, BusTermination term, bool extSync)
{

    // new and not in our docs: the first data word (the role argument)
    // is apparently a 2-bit field: if bit 1 is set an external clock
    // signal can be input on chopper3.
    u16 arg0 = static_cast<u16>(role) | (extSync << 1);
    u16 arg1 = static_cast<u16>(term);

    auto request = make_command_packet(
        CommandType::SetTiming, mcpdId, { arg0, arg1 });

    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

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

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
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

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
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

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
};

std::error_code mcpd_set_param_source(
    int sock, u8 mcpdId,
    u16 param,
    const DataSource &source)
{
    std::array<u16, 2> data =
    {
        static_cast<u16>(param),
        static_cast<u16>(source),
    };

    auto request = make_command_packet(CommandType::SetParam, mcpdId, data.data(), data.size());
    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
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

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
}

std::error_code mcpd_scan_busses(
    int sock, u8 mcpdId, std::array<u16, McpdBusCount> &dest)
{
    auto request = make_command_packet(CommandType::ReadIds, mcpdId);
    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    for (size_t bus=0; bus<McpdBusCount; ++bus)
        dest[bus] = response.data[bus];

    return {};
}

std::error_code mcpd_write_register(
    int sock, u8 mcpdId, u16 address, u32 value)
{
    std::array<u16, 3> data =
    {
        address,
        static_cast<u16>(value & 0xFFFF),
        static_cast<u16>(value >> 16),
    };

    auto request = make_command_packet(CommandType::WriteRegister, mcpdId, data.data(), data.size());
    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    if (get_data_length(response) < get_data_length(request))
    {
        spdlog::warn("WriteRegister response too short, expected {} data words, got {}",
                     get_data_length(response), get_data_length(request));
    }

    if (!std::equal(response.data, response.data + get_data_length(response),
                    request.data))
    {
        spdlog::warn("WriteRegister response data does not match request data");
    }

    return {};
}

std::error_code mcpd_read_register(
    int sock, u8 mcpdId, u16 address, u32 &dest)
{
    std::array<u16, 1> data =
    {
        address,
    };

    auto request = make_command_packet(CommandType::ReadRegister, mcpdId, data.data(), data.size());
    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    if (get_data_length(response) < 3)
    {
        spdlog::error("ReadRegister response too short, expected 3 data words, got {}",
                      get_data_length(response));
        return make_error_code(std::errc::protocol_error);
    }

    // check address
    if (response.data[0] != address)
    {
        spdlog::warn("ReadRegister: request address != response address: 0x{:04X} != 0x{:04X}",
                     address, response.data[0]);
    }

    dest = response.data[1] | (response.data[2] << 16);

    return {};
}

std::error_code read_peripheral_register(
    int sock, u8 mcpdId, u8 mpsdId,
    u16 registerNumber, u16 &dest)
{
    std::array<u16, 2> data =
    {
        static_cast<u16>(mpsdId),
        registerNumber,
    };

    auto request = make_command_packet(CommandType::ReadPeripheralRegister, mcpdId, data.data(), data.size());
    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    dest = response.data[2];

    return {};
}

std::error_code write_peripheral_register(
    int sock, u8 mcpdId, u8 mpsdId,
    u16 registerNumber, u16 registerValue)
{
    std::array<u16, 3> data =
    {
        static_cast<u16>(mpsdId),
        registerNumber,
        registerValue,
    };

    auto request = make_command_packet(CommandType::WritePeripheralRegister, mcpdId, data.data(), data.size());
    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
}

#if 0 // untested/not implemented according to Gregor
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
#endif

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

    // The command always sets the error flag in the response. Call the
    // internal version of command_transaction_() here ignoring the error flag.
    if (auto ec = command_transaction_(sock, request, response, true))
        return ec;

    return {};
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

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
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

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    assert(get_data_length(response) >= 5);

    return {};
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

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
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

std::error_code mstd_set_gain(
    int sock, u8 mcpdId,
    u8 mstdId, u8 channel, u8 gain)
{
    std::array<u16, 3> data =
    {
        static_cast<u16>(mstdId),
        static_cast<u16>(channel),
        static_cast<u16>(gain),
    };

    auto request = make_command_packet(CommandType::SetMstdGain, mcpdId, data.data(), data.size());
    CommandPacket response = {};

    if (auto ec = command_transaction(sock, request, response))
        return ec;

    return {};
}

}
}
