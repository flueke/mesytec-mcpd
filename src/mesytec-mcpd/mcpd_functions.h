#ifndef __MESYTEC_MCPD_FUNCTIONS_H__
#define __MESYTEC_MCPD_FUNCTIONS_H__

#include <vector>

#include "mcpd_core.h"

namespace mesytec
{
namespace mcpd
{

std::error_code send_command(int sock, const CommandPacket &request);
std::error_code receive_response(int sock, CommandPacket &response);

std::error_code command_transaction(
    int sock,
    const CommandPacket &request,
    CommandPacket &response);

std::error_code prepare_command_packet(
    CommandPacket &dest,
    const CommandType &cmd,
    u8 mcpdId,
    const u16 *data, u16 dataSize);

inline std::error_code prepare_command_packet(
    CommandPacket &dest,
    const CommandType &cmd,
    u8 mcpdId,
    const std::vector<u16> &data = {})
{
    return prepare_command_packet(dest, cmd, mcpdId, data.data(), data.size());
}

CommandPacket make_command_packet(
    const CommandType &cmd, u8 mcpdId,
    const u16 *data, u16 dataSize);

inline CommandPacket make_command_packet(
    const CommandType &cmd, u8 mcpdId,
    const std::vector<u16> &data = {})
{
    return make_command_packet(cmd, mcpdId, data.data(), data.size());
}

inline CommandPacket make_command_packet(
    u16 cmdId, u8 mcpdId,
    const std::vector<u16> &data = {})
{
    return make_command_packet(static_cast<CommandType>(cmdId), mcpdId, data.data(), data.size());
}

template<typename View>
CommandPacket command_packet_from_data(const View &view)
{
    CommandPacket ret = {};
    std::memcpy(reinterpret_cast<u8 *>(&ret),
                view.data(),
                std::min(sizeof(ret), view.size() * sizeof(view[0])));
    return ret;
}

template<typename Packet>
std::vector<u16> packet_to_data(const Packet &packet)
{
    std::vector<u16> ret;
    ret.resize(packet.bufferLength);
    size_t bytesToCopy = std::min(packet.bufferLength * sizeof(u16), sizeof(Packet));
    std::memcpy(reinterpret_cast<u8 *>(ret.data()),
                reinterpret_cast<const u8 *>(&packet),
                bytesToCopy);
    return ret;
}

std::error_code mcpd_get_version(int sock, u8 mcpdId, McpdVersionInfo &vi);
std::error_code mcpd_set_id(int sock, u8 mcpdId, u8 newId);

std::error_code mcpd_set_network_parameters(
    int sock, u8 mcpdId,
    const std::array<u8, 4> &mcpdIpAddress,
    const std::array<u8, 4> &cmdDestAddress,
    u16 cmdDestPort,
    const std::array<u8, 4> &dataDestAddress,
    u16 dataDestPort);

// This variant does not modify the ip address of the mcpd itself.
std::error_code mcpd_set_network_parameters(
    int sock, u8 mcpdId,
    const std::array<u8, 4> &cmdDestAddress,
    u16 cmdDestPort,
    const std::array<u8, 4> &dataDestAddress,
    u16 dataDestPort);

// Allows passing ipv4 address strings like "10.11.12.42"
std::error_code mcpd_set_network_parameters(
    int sock, u8 mcpdId,
    const std::string &mcpdAddress,
    const std::string &cmdDestAddress,
    u16 cmdDestPort,
    const std::string &dataDestAddress,
    u16 dataDestPort);

// This variant does not modify the ip address of the mcpd itself.
std::error_code mcpd_set_network_parameters(
    int sock, u8 mcpdId,
    const std::string &cmdDestAddress,
    u16 cmdDestPort,
    const std::string &dataDestAddress,
    u16 dataDestPort);

// Only changes the ip address of the mcpd, leaves other network settings unchanged.
std::error_code mcpd_set_ip_address(int sock, u8 mcpdId, const std::string &address);

// Only changes the data dest port, leaves other network settings unchanged.
std::error_code mcpd_set_data_dest_port(int sock, u8 mcpdId, u16 dataDestPort);

// Changes only the mcpd ip address and data destination address and port.
std::error_code mcpd_set_ip_address_and_data_dest(
    int sock, u8 mcpdId, const std::string &address,
    const std::string &dataDestAddress, u16 dataDestPort);

std::error_code mcpd_set_run_id(int sock, u8 mcpdId, u16 runId);

std::error_code mcpd_reset_daq(int sock, u8 mcpdId);
std::error_code mcpd_start_daq(int sock, u8 mcpdId);
std::error_code mcpd_stop_daq(int sock, u8 mcpdId);
std::error_code mcpd_continue_daq(int sock, u8 mcpdId);

std::error_code mcpd_get_all_parameters(int sock, u8 mcpdId, McpdParams &dest);

std::error_code mcpd_get_bus_capabilities(int sock, u8 mcpdId, BusCapabilities &caps);
std::error_code mcpd_set_bus_capabilities(int sock, u8 mcpdId, u8 capBits, u8 &resultBits);

std::error_code mcpd_set_timing_options(int sock, u8 mcpdId, TimingRole role, BusTermination term);

std::error_code mcpd_set_master_clock_value(int sock, u8 mcpdId, u64 clock);

std::error_code mcpd_setup_cell(
    int sock, u8 mcpdId,
    const CellName &cell,
    const TriggerSource &trigSource,
    u16 compareRegisterBitValue);

std::error_code mcpd_setup_auxtimer(
    int sock, u8 mcpdId,
    u16 timerId,
    u16 compareRegisterValue);

std::error_code mcpd_set_param_source(
    int sock, u8 mcpdId,
    u16 param,
    const CounterSource &source);

std::error_code mcpd_set_dac_output_values(
    int sock, u8 mcpdId,
    u16 dac0Value, u16 dac1Value);

// Scans the 8 MCPD data busses. Places a non-zero value into the dest array if
// a device is connected and responding on the corresponding bus.
std::error_code mcpd_scan_busses(
    int sock, u8 mcpdId, std::array<u16, McpdBusCount> &dest);

//
// MPSD specific
//

std::error_code mpsd_set_gain(
    int sock, u8 mcpdId,
    u8 mpsdId, u8 channel, u8 gain);

std::error_code mpsd_set_threshold(
    int sock, u8 mcpdId,
    u8 mpsdId, u8 threshold);

std::error_code mpsd_set_pulser(
    int sock, u8 mcpdId,
    u8 mpsdId, u8 channel,
    const ChannelPosition &pos,
    u8 amplitude,
    const PulserState &state);

std::error_code mpsd_set_mode(
    int sock, u8 mcpdId,
    u8 mpsdId,
    const MpsdMode &mode);

std::error_code mpsd_get_params(
    int sock, u8 mcpdId,
    u8 mpsdId,
    MpsdParameters &dest);

}
}

#endif /* __MESYTEC_MCPD_FUNCTIONS_H__ */
