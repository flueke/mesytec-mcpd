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

std::error_code command_transaction(int sock, const CommandPacket &request, CommandPacket &response);

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
    const CommandType &cmd, u8 mcpdId, const std::vector<u16> &data = {})
{
    return make_command_packet(cmd, mcpdId, data.data(), data.size());
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

std::error_code mcpd_set_run_id(int sock, u8 mcpdId, u16 runId);

std::error_code mcpd_reset(int sock, u8 mcpdId);
std::error_code mcpd_start_daq(int sock, u8 mcpdId);
std::error_code mcpd_stop_daq(int sock, u8 mcpdId);
std::error_code mcpd_continue_daq(int sock, u8 mcpdId);

std::error_code mcpd_get_all_parameters(int sock, u8 mcpdId, McpdParams &dest);

std::error_code mcpd_get_bus_capabilities(int sock, u8 mcpdId, BusCapabilities &caps);
std::error_code mcpd_set_bus_capabilities(int sock, u8 mcpdId, u8 capBits, u8 &resultBits);

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

std::error_code mcpd_send_serial_string(
    int sock, u8 mcpdId,
    const std::string &str);

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
