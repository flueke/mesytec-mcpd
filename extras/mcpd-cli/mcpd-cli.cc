#include <cstdlib>
#include <fstream>
#include <iostream>
#include <signal.h>

#include <mesytec-mcpd/mesytec-mcpd.h>
#include <lyra/lyra.hpp>
#include <spdlog/spdlog.h>
#include <sys/stat.h>

#ifdef MESYTEC_MCPD_ENABLE_ROOT
#include "mcpd_root_histos.h"
#endif

using namespace mesytec::mcpd;

namespace
{

static std::atomic<bool> g_interrupted(false);

void signal_handler(int signum)
{
    g_interrupted = true;
}

bool file_exists(const char *path)
{
    struct stat st = {};

    return (::stat(path, &st) == 0);
}

}

void setup_signal_handlers()
{
#ifndef __WIN32
    /* Set up the structure to specify the new action. */
    struct sigaction new_action;
    new_action.sa_handler = signal_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;

    for (auto signum: { SIGINT, SIGHUP, SIGTERM })
    {
        if (sigaction(signum, &new_action, NULL) != 0)
            throw std::system_error(errno, std::generic_category(), "setup_signal_handlers");
    }
#endif
}

struct CliContext
{
    std::string mcpdAddress;
    u16 mcpdPort = McpdDefaultPort;
    int mcpdId = -1;
    int cmdSock = -1;
};

struct BaseCommand
{
    bool run_ = false;
    bool offline_ = false;
    bool active() const { return run_; };
    bool offline() const { return offline_; }
    virtual int runCommand(CliContext &ctx) = 0;
    virtual ~BaseCommand() {};
};

struct SetupCommand: public BaseCommand
{
    std::string newAddress_;
    u16 newId_ = 0;
    std::string dataDestAddress_ = "0.0.0.0";
    u16 dataPort_ = McpdDefaultPort + 1u;


    SetupCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "setup",
                 [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("MCPD base setup")

            //.add_argument(lyra::help(showHelp_)) This is broken in lyra

            .add_argument(
                lyra::arg(newAddress_, "newAddress")
                .required()
                .help("new mcpd ip-address/hostame")
                )

            .add_argument(
                lyra::arg(newId_, "newId")
                .required()
                .help("new mcpd id")
                )

            .add_argument(
                lyra::arg(dataDestAddress_, "dataDestAddress")
                .optional()
                .help("new mcpd data destination ip-address/hostname")
                )

            .add_argument(
                lyra::arg(dataPort_, "dataPort")
                .optional()
                .help("mcpd data destination port")
                )
            );
    }

    int runCommand(CliContext &ctx) override
    {
        if (newAddress_.empty())
            return 1;

        spdlog::debug("{} {} {} {} {}",
                      __PRETTY_FUNCTION__, newAddress_, newId_, dataDestAddress_, dataPort_);

        auto ec = mcpd_set_id(ctx.cmdSock, ctx.mcpdId, newId_);

        if (ec)
        {
            spdlog::error("Error setting mcpdId: {} ({}, {})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        // Update context with the new mcpdId and change the ip address. Ignore
        // the error code as we might not receive a response after changing the
        // ip address.
        ctx.mcpdId = newId_;
        mcpd_set_ip_address_and_data_dest(ctx.cmdSock, ctx.mcpdId, newAddress_, dataDestAddress_, dataPort_);

        return 0;
    }
};

struct SetIdCommand: public BaseCommand
{
    u16 newId_ = 0;

    SetIdCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "setid",
                [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Set MCPD id")

            .add_argument(
                lyra::arg(newId_, "newId")
                .required()
                .help("new mcpd id")
                )
            );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{} {}", __PRETTY_FUNCTION__, newId_);

        auto ec = mcpd_set_id(ctx.cmdSock, ctx.mcpdId, newId_);

        if (ec)
        {
            spdlog::error("Error setting mcpdId: {} ({}, {})",
                          ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        ctx.mcpdId = newId_;
        return 0;
    }
};

struct TimingCommand: public BaseCommand
{
    std::string role_;
    std::string term_;

    TimingCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "timing",
                 [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Bus master/slave setup")

            .add_argument(
                lyra::arg(role_, "role")
                .required()
                .choices("master", "slave")
                .help("role=master|slave")
                )

            .add_argument(
                lyra::arg(term_, "termination")
                .required()
                .choices("on", "off")
                .help("termination=on|off")
                )
            );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{} role={} term={}", __PRETTY_FUNCTION__, role_, term_);

        if (role_.empty())
        {
            spdlog::error("timing: no bus role specified");
            return 1;
        }

        if (term_.empty())
        {
            spdlog::error("timing: no termination specified");
            return 1;
        }

        auto role = role_ == "master" ? TimingRole::Master : TimingRole::Slave;
        auto term = term_ == "on" ? BusTermination::On : BusTermination::Off;

        auto ec = mcpd_set_timing_options(ctx.cmdSock, ctx.mcpdId, role, term);

        if (ec)
        {
            spdlog::error("Error setting timing options: {} ({}, {})",
                          ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

struct RunIdCommand: public BaseCommand
{
    u16 runId_ = 0u;

    RunIdCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "runid",
                 [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Set the mcpd runId for the next DAQ run")

            .add_argument(
                lyra::arg(runId_, "runId")
                .required()
                .help("runId")
                )
            );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{} runId={}", __PRETTY_FUNCTION__, runId_);

        auto ec = mcpd_set_run_id(ctx.cmdSock, ctx.mcpdId, runId_);

        if (ec)
        {
            spdlog::error("Error setting runid: {} ({}, {})",
                          ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

struct CellCommand: public BaseCommand
{
    u16 cellId_ = 0u;
    u16 trigger_ = 0u;
    u16 compareReg_ = 0u;

    CellCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "cell",
                 [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Counter cell setup")

            .add_argument(
                lyra::arg(cellId_, "cellId")
                .required()
                .help("0-3: Monitor/Chopper1-4, 4/5: Digital Input 1/2")
                )

            .add_argument(
                lyra::arg(trigger_, "triggerValue")
                .required()
                .help("0: NoTrigger, 1-4: AuxTimer1-4, 5/6: Digital Input 1/2, 7: Compare Register")
                )

            .add_argument(
                lyra::arg(compareReg_, "compareRegister")
                .optional()
                .help("0-20: trigger if bit n=1, 21: trigger on overflow, 22: trigger on rising edge of input")
                )
            );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{}, cellId={}, trigger={}, compareReg={}",
                      __PRETTY_FUNCTION__, cellId_, trigger_, compareReg_);

        auto ec = mcpd_setup_cell(ctx.cmdSock, ctx.mcpdId,
                                  static_cast<CellName>(cellId_),
                                  static_cast<TriggerSource>(trigger_),
                                  compareReg_);

        if (ec)
        {
            spdlog::error("cell: {} ({}, {})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

struct TimerCommand: public BaseCommand
{
    u16 timerId_;
    u16 captureValue_;

    TimerCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "timer",
                 [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Timer setup")

            .add_argument(
                lyra::arg(timerId_, "timerId")
                .required()
                .help("timerId in [0, 3]")
                )

            .add_argument(
                lyra::arg(captureValue_, "captureValue")
                .required()
                .help("capture register value")
                )
            );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{}, timerId={}, captureValue={}",
                      __PRETTY_FUNCTION__, timerId_, captureValue_);

        auto ec = mcpd_setup_auxtimer(ctx.cmdSock, ctx.mcpdId, timerId_, captureValue_);

        if (ec)
        {
            spdlog::error("cell: {} ({}, {})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

struct ParamSourceCommand: public BaseCommand
{
    u16 param_ = 0u;
    u16 source_ = 0u;

    ParamSourceCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "param_source",
                 [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Set parameter source")

            .add_argument(
                lyra::arg(param_, "paramId")
                .required()
                .help("paramId")
                )

            .add_argument(
                lyra::arg(source_, "paramSource")
                .required()
                .help("0-3: Monitor0-3, 4/5: Digital Input 1/2, 6: All digital and ADC inputs, "
                      "7: event counter, 8: master clock")
                )
            );
    }

    int runCommand(CliContext &ctx)
    {
        spdlog::debug("{}, param={}, source={}",
                      __PRETTY_FUNCTION__, param_, source_);

        auto ec = mcpd_set_param_source(ctx.cmdSock, ctx.mcpdId, param_, static_cast<CounterSource>(source_));

        if (ec)
        {
            spdlog::error("param_source: {} ({}, {})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

struct GetParametersCommand: public BaseCommand
{
    GetParametersCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "get_parameters",
                 [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Read and print the current parameter values")
            );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{}", __PRETTY_FUNCTION__);

        McpdParams params = {};

        auto ec = mcpd_get_all_parameters(ctx.cmdSock, ctx.mcpdId, params);

        if (ec)
        {
            spdlog::error("get_parameters: {} ({}, {})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        spdlog::info("parameter values");
        spdlog::info("  ADC1: {}", params.adc[0]);
        spdlog::info("  ADC2: {}", params.adc[1]);
        spdlog::info("  DAC1: {}", params.dac[0]);
        spdlog::info("  DAC2: {}", params.dac[1]);
        spdlog::info("  TTL out: {}", params.ttlOut);
        spdlog::info("  TTL in: {}", params.ttlIn);

        for (size_t pi=0; pi<McpdParamCount; ++pi)
            spdlog::info("  Parameter{}: {}", pi, to_48bit_value(params.params[pi]));

        return 0;
    }
};

struct VersionCommand: public BaseCommand
{
    VersionCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "version",
                 [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Read mcpd cpu and fpga version info")
            );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{}", __PRETTY_FUNCTION__);

        McpdVersionInfo vi = {};
        auto ec = mcpd_get_version(ctx.cmdSock, ctx.mcpdId, vi);

        if (ec)
        {
            spdlog::error("version: {} ({}, {})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        spdlog::info("MCPD cpu={}.{}, fpga={}.{})", vi.cpu[0], vi.cpu[1], vi.fpga[0], vi.fpga[1]);

        return 0;
    }
};

struct DacSetupCommand: public BaseCommand
{
    u16 dac0_ = 0u;
    u16 dac1_ = 0u;

    DacSetupCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "dac_setup",
                 [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("MCPD DAC unit setup")

            .add_argument(
                lyra::arg(dac0_, "dac0")
                .required()
                .help("dac0 value (12 bit)")
                )

            .add_argument(
                lyra::arg(dac1_, "dac1")
                .required()
                .help("dac1 value (12 bit)")
                )
            );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{} dac0={}, dac1={}", __PRETTY_FUNCTION__, dac0_, dac1_);

        auto ec = mcpd_set_dac_output_values(ctx.cmdSock, ctx.mcpdId, dac0_, dac1_);

        if (ec)
        {
            spdlog::error("dac_setup: {} ({}, {})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

struct ScanBussesCommand: public BaseCommand
{
    ScanBussesCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "scan_busses",
                 [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Scan MCPD busses for connected MPSD modules")

            );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{}", __PRETTY_FUNCTION__);

        std::array<u16, McpdBusCount> scanDest = {};
        auto ec = mcpd_scan_busses(ctx.cmdSock, ctx.mcpdId, scanDest);

        if (ec)
        {
            spdlog::error("scan_busses: {} ({}, {})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        spdlog::info("scan_busses result:");

        for (size_t bus=0; bus<scanDest.size(); ++bus)
            spdlog::info("  [{}]: {}", bus, scanDest[bus]);

        return 0;
    }
};

namespace
{

// Helper function using std::stoul() with base=0 to allow parsing decimal, hex
// and octal values.
template<typename T>
lyra::parser_result parse_unsigned_value(T &dest, const std::string &input)
{
    try
    {
        dest = std::stoul(input, nullptr, 0);
        return lyra::parser_result::ok(lyra::parser_result_type::matched);
    }
    catch(const std::exception& e)
    {
        auto msg = fmt::format("Error parsing unsigned value from \"{}\"", input);
        return lyra::parser_result::error(lyra::parser_result_type::short_circuit_all, msg);
    }
}

}

struct WriteRegisterCommand: public BaseCommand
{
    u16 address_;
    u32 value_;

    WriteRegisterCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "write_register",
                 [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("write MCPD/MDLL internal register (modern versions only)")

            .add_argument(
                lyra::arg([&] (const std::string &str) { return parse_unsigned_value(address_, str); }, "address")
                .required()
                )

            .add_argument(
                lyra::arg([&] (const std::string &str) { return parse_unsigned_value(value_, str); }, "value")
                .required()
                )
            );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{}: address=0x{:04X}, value=0x{:08X}",
                      __PRETTY_FUNCTION__, address_, value_);

        auto ec = mcpd_write_register(ctx.cmdSock, ctx.mcpdId, address_, value_);

        if (ec)
        {
            spdlog::error("mcpd_write_register: {} ({}, {})",
                          ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

struct ReadRegisterCommand: public BaseCommand
{
    u16 address_;

    ReadRegisterCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "read_register",
                 [this] (const lyra::group &) { this->run_ = true; }
                 )
            .help("read MCPD/MDLL internal register (modern versions only)")

            .add_argument(
                lyra::arg([&] (const std::string &str) { return parse_unsigned_value(address_, str); }, "address")
                .required()
                )
            );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{}: address=0x{:04X}", __PRETTY_FUNCTION__, address_);

        u32 dest{};
        auto ec = mcpd_read_register(ctx.cmdSock, ctx.mcpdId, address_, dest);

        if (ec)
        {
            spdlog::error("mcpd_read_register: {} ({}, {})",
                          ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        spdlog::info("mcdp_read_register: 0x{:04X} = 0x{:08X}", address_, dest);

        return 0;
    }
};

struct MpsdSetGainCommand: public BaseCommand
{
    u16 mpsdId_ = 0u;
    unsigned channel_ = 0u;
    unsigned gain_ = 0u;

    MpsdSetGainCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "mpsd_set_gain",
                 [this] (const lyra::group &) { this->run_ = true; }
                 )
            .help("set per-channel mpsd gain")

            .add_argument(
                lyra::arg(mpsdId_, "mpsdid")
                .required()
                .help("mpsd id")
                )

            .add_argument(
                lyra::arg(channel_, "channel")
                .required()
                .help("mpsd channel")
                )

            .add_argument(
                lyra::arg(gain_, "gain")
                .required()
                .help("gain value")
                )
            );
    }

    int runCommand(CliContext &ctx)
    {
        spdlog::debug("{} mpsdId={}, channel={}, gain={}",
                      __PRETTY_FUNCTION__, mpsdId_, channel_, gain_);

        auto ec = mpsd_set_gain(ctx.cmdSock, ctx.mcpdId, mpsdId_, channel_, gain_);

        if (ec)
        {
            spdlog::error("version: {} ({}, {})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

struct MpsdSetTresholdCommand: public BaseCommand
{
    u16 mpsdId_ = 0u;
    unsigned threshold_ = 0u;

    MpsdSetTresholdCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "mpsd_set_threshold",
                 [this] (const lyra::group &) { this->run_ = true; }
                 )
            .help("set mpsd threshold")

            .add_argument(
                lyra::arg(mpsdId_, "mpsdid")
                .required()
                .help("mpsd id")
                )

            .add_argument(
                lyra::arg(threshold_, "treshold")
                .required()
                .help("threshold value")
                )
            );
    }

    int runCommand(CliContext &ctx)
    {
        spdlog::debug("{} mpsdId={}, threshold={}",
                      __PRETTY_FUNCTION__, mpsdId_, threshold_);

        auto ec = mpsd_set_threshold(ctx.cmdSock, ctx.mcpdId, mpsdId_, threshold_);

        if (ec)
        {
            spdlog::error("version: {} ({}, {})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

struct MpsdSetPulserCommand: public BaseCommand
{
    u16 mpsdId_ = 0u;
    unsigned channel_ = 0u;
    unsigned pos_ = 0u;
    unsigned amplitude_ = 0u;
    std::string state_ = "off";

    MpsdSetPulserCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "mpsd_set_pulser",
                 [this] (const lyra::group &) { this->run_ = true; }
                 )
            .help("set per-channel mpsd pulser settings")

            .add_argument(
                lyra::arg(mpsdId_, "mpsdId")
                .required()
                .help("mpsd id")
                )

            .add_argument(
                lyra::arg(channel_, "channel")
                .required()
                .help("mpsd channel")
                )

            .add_argument(
                lyra::arg(pos_, "position")
                .required()
                .help("0: left, 1: right, 2: middle")
                )

            .add_argument(
                lyra::arg(amplitude_, "amplitude")
                .required()
                .help("pulser amplitude")
                )

            .add_argument(
                lyra::arg(state_, "state")
                .required()
                .choices("on", "off")
                .help("pulser state, on|off")
                )
            );
    }

    int runCommand(CliContext &ctx)
    {
        spdlog::debug("{} mpsdId={} channel={}, position={}, amplitude={}, state={} ",
                      __PRETTY_FUNCTION__, mpsdId_, channel_, pos_, amplitude_, state_);

        auto state = state_ == "on" ? PulserState::On : PulserState::Off;

        auto ec = mpsd_set_pulser(
            ctx.cmdSock, ctx.mcpdId,
            mpsdId_, channel_, static_cast<ChannelPosition>(pos_), amplitude_, state);

        if (ec)
        {
            spdlog::error("version: {} ({}, {})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

struct MpsdGetParametersCommand: public BaseCommand
{
    u16 mpsdId_ = 0u;

    MpsdGetParametersCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "mpsd_get_parameters",
                 [this] (const lyra::group &) { this->run_ = true; }
                 )
            .help("set per-channel mpsd gain")

            .add_argument(
                lyra::arg(mpsdId_, "mpsdid")
                .required()
                .help("mpsd id")
                )
            );
    }

    int runCommand(CliContext &ctx)
    {
        spdlog::debug("{} mpsdId={}", __PRETTY_FUNCTION__, mpsdId_);

        MpsdParameters params = {};

        auto ec = mpsd_get_params(ctx.cmdSock, ctx.mcpdId, mpsdId_, params);

        if (ec)
        {
            spdlog::error("version: {} ({}, {})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        spdlog::info("MPSD{} parameters:", params.mpsdId);
        spdlog::info("  busTxCapabilities={}", params.busTxCaps);
        spdlog::info("  fastTxFormat={}", params.fastTxFormat);
        spdlog::info("  firmwareRevision={}", params.firmwareRevision);

        return 0;
    }
};


struct DaqCommand: public BaseCommand
{
    std::string subCommand_;

    DaqCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "daq",
                [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("DAQ control commands")

            .add_argument(
                lyra::arg(subCommand_, "command")
                .required()
                .choices("start", "stop", "continue", "reset")
                .help("start|stop|continue|reset")
                )
            );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{} {}", __PRETTY_FUNCTION__, subCommand_);

        std::error_code ec;

        if (subCommand_ == "start")
            ec = mcpd_start_daq(ctx.cmdSock, ctx.mcpdId);
        else if (subCommand_ == "stop")
            ec = mcpd_stop_daq(ctx.cmdSock, ctx.mcpdId);
        else if (subCommand_ == "continue")
            ec = mcpd_continue_daq(ctx.cmdSock, ctx.mcpdId);
        else if (subCommand_ == "reset")
            ec = mcpd_reset_daq(ctx.cmdSock, ctx.mcpdId);

        if (ec)
        {
            spdlog::error("daq {}: {} ({}, {})", subCommand_, ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

struct ReadoutCounters
{
    size_t packets = 0u;
    size_t bytes = 0u;
    size_t timeouts = 0u;
    size_t events = 0u;
};

void report_counters(const ReadoutCounters &counters, const std::string &title = "readout")
{
    spdlog::info("{}: packets={}, bytes={}, timeouts={}, events={}",
                 title, counters.packets, counters.bytes, counters.timeouts,
                 counters.events
                );
}


struct ReadoutCommand: public BaseCommand
{
    u16 dataPort_ = McpdDefaultPort + 1;
    std::string listfilePath_;
    bool noListfile_ = false;
    size_t duration_s_ = 0u;
    size_t reportInterval_ms_ = 1000u;
    bool printPacketSummary_ = false;
    bool printEventData_ = false;

#ifdef MESYTEC_MCPD_ENABLE_ROOT
    RootHistoContext rootHistoContext_ = {};
    std::string rootHistoPath_;
#endif

    ReadoutCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "readout",
                [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("DAQ readout to listfile")

            .add_argument(
                lyra::opt(listfilePath_, "listfilePath")
                ["--listfile"]
                .optional()
                .help("Path to the output listfile")
                )

            .add_argument(
                lyra::opt([this] (const bool &b) { noListfile_ = b; })
                ["--no-listfile"]
                .optional()
                .help("Do not write an output listfile.")
                )

            .add_argument(
                lyra::opt(duration_s_, "duration [s]")
                ["--duration"]
                .optional()
                .help("DAQ run duration in seconds. Runs forever if not specified or 0.")
                )

            .add_argument(
                lyra::opt(dataPort_, "dataPort")
                ["--dataport"]
                .optional()
                .help("mcpd data port (also the local listening port)")
                )

            .add_argument(
                lyra::opt(reportInterval_ms_, "interval [ms]")
                ["--report-interval"]
                .optional()
                .help("Time in ms between logging readout stats")
                )

            .add_argument(
                lyra::opt([this] (const bool &b) { printPacketSummary_ = b; })
                ["--print-packet-summary"]
                .optional()
                .help("Print readout packet summaries")
                )

            .add_argument(
                lyra::opt([this] (const bool &b) { printEventData_ = b; })
                ["--print-event-data"]
                .optional()
                .help("Print readout event data")
                )

#ifdef MESYTEC_MCPD_ENABLE_ROOT
            .add_argument(
                lyra::opt(rootHistoPath_, "rootfile")
                ["--root-histo-file"]
                .optional()
                .help("ROOT histo output file path")
                )
#endif
            );
    }

    int runCommand(CliContext &ctx) override
    {
        if (listfilePath_.empty() && !noListfile_)
        {
            spdlog::error("readout: no listfile name given (use --no-listfile to ignore)");
            return 1;
        }

        spdlog::debug("{} {} {}", __PRETTY_FUNCTION__, dataPort_, listfilePath_);

        std::error_code ec;
        // Creates an unconnected UDP socket listening on the dataPort.
        int dataSock = bind_udp_socket(dataPort_, &ec);

        if (ec)
        {
            spdlog::error("readout: error listening on data port {}: {} ({}, {})",
                          dataPort_, ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        {
            u16 localPort = get_local_socket_port(dataSock);
            spdlog::info("readout: listening for data on port {}", localPort);
        }

        std::ofstream listfile;
        listfile.exceptions(std::ios::failbit | std::ios::badbit);

        if (!noListfile_)
        {
            if (file_exists(listfilePath_.c_str()))
            {
                spdlog::error("readout: Output listfile '{}' already exists",
                              listfilePath_);
                return 1;
            }

            try
            {
                listfile.open(listfilePath_, std::ios_base::out | std::ios::binary);
            }
            catch (const std::exception &e)
            {
                spdlog::error("readout: Error opening listfile '{}': {}",
                              listfilePath_, e.what());
                return 1;
            }
        }

#ifdef MESYTEC_MCPD_ENABLE_ROOT
        if (!rootHistoPath_.empty())
        {
            try
            {
                rootHistoContext_ = create_histo_context(rootHistoPath_);
                spdlog::info("Writing ROOT histograms to {}", rootHistoPath_);
            }
            catch (const std::runtime_error &e)
            {
                spdlog::error("{}", e.what());
                return 1;
            }
        }
#endif

        ReadoutCounters counters = {};
        DataPacket dataPacket = {};

        spdlog::info("readout: entering readout loop");

        auto tStart = std::chrono::steady_clock::now();
        auto tReport = tStart;

        while (!g_interrupted)
        {
            size_t bytesTransferred = 0u;

            auto ec = receive_one_packet(
                dataSock,
                reinterpret_cast<u8 *>(&dataPacket), sizeof(dataPacket),
                bytesTransferred, DefaultReadTimeout_ms);

            if (ec)
            {
                if (ec == std::errc::interrupted)
                    break;

                if (ec != SocketErrorType::Timeout)
                {
                    spdlog::error("readout: error reading from network: {} ({}, {})",
                                  ec.message(), ec.value(), ec.category().name());
                    return 1;
                }
                else
                    ++counters.timeouts;
            }

            if (bytesTransferred)
            {
                if (!noListfile_)
                {
                    try
                    {
                        listfile.write(reinterpret_cast<const char *>(&dataPacket), sizeof(dataPacket));
                    }
                    catch (const std::exception &e)
                    {
                        spdlog::error("readout: Error writing to listfile '{}': {}",
                                      listfilePath_, e.what());
                        return 1;
                    }
                }

                const auto eventCount = get_event_count(dataPacket);

                if (printPacketSummary_)
                {
                    spdlog::info(
                        "packet#{}: bufferType=0x{:04x}, bufferNumber={}, runId={}, "
                        "devStatus={}, devId={}, timestamp={}",
                        counters.packets, dataPacket.bufferType, dataPacket.bufferNumber,
                        dataPacket.runId, dataPacket.deviceStatus, dataPacket.deviceId,
                        get_header_timestamp(dataPacket));

                    spdlog::info("  parameters: 0x{:012x}, {}, {}, {}",
                                 to_48bit_value(dataPacket.param[0]),
                                 to_48bit_value(dataPacket.param[1]),
                                 to_48bit_value(dataPacket.param[2]),
                                 to_48bit_value(dataPacket.param[3])
                                 );

                    spdlog::info("  packet contains {} events", eventCount);
                }

                if (printEventData_)
                {
                    for(size_t ei=0; ei<eventCount; ++ei)
                    {
                        auto event = decode_event(dataPacket, ei);
                        spdlog::info("{}", to_string(event));
                    }
                }

#ifdef MESYTEC_MCPD_ENABLE_ROOT
            if (rootHistoContext_.histoOutFile)
                root_histos_process_packet(rootHistoContext_, dataPacket);
#endif

                ++counters.packets;
                counters.bytes += bytesTransferred;
                counters.events += eventCount;
            }

            const auto now = std::chrono::steady_clock::now();

            if (duration_s_ > 0)
            {
                auto elapsed = now - tStart;

                if (elapsed >= std::chrono::seconds(duration_s_))
                {
                    spdlog::info("readout: runDuration reached, leaving readout loop");
                    break;
                }
            }

            if (reportInterval_ms_ > 0)
            {
                auto elapsed = now - tReport;

                if (elapsed >= std::chrono::milliseconds(reportInterval_ms_))
                {
                    report_counters(counters);
                    tReport = now;
                }
            }
        }

        report_counters(counters);

        return 0;
    }
};

struct ReplayCommand: public BaseCommand
{
    std::string listfilePath_;
    size_t reportInterval_ms_ = 1000u;
    bool printPacketSummary_ = false;
    bool printEventData_ = false;

#ifdef MESYTEC_MCPD_ENABLE_ROOT
    RootHistoContext rootHistoContext_ = {};
    std::string rootHistoPath_;
#endif

    ReplayCommand(lyra::cli &cli)
    {
        offline_ = true;

        cli.add_argument(
            lyra::command(
                "replay",
                [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("DAQ replay from listfile")

            .add_argument(
                lyra::opt(listfilePath_, "listfilePath")
                ["--listfile"]
                .required()
                .help("Path to the input listfile")
                )

            .add_argument(
                lyra::opt(reportInterval_ms_, "interval [ms]")
                ["--report-interval"]
                .optional()
                .help("Time in ms between logging readout stats")
                )

            .add_argument(
                lyra::opt([this] (const bool &b) { printPacketSummary_ = b; })
                ["--print-packet-summary"]
                .optional()
                .help("Print readout packet summaries")
                )

            .add_argument(
                lyra::opt([this] (const bool &b) { printEventData_ = b; })
                ["--print-event-data"]
                .optional()
                .help("Print readout event data")
                )

#ifdef MESYTEC_MCPD_ENABLE_ROOT
            .add_argument(
                lyra::opt(rootHistoPath_, "rootfile")
                ["--root-histo-file"]
                .optional()
                .help("ROOT histo output file path")
                )
#endif

            );
    }

    int runCommand(CliContext &ctx) override
    {
        if (listfilePath_.empty())
        {
            spdlog::error("replay: no input listfile specified");
            return 1;
        }

        spdlog::debug("{} {}", __PRETTY_FUNCTION__, listfilePath_);

        std::ifstream listfile;
        listfile.exceptions(std::ios::badbit);

        try
        {
            listfile.open(listfilePath_, std::ios::in | std::ios::binary);
        }
        catch (const std::exception &e)
        {
            spdlog::error("replay: Error opening listfile '{}': {}",
                          listfilePath_, e.what());
            return 1;
        }

#ifdef MESYTEC_MCPD_ENABLE_ROOT
        if (!rootHistoPath_.empty())
        {
            try
            {
                rootHistoContext_ = create_histo_context(rootHistoPath_);
                spdlog::info("Writing ROOT histograms to {}", rootHistoPath_);
            }
            catch (const std::runtime_error &e)
            {
                spdlog::error("{}", e.what());
                return 1;
            }
        }
#endif

        ReadoutCounters counters = {};
        DataPacket dataPacket = {};

        spdlog::info("Replaying from {}", listfilePath_);

        auto tStart = std::chrono::steady_clock::now();
        auto tReport = tStart;

        while (!listfile.eof() && !g_interrupted)
        {
            try
            {
                listfile.read(reinterpret_cast<char *>(&dataPacket), sizeof(dataPacket));

                if (listfile.eof())
                    break;
            }
            catch (const std::exception &e)
            {
                spdlog::error("replay: Error reading from listfile '{}': {}",
                              listfilePath_, e.what());
                return 1;
            }

            const auto eventCount = get_event_count(dataPacket);

            if (printPacketSummary_)
            {
                spdlog::info(
                    "packet#{}: bufferType=0x{:04x}, bufferNumber={}, runId={}, "
                    "devStatus={}, devId={}, timestamp={}",
                    counters.packets, dataPacket.bufferType, dataPacket.bufferNumber,
                    dataPacket.runId, dataPacket.deviceStatus, dataPacket.deviceId,
                    get_header_timestamp(dataPacket));

                spdlog::info("  parameters: {}, {}, {}, {}",
                             to_48bit_value(dataPacket.param[0]),
                             to_48bit_value(dataPacket.param[1]),
                             to_48bit_value(dataPacket.param[2]),
                             to_48bit_value(dataPacket.param[3])
                            );

                spdlog::info("  packet contains {} events", eventCount);
            }

            if (printEventData_)
            {
                for(size_t ei=0; ei<eventCount; ++ei)
                {
                    auto event = decode_event(dataPacket, ei);
                    spdlog::info("{}", to_string(event));
                }
            }

#ifdef MESYTEC_MCPD_ENABLE_ROOT
            if (rootHistoContext_.histoOutFile)
                root_histos_process_packet(rootHistoContext_, dataPacket);
#endif

            ++counters.packets;
            counters.bytes += sizeof(dataPacket);
            counters.events += eventCount;

            const auto now = std::chrono::steady_clock::now();

            if (reportInterval_ms_ > 0)
            {
                auto elapsed = now - tReport;

                if (elapsed >= std::chrono::milliseconds(reportInterval_ms_))
                {
                    report_counters(counters, "replay");
                    tReport = now;
                }
            }
        }

        report_counters(counters, "replay");

        return 0;
    }
};

struct CustomCommand: public BaseCommand
{
    u16 commandId_;
    std::vector<std::string> commandData_;

    CustomCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "custom",
                [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Send a custom command to the MCPD")

            .add_argument(
                lyra::arg(commandId_, "commandId")
                .required()
                .help("The command id to send to the MCPD (CommandPacket::id)")
                )

            .add_argument(
                lyra::arg(commandData_, "commandData")
                .cardinality(0, CommandPacketMaxDataWords)
                .help("Custom uint16_t data to send with the command (CommandPacket::data)")
                )

            );
    }

    int runCommand(CliContext &ctx)
    {
        spdlog::debug("{}: cmdId={}, cmdData=[{}]",
                      __PRETTY_FUNCTION__, commandId_, fmt::join(commandData_, ", "));

        std::vector<u16> data;

        for (const auto &dataStr: commandData_)
        {
            try
            {
                auto parsedValue = std::stoul(dataStr);

                if (parsedValue > std::numeric_limits<u16>::max())
                {
                    throw std::out_of_range(fmt::format(
                            "data value '{}' is out of the uint16_t range", dataStr));
                }

                data.push_back(parsedValue);
            }
            catch (const std::exception &e)
            {
                spdlog::error("Error parsing data value '{}': {}",
                              dataStr, e.what());
                return 1;
            }
        }

        assert(data.size() <= CommandPacketMaxDataWords);

        auto request = make_command_packet(commandId_, ctx.mcpdId, data);
        CommandPacket response = {};

        spdlog::info("Sending custom command packet: {}", to_string(request));

        auto ec = command_transaction(ctx.cmdSock, request, response);

        if (ec)
        {
            spdlog::error("custom: {} ({}, {})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        spdlog::info("Received response: {}", to_string(response));

        return 0;
    }
};

struct MdllSetThresholds: public BaseCommand
{
    u16 thresholdX_;
    u16 thresholdY_;
    u16 thresholdAnode_;

    MdllSetThresholds(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "mdll_set_thresholds",
                [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Set MDLL thresholds")

            .add_argument(
                lyra::arg(thresholdX_, "thresholdX")
                .required()
                )

            .add_argument(
                lyra::arg(thresholdY_, "thresholdY")
                .required()
                )

            .add_argument(
                lyra::arg(thresholdAnode_, "thresholdAnode")
                .required()
                )
            );
    }

    int runCommand(CliContext &ctx)
    {
        spdlog::debug("{}: thresholdX={}, thresholdY={}, thresholdAnode={} ",
                      __PRETTY_FUNCTION__, thresholdX_, thresholdY_, thresholdAnode_);

        auto ec = mdll_set_thresholds(
            ctx.cmdSock, thresholdX_, thresholdY_, thresholdAnode_);

        if (ec)
        {
            spdlog::error("mdll_set_thresholds: {} ({}, {})",
                          ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

struct MdllSetSpectrum: public BaseCommand
{
    u16 shiftX_;
    u16 shiftY_;
    u16 scaleX_;
    u16 scaleY_;

    MdllSetSpectrum(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "mdll_set_spectrum",
                [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Set MDLL spectrum")

            .add_argument(
                lyra::arg(shiftX_, "shiftX")
                .required()
                )

            .add_argument(
                lyra::arg(shiftY_, "shiftY")
                .required()
                )

            .add_argument(
                lyra::arg(scaleX_, "scaleX")
                .required()
                )

            .add_argument(
                lyra::arg(scaleY_, "scaleY")
                .required()
                )
            );
    }

    int runCommand(CliContext &ctx)
    {
        spdlog::debug("{}: shiftX={}, shiftY={}, scaleX={}, scaleY={} ",
                      __PRETTY_FUNCTION__, shiftX_, shiftY_, scaleX_, scaleY_);

        auto ec = mdll_set_spectrum(
            ctx.cmdSock, shiftX_, shiftY_, scaleX_, scaleY_);

        if (ec)
        {
            spdlog::error("mdll_set_spectrum: {} ({}, {})",
                          ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

struct MdllSetPulser: public BaseCommand
{
    bool enable_;
    u16 amplitude_;
    u16 position_;

    MdllSetPulser(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "mdll_set_pulser",
                [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Set MDLL pulser")

            .add_argument(
                lyra::arg(enable_, "enable")
                .required()
                )

            .add_argument(
                lyra::arg(amplitude_, "amplitude")
                .required()
                .help("amplitude: 0-3")
                )

            .add_argument(
                lyra::arg(position_, "position")
                .required()
                .help("0: lower-left, 1: middle, 2: upper-right")
                )

            );
    }

    int runCommand(CliContext &ctx)
    {
        spdlog::debug("{}: enable={}, amplitude={}, position={} ",
                      __PRETTY_FUNCTION__, enable_, amplitude_, position_);

        auto ec = mdll_set_pulser(
            ctx.cmdSock, enable_, amplitude_,
            static_cast<MdllChannelPosition>(position_));

        if (ec)
        {
            spdlog::error("mdll_set_pulser: {} ({}, {})",
                          ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};


struct MdllSetTxDataSet: public BaseCommand
{
    u16 ds_;

    MdllSetTxDataSet(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "mdll_set_tx_data_set",
                [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Set MDLL TX data set")

            .add_argument(
                lyra::arg(ds_, "dataset")
                .required()
                .help("0: Default, 1: Timings")
                )
            );
    }

    int runCommand(CliContext &ctx)
    {
        spdlog::debug("{}: ds={} ", __PRETTY_FUNCTION__, ds_);

        auto ec = mdll_set_tx_data_set(
            ctx.cmdSock, static_cast<MdllTxDataSet>(ds_));

        if (ec)
        {
            spdlog::error("mdll_set_tx_data_set: {} ({}, {})",
                          ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

struct MdllSetTimingWindow: public BaseCommand
{
    unsigned tSumLimitXLow_;
    unsigned tSumLimitXHigh_;
    unsigned tSumLimitYLow_;
    unsigned tSumLimitYHigh_;

    MdllSetTimingWindow(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "mdll_set_timing_window",
                [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Set MDLL timing window")

            .add_argument(
                lyra::arg(tSumLimitXLow_, "X low")
                .required()
                )

            .add_argument(
                lyra::arg(tSumLimitXHigh_, "X high")
                .required()
                )

            .add_argument(
                lyra::arg(tSumLimitYLow_, "Y low")
                .required()
                )

            .add_argument(
                lyra::arg(tSumLimitYHigh_, "Y high")
                .required()
                )
            );
    }

    int runCommand(CliContext &ctx)
    {
        spdlog::debug("{}: xLow={}, xHigh={}, yLow={}, yHigh={} ",
                      __PRETTY_FUNCTION__,
                      tSumLimitXLow_, tSumLimitXHigh_,
                      tSumLimitYLow_, tSumLimitYHigh_);

        auto ec = mdll_set_timing_window(
            ctx.cmdSock,
            tSumLimitXLow_,
            tSumLimitXHigh_,
            tSumLimitYLow_,
            tSumLimitYHigh_);

        if (ec)
        {
            spdlog::error("mdll_set_timing_window: {} ({}, {})",
                          ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

struct MdllSetEnergyWindow: public BaseCommand
{
    u16 lowerThreshold_;
    u16 upperThreshold_;

    MdllSetEnergyWindow(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "mdll_set_energy_window",
                [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Set MDLL energy window")

            .add_argument(
                lyra::arg(lowerThreshold_, "lower threshold")
                .required()
                )

            .add_argument(
                lyra::arg(upperThreshold_, "upper threshold")
                .required()
                )

            );
    }

    int runCommand(CliContext &ctx)
    {
        spdlog::debug("{}: lowerThreshold={}, upperThreshold={} ",
                      __PRETTY_FUNCTION__, lowerThreshold_, upperThreshold_);

        auto ec = mdll_set_energy_window(
            ctx.cmdSock,
            lowerThreshold_,
            upperThreshold_);

        if (ec)
        {
            spdlog::error("mdll_set_energy_window: {} ({}, {})",
                          ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::info);

    setup_signal_handlers();

    bool showHelp = false;
    CliContext ctx = {};
    bool logDebug = false;
    bool logTrace = false;
    bool showVersion = false;

    auto cli = (
        lyra::help(showHelp)

        | lyra::opt(ctx.mcpdAddress, "mcpdAddress")
        ["--address"]("mcpd ip-address/hostname")
        .optional()

        | lyra::opt(ctx.mcpdId, "mcpdId")
        ["--id"]("mcpd id")
        .optional()

        | lyra::opt(ctx.mcpdPort, "port")
        ["--port"]("mcpd command port")
        .optional()

        | lyra::opt([&] (bool b) { logDebug = b; })
        ["--debug"]("set log level to debug")
        .optional()

        | lyra::opt([&] (bool b) { logTrace = b; })
        ["--trace"]("set log level to trace")
        .optional()

        | lyra::opt([&] (bool b) { showVersion = b; })
        ["--version"]("show mcpd-cli version info")
        .optional()

        );

    std::vector<std::unique_ptr<BaseCommand>> commands;

    commands.emplace_back(std::make_unique<SetupCommand>(cli));
    commands.emplace_back(std::make_unique<SetIdCommand>(cli));
    commands.emplace_back(std::make_unique<TimingCommand>(cli));
    commands.emplace_back(std::make_unique<RunIdCommand>(cli));
    commands.emplace_back(std::make_unique<CellCommand>(cli));
    commands.emplace_back(std::make_unique<TimerCommand>(cli));
    commands.emplace_back(std::make_unique<ParamSourceCommand>(cli));
    commands.emplace_back(std::make_unique<GetParametersCommand>(cli));
    commands.emplace_back(std::make_unique<VersionCommand>(cli));
    commands.emplace_back(std::make_unique<DacSetupCommand>(cli));
    commands.emplace_back(std::make_unique<ScanBussesCommand>(cli));

    commands.emplace_back(std::make_unique<WriteRegisterCommand>(cli));
    commands.emplace_back(std::make_unique<ReadRegisterCommand>(cli));

    commands.emplace_back(std::make_unique<MpsdSetGainCommand>(cli));
    commands.emplace_back(std::make_unique<MpsdSetTresholdCommand>(cli));
    commands.emplace_back(std::make_unique<MpsdSetPulserCommand>(cli));
    commands.emplace_back(std::make_unique<MpsdGetParametersCommand>(cli));

    commands.emplace_back(std::make_unique<DaqCommand>(cli));
    commands.emplace_back(std::make_unique<ReadoutCommand>(cli));
    commands.emplace_back(std::make_unique<ReplayCommand>(cli));

    commands.emplace_back(std::make_unique<MdllSetThresholds>(cli));
    commands.emplace_back(std::make_unique<MdllSetSpectrum>(cli));
    commands.emplace_back(std::make_unique<MdllSetTxDataSet>(cli));
    commands.emplace_back(std::make_unique<MdllSetTimingWindow>(cli));
    commands.emplace_back(std::make_unique<MdllSetEnergyWindow>(cli));
    commands.emplace_back(std::make_unique<MdllSetPulser>(cli));

    commands.emplace_back(std::make_unique<CustomCommand>(cli));

    auto parsed = cli.parse({argc, argv});

    if (!parsed)
    {
        std::cerr << std::endl << cli << std::endl;
        spdlog::error("Error parsing command line: {}", parsed.message());
        return 1;
    }

    if (logDebug)
        spdlog::set_level(spdlog::level::debug);

    if (logTrace)
        spdlog::set_level(spdlog::level::trace);

    if (showVersion)
    {
        std::cout << fmt::format("mcpd-cli {}\nCopyright (c) 2021 mesytec GmbH & Co. KG\nLicense: Boost Software License - Version 1.0 - August 17th, 2003",
                                 library_version()) << std::endl;
        return 0;
    }

    // hack around the lyra subgroup parsing issue with --help
    for (int arg=1; arg<argc; ++arg)
    {
        std::string s = argv[arg];
        if (s == "-h" || s == "--help" || s == "-?")
        {
            showHelp = true;
            break;
        }
    }

    if (showHelp)
    {
        std::cout << cli << std::endl;
        std::cout << "MCPD address and id can also be specified via the environment variables MCPD_ADDRESS and MCPD_ID." << std::endl;
        return 0;
    }

    // Use mcpd ip address/host, mcpd id and the command port from the
    // environment if not specified on the command line.
    if (ctx.mcpdAddress.empty())
    {
        if (char *envAddr = std::getenv("MCPD_ADDRESS"))
            ctx.mcpdAddress = envAddr;
    }

    if (ctx.mcpdAddress.empty())
        ctx.mcpdAddress = McpdDefaultAddress;

    if (ctx.mcpdId < 0)
    {
        if (char *envId = std::getenv("MCPD_ID"))
            ctx.mcpdId = std::atoi(envId);
    }

    if (ctx.mcpdId < 0)
        ctx.mcpdId = 0;

    // Find the active command.
    auto activeCommand = std::find_if(
        std::begin(commands), std::end(commands),
        [] (const auto &cmd) { return cmd->active(); });

    if (activeCommand == std::end(commands))
    {
        std::cerr << cli << std::endl;
        spdlog::error("No command specified");
        return 1;
    }

    // Connect to the mcpd

    std::error_code ec;

    if (!(*activeCommand)->offline())
    {
        spdlog::info("Connecting to mcpd @ {}:{}, mcpdId={} ...",
                     ctx.mcpdAddress, ctx.mcpdPort, ctx.mcpdId);

        ctx.cmdSock = connect_udp_socket(ctx.mcpdAddress, ctx.mcpdPort, &ec);

        if (ec)
        {
            spdlog::error("Error connecting to mcpd@{}:{}: {}",
                          ctx.mcpdAddress, ctx.mcpdPort, ec.message());
            return 1;
        }

#if 0 // FIXME: temporarily disabled until the MCPD+MDLL can handle the GetVersion command
        {
            McpdVersionInfo vi = {};
            ec = mcpd_get_version(ctx.cmdSock, ctx.mcpdId, vi);

            if (ec)
            {
                spdlog::error("Error reading mcpd version: {} ({}, {})",
                              ec.message(), ec.value(), ec.category().name());
                return 1;
            }

            spdlog::info("Connected to mcpd @ {}:{} mcpdId={} (cpu={}.{}, fpga={}.{})",
                         ctx.mcpdAddress, ctx.mcpdPort, ctx.mcpdId,
                         vi.cpu[0], vi.cpu[1], vi.fpga[0], vi.fpga[1]);
        }
#endif
    }


    return (*activeCommand)->runCommand(ctx);
}
