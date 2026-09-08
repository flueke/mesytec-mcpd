#ifndef C7149101_E543_4822_B532_89BA1DA7D093
#define C7149101_E543_4822_B532_89BA1DA7D093

#include <array>
#include <stdexcept>
#include <string>
#include <system_error>

#include <mesytec-mcpd/mesytec-mcpd.h>

namespace mesytec::mcpd::py_lib
{

// Exception type raised for any failed MCPD/MDLL command transaction. Mirrors
// the "<what>: <message> (code=<value>, category=<category>)" error strings
// used throughout mcpd-cli.
class McpdError: public std::runtime_error
{
  public:
    McpdError(const std::string &what, const std::error_code &ec);

    const std::error_code &code() const noexcept { return ec_; }

  private:
    std::error_code ec_;
};

// Thin, exception-based, object-oriented wrapper around the free command
// functions in mcpd_functions.h/mdll_functions.h. One instance owns a single
// UDP command socket connected to one MCPD (which may carry an MDLL/MPSD/MSTD
// on one of its busses). Mirrors the verb-like commands implemented by
// mcpd-cli.
class McpdConnection
{
  public:
    McpdConnection(const std::string &address, unsigned mcpdId = 0, u16 port = McpdDefaultPort);
    ~McpdConnection();

    McpdConnection(const McpdConnection &) = delete;
    McpdConnection &operator=(const McpdConnection &) = delete;

    void close();
    bool is_open() const { return sock_ >= 0; }

    unsigned mcpd_id() const { return mcpdId_; }
    void set_mcpd_id(unsigned id) { mcpdId_ = id; }

    // Generic MCPD commands
    McpdVersionInfo get_version();
    void set_id(u8 newId);
    void set_ip_address(const std::string &address);
    void set_data_dest_port(u16 port);
    void set_ip_address_and_data_dest(
        const std::string &address, const std::string &dataDestAddress, u16 dataDestPort);
    void set_run_id(u16 runId);
    void reset_daq();
    void start_daq();
    void stop_daq();
    void continue_daq();
    McpdParams get_all_parameters();
    BusCapabilities get_bus_capabilities();
    u8 set_bus_capabilities(u8 capBits);
    void set_timing_options(TimingRole role, BusTermination term, bool extSync = false);
    void set_master_clock_value(u64 clock);
    void setup_cell(CellName cell, TriggerSource trigSource, u16 compareRegisterBitValue);
    void setup_auxtimer(u16 timerId, u16 compareRegisterValue);
    void set_param_source(u16 param, DataSource source);
    void set_dac_output_values(u16 dac0Value, u16 dac1Value);
    std::array<u16, McpdBusCount> scan_busses();
    void write_register(u16 address, u32 value);
    u32 read_register(u16 address);
    u16 read_peripheral_register(u8 mpsdId, u16 registerNumber);
    void write_peripheral_register(u8 mpsdId, u16 registerNumber, u16 registerValue);

    // MPSD specific
    void mpsd_set_gain(u8 mpsdId, u8 channel, u8 gain);
    void mpsd_set_threshold(u8 mpsdId, u8 threshold);
    void mpsd_set_pulser(
        u8 mpsdId, u8 channel, ChannelPosition pos, u8 amplitude, PulserState state);
    void mpsd_set_mode(u8 mpsdId, MpsdMode mode);
    void mpsd_set_tx_format(u8 mpsdId, u16 txFormat);
    MpsdParameters mpsd_get_params(u8 mpsdId);

    // MSTD specific
    void mstd_set_gain(u8 mstdId, u8 channel, u8 gain);

    // MDLL specific
    void mdll_set_thresholds(u8 thresholdX, u8 thresholdY, u8 thresholdAnode);
    void mdll_set_spectrum(u8 shiftX, u8 shiftY, u8 scaleX, u8 scaleY);
    void mdll_set_pulser(bool enable, u16 amplitude, MdllChannelPosition pos);
    void mdll_set_tx_data_set(MdllTxDataSet ds);
    void mdll_set_timing_window(
        unsigned tSumLimitXLow, unsigned tSumLimitXHigh, unsigned tSumLimitYLow,
        unsigned tSumLimitYHigh);
    void mdll_set_energy_window(u8 lowerThreshold, u8 upperThreshold);

  private:
    void check(const std::error_code &ec, const char *what) const;

    int sock_ = -1;
    unsigned mcpdId_ = 0;
};

// Probes mcpd_id values 0..255 against the given address until one responds
// (used for MCPD-8_v1 modules where the id is not known beforehand). Throws
// McpdError if no responding id is found or on any unexpected socket error.
unsigned find_mcpd_id(const std::string &address, u16 port = McpdDefaultPort);

} // namespace mesytec::mcpd::py_lib

#endif /* C7149101_E543_4822_B532_89BA1DA7D093 */
