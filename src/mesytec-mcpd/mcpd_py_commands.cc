#include "mcpd_py_commands.h"

#include <spdlog/spdlog.h> // for fmt::

namespace mesytec::mcpd::py_lib
{

McpdError::McpdError(const std::string &what, const std::error_code &ec)
    : std::runtime_error(fmt::format(
          "{}: {} (code={}, category={})", what, ec.message(), ec.value(), ec.category().name()))
    , ec_(ec)
{
}

McpdConnection::McpdConnection(const std::string &address, unsigned mcpdId, u16 port)
    : mcpdId_(mcpdId)
{
    std::error_code ec;
    sock_ = connect_udp_socket(address, port, &ec);
    if (ec)
        throw McpdError(fmt::format("connecting to mcpd@{}:{}", address, port), ec);
}

McpdConnection::~McpdConnection() { close(); }

void McpdConnection::close()
{
    if (sock_ >= 0)
    {
        close_socket(sock_);
        sock_ = -1;
    }
}

void McpdConnection::check(const std::error_code &ec, const char *what) const
{
    if (ec)
        throw McpdError(what, ec);
}

McpdVersionInfo McpdConnection::get_version()
{
    McpdVersionInfo vi = {};
    check(mcpd_get_version(sock_, static_cast<u8>(mcpdId_), vi), "get_version");
    return vi;
}

void McpdConnection::set_id(u8 newId)
{
    check(mcpd_set_id(sock_, static_cast<u8>(mcpdId_), newId), "set_id");
    mcpdId_ = newId;
}

void McpdConnection::set_ip_address(const std::string &address)
{
    check(mcpd_set_ip_address(sock_, static_cast<u8>(mcpdId_), address), "set_ip_address");
}

void McpdConnection::set_data_dest_port(u16 port)
{
    check(mcpd_set_data_dest_port(sock_, static_cast<u8>(mcpdId_), port), "set_data_dest_port");
}

void McpdConnection::set_ip_address_and_data_dest(
    const std::string &address, const std::string &dataDestAddress, u16 dataDestPort)
{
    auto ec = mcpd_set_ip_address_and_data_dest(
        sock_, static_cast<u8>(mcpdId_), address, dataDestAddress, dataDestPort);

    // Note: changing the mcpd ip address means we will not receive a response
    // to this particular request, matching mcpd-cli's 'setup' command.
    if (ec && ec != std::errc::timed_out)
        throw McpdError("set_ip_address_and_data_dest", ec);
}

void McpdConnection::set_run_id(u16 runId)
{
    check(mcpd_set_run_id(sock_, static_cast<u8>(mcpdId_), runId), "set_run_id");
}

void McpdConnection::reset_daq() { check(mcpd_reset_daq(sock_, static_cast<u8>(mcpdId_)), "reset_daq"); }
void McpdConnection::start_daq() { check(mcpd_start_daq(sock_, static_cast<u8>(mcpdId_)), "start_daq"); }
void McpdConnection::stop_daq() { check(mcpd_stop_daq(sock_, static_cast<u8>(mcpdId_)), "stop_daq"); }
void McpdConnection::continue_daq()
{
    check(mcpd_continue_daq(sock_, static_cast<u8>(mcpdId_)), "continue_daq");
}

McpdParams McpdConnection::get_all_parameters()
{
    McpdParams params = {};
    check(mcpd_get_all_parameters(sock_, static_cast<u8>(mcpdId_), params), "get_all_parameters");
    return params;
}

BusCapabilities McpdConnection::get_bus_capabilities()
{
    BusCapabilities caps = {};
    check(mcpd_get_bus_capabilities(sock_, static_cast<u8>(mcpdId_), caps), "get_bus_capabilities");
    return caps;
}

u8 McpdConnection::set_bus_capabilities(u8 capBits)
{
    u8 result = {};
    check(
        mcpd_set_bus_capabilities(sock_, static_cast<u8>(mcpdId_), capBits, result),
        "set_bus_capabilities");
    return result;
}

void McpdConnection::set_timing_options(TimingRole role, BusTermination term, bool extSync)
{
    check(
        mcpd_set_timing_options(sock_, static_cast<u8>(mcpdId_), role, term, extSync),
        "set_timing_options");
}

void McpdConnection::set_master_clock_value(u64 clock)
{
    check(
        mcpd_set_master_clock_value(sock_, static_cast<u8>(mcpdId_), clock),
        "set_master_clock_value");
}

void McpdConnection::setup_cell(CellName cell, TriggerSource trigSource, u16 compareRegisterBitValue)
{
    check(
        mcpd_setup_cell(sock_, static_cast<u8>(mcpdId_), cell, trigSource, compareRegisterBitValue),
        "setup_cell");
}

void McpdConnection::setup_auxtimer(u16 timerId, u16 compareRegisterValue)
{
    check(
        mcpd_setup_auxtimer(sock_, static_cast<u8>(mcpdId_), timerId, compareRegisterValue),
        "setup_auxtimer");
}

void McpdConnection::set_param_source(u16 param, DataSource source)
{
    check(
        mcpd_set_param_source(sock_, static_cast<u8>(mcpdId_), param, source), "set_param_source");
}

void McpdConnection::set_dac_output_values(u16 dac0Value, u16 dac1Value)
{
    check(
        mcpd_set_dac_output_values(sock_, static_cast<u8>(mcpdId_), dac0Value, dac1Value),
        "set_dac_output_values");
}

std::array<u16, McpdBusCount> McpdConnection::scan_busses()
{
    std::array<u16, McpdBusCount> dest = {};
    check(mcpd_scan_busses(sock_, static_cast<u8>(mcpdId_), dest), "scan_busses");
    return dest;
}

void McpdConnection::write_register(u16 address, u32 value)
{
    check(mcpd_write_register(sock_, static_cast<u8>(mcpdId_), address, value), "write_register");
}

u32 McpdConnection::read_register(u16 address)
{
    u32 dest = {};
    check(mcpd_read_register(sock_, static_cast<u8>(mcpdId_), address, dest), "read_register");
    return dest;
}

u16 McpdConnection::read_peripheral_register(u8 mpsdId, u16 registerNumber)
{
    u16 dest = {};
    check(
        mesytec::mcpd::read_peripheral_register(
            sock_, static_cast<u8>(mcpdId_), mpsdId, registerNumber, dest),
        "read_peripheral_register");
    return dest;
}

void McpdConnection::write_peripheral_register(u8 mpsdId, u16 registerNumber, u16 registerValue)
{
    check(
        mesytec::mcpd::write_peripheral_register(
            sock_, static_cast<u8>(mcpdId_), mpsdId, registerNumber, registerValue),
        "write_peripheral_register");
}

void McpdConnection::mpsd_set_gain(u8 mpsdId, u8 channel, u8 gain)
{
    check(
        mesytec::mcpd::mpsd_set_gain(sock_, static_cast<u8>(mcpdId_), mpsdId, channel, gain),
        "mpsd_set_gain");
}

void McpdConnection::mpsd_set_threshold(u8 mpsdId, u8 threshold)
{
    check(
        mesytec::mcpd::mpsd_set_threshold(sock_, static_cast<u8>(mcpdId_), mpsdId, threshold),
        "mpsd_set_threshold");
}

void McpdConnection::mpsd_set_pulser(
    u8 mpsdId, u8 channel, ChannelPosition pos, u8 amplitude, PulserState state)
{
    check(
        mesytec::mcpd::mpsd_set_pulser(
            sock_, static_cast<u8>(mcpdId_), mpsdId, channel, pos, amplitude, state),
        "mpsd_set_pulser");
}

void McpdConnection::mpsd_set_mode(u8 mpsdId, MpsdMode mode)
{
    check(
        mesytec::mcpd::mpsd_set_mode(sock_, static_cast<u8>(mcpdId_), mpsdId, mode),
        "mpsd_set_mode");
}

void McpdConnection::mpsd_set_tx_format(u8 mpsdId, u16 txFormat)
{
    check(
        mesytec::mcpd::mpsd_set_tx_format(sock_, static_cast<u8>(mcpdId_), mpsdId, txFormat),
        "mpsd_set_tx_format");
}

MpsdParameters McpdConnection::mpsd_get_params(u8 mpsdId)
{
    MpsdParameters params = {};
    check(
        mesytec::mcpd::mpsd_get_params(sock_, static_cast<u8>(mcpdId_), mpsdId, params),
        "mpsd_get_params");
    return params;
}

void McpdConnection::mstd_set_gain(u8 mstdId, u8 channel, u8 gain)
{
    check(
        mesytec::mcpd::mstd_set_gain(sock_, static_cast<u8>(mcpdId_), mstdId, channel, gain),
        "mstd_set_gain");
}

void McpdConnection::mdll_set_thresholds(u8 thresholdX, u8 thresholdY, u8 thresholdAnode)
{
    check(
        mesytec::mcpd::mdll_set_thresholds(
            sock_, static_cast<u8>(mcpdId_), thresholdX, thresholdY, thresholdAnode),
        "mdll_set_thresholds");
}

void McpdConnection::mdll_set_spectrum(u8 shiftX, u8 shiftY, u8 scaleX, u8 scaleY)
{
    check(
        mesytec::mcpd::mdll_set_spectrum(
            sock_, static_cast<u8>(mcpdId_), shiftX, shiftY, scaleX, scaleY),
        "mdll_set_spectrum");
}

void McpdConnection::mdll_set_pulser(bool enable, u16 amplitude, MdllChannelPosition pos)
{
    check(
        mesytec::mcpd::mdll_set_pulser(sock_, static_cast<u8>(mcpdId_), enable, amplitude, pos),
        "mdll_set_pulser");
}

void McpdConnection::mdll_set_tx_data_set(MdllTxDataSet ds)
{
    check(
        mesytec::mcpd::mdll_set_tx_data_set(sock_, static_cast<u8>(mcpdId_), ds),
        "mdll_set_tx_data_set");
}

void McpdConnection::mdll_set_timing_window(
    unsigned tSumLimitXLow, unsigned tSumLimitXHigh, unsigned tSumLimitYLow,
    unsigned tSumLimitYHigh)
{
    check(
        mesytec::mcpd::mdll_set_timing_window(
            sock_, static_cast<u8>(mcpdId_), tSumLimitXLow, tSumLimitXHigh, tSumLimitYLow,
            tSumLimitYHigh),
        "mdll_set_timing_window");
}

void McpdConnection::mdll_set_energy_window(u8 lowerThreshold, u8 upperThreshold)
{
    check(
        mesytec::mcpd::mdll_set_energy_window(
            sock_, static_cast<u8>(mcpdId_), lowerThreshold, upperThreshold),
        "mdll_set_energy_window");
}

unsigned find_mcpd_id(const std::string &address, u16 port)
{
    std::error_code ec;
    int sock = connect_udp_socket(address, port, &ec);
    if (ec)
        throw McpdError(fmt::format("connecting to mcpd@{}:{}", address, port), ec);

    struct SocketGuard
    {
        int sock;
        ~SocketGuard() { close_socket(sock); }
    } guard{sock};

    for (unsigned id = 0; id <= 255; ++id)
    {
        McpdVersionInfo vi = {};
        ec = mcpd_get_version(sock, static_cast<u8>(id), vi);

        if (!ec)
            return id;

        if (ec != make_error_code(CommandError::IdMismatch))
            throw McpdError("find_mcpd_id", ec);
    }

    throw std::runtime_error(
        fmt::format("find_mcpd_id: no responding mcpd_id found on {}:{}", address, port));
}

} // namespace mesytec::mcpd::py_lib
