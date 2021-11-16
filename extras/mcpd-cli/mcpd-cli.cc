#include <cstdlib>
#include <fstream>
#include <iostream>
#include <signal.h>

#include <mesytec-mcpd/mesytec-mcpd.h>
#include <lyra/lyra.hpp>
#include <spdlog/spdlog.h>

using namespace mesytec::mcpd;

static std::atomic<bool> g_interrupted(false);

void signal_handler(int signum)
{
    g_interrupted = true;
}

void setup_signal_handlers()
{
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
}

struct CliContext
{
    std::string mcpdAddress;
    u16 mcpdPort = 0;
    int mcpdId = -1;
    int cmdSock = -1;
};

struct BaseCommand
{
    bool run_ = false;
    bool showHelp_ = false;
    bool active() const { return run_; };
    virtual int runCommand(CliContext &ctx) = 0;
    virtual ~BaseCommand() {};
};

struct SetupCommand: public BaseCommand
{
    bool showHelp_ = false;
    std::string newAddress_;
    u16 newId_ = 0;
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

        spdlog::debug("{} {} {} {}", __PRETTY_FUNCTION__, newAddress_, newId_, dataPort_);

        auto ec = mcpd_set_id(ctx.cmdSock, ctx.mcpdId, newId_);

        if (ec)
        {
            spdlog::error("Error setting mcpdId: {} ({}, {})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        // Update context and change the ip address. Ignore the error code as
        // we might not receive a response after changing the ip address.

        ctx.mcpdId = newId_;
        mcpd_set_ip_address_and_data_dest_port(ctx.cmdSock, ctx.mcpdId, newAddress_, dataPort_);

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
                lyra::arg(compareReg_, "compare register value")
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
            spdlog::error("version: {} ({}, {})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

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

struct ReadoutCommand: public BaseCommand
{
    u16 dataPort_ = McpdDefaultPort + 1;
    std::string listfilePath_;
    size_t duration_s_ = 0u;
    size_t reportInterval_ms_ = 1000u;
    bool printData_ = false;

    ReadoutCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "readout",
                [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("DAQ readout to listfile")

            .add_argument(
                lyra::opt(dataPort_, "dataPort")
                ["--dataport"]
                .optional()
                .help("mcpd data port (also the local listening port)")
                )

            .add_argument(
                lyra::opt(duration_s_, "duration [s]")
                ["--duration"]
                .optional()
                .help("DAQ run duration in seconds")
                )

            .add_argument(
                lyra::opt(reportInterval_ms_, "interval [ms]")
                ["--report-interval"]
                .optional()
                .help("Time in ms between logging readout stats")
                )

            .add_argument(
                lyra::arg(listfilePath_, "listfilePath")
                .required()
                .help("Path to the output listfile")
                )

            .add_argument(
                lyra::opt([this] (const bool &b) { printData_ = b; })
                ["--print-data"]
                .optional()
                .help("Print readout data")
                )

            );
    }

    int runCommand(CliContext &ctx) override
    {
        if (listfilePath_.empty())
        {
            spdlog::error("readout: no output listfile name specified");
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

        struct Counters
        {
            size_t packets = 0u;
            size_t bytes = 0u;
            size_t timeouts = 0u;
            size_t events = 0u;
        };

        auto report_counters = [] (const Counters &counters)
        {
            spdlog::info("readout: packets={}, bytes={}, timeouts={}, events={}",
                         counters.packets, counters.bytes, counters.timeouts,
                         counters.events
                         );
        };

        Counters counters = {};
        std::ofstream listfile(listfilePath_);
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

            if (g_interrupted)
                break;

            if (ec)
            {
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
                listfile.write(reinterpret_cast<const char *>(&dataPacket), sizeof(dataPacket));

                if (listfile.bad() || listfile.fail())
                {
                    // FIXME: proper error message here
                    spdlog::error("readout: error writing to listfile {}: {}",
                                  listfilePath_, "FIXME");
                    return 1;
                }

                const auto eventCount = get_event_count(dataPacket);

                if (printData_)
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

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::info);

    setup_signal_handlers();

    bool showHelp = false;
    CliContext ctx = {};
    bool logDebug = false;
    bool logTrace = false;

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

        );

    std::vector<std::unique_ptr<BaseCommand>> commands;

    commands.emplace_back(std::make_unique<SetupCommand>(cli));
    commands.emplace_back(std::make_unique<TimingCommand>(cli));
    commands.emplace_back(std::make_unique<RunIdCommand>(cli));
    commands.emplace_back(std::make_unique<CellCommand>(cli));
    commands.emplace_back(std::make_unique<ParamSourceCommand>(cli));
    commands.emplace_back(std::make_unique<GetParametersCommand>(cli));
    commands.emplace_back(std::make_unique<VersionCommand>(cli));
    commands.emplace_back(std::make_unique<DacSetupCommand>(cli));

    commands.emplace_back(std::make_unique<MpsdSetGainCommand>(cli));
    commands.emplace_back(std::make_unique<MpsdSetTresholdCommand>(cli));
    commands.emplace_back(std::make_unique<MpsdSetPulserCommand>(cli));
    commands.emplace_back(std::make_unique<MpsdGetParametersCommand>(cli));

    commands.emplace_back(std::make_unique<DaqCommand>(cli));
    commands.emplace_back(std::make_unique<ReadoutCommand>(cli));

    auto parsed = cli.parse({argc, argv});

    if (!parsed)
    {
        spdlog::error("Error parsing command line: {}", parsed.errorMessage());
        std::cerr << std::endl << cli << std::endl;
        return 1;
    }

    if (logDebug)
        spdlog::set_level(spdlog::level::debug);

    if (logTrace)
        spdlog::set_level(spdlog::level::trace);

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
        std::cerr << cli << std::endl;
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

    if (ctx.mcpdPort == 0)
    {
        if (char *envPort = std::getenv("MCPD_PORT"))
            ctx.mcpdPort = std::atoi(envPort);
    }

    if (ctx.mcpdPort == 0)
        ctx.mcpdPort = McpdDefaultPort;


    // Connect to the mcpd

    std::error_code ec;

    ctx.cmdSock = connect_udp_socket(ctx.mcpdAddress, ctx.mcpdPort, &ec);

    if (ec)
    {
        spdlog::error("Error connecting to mcpd@{}:{}: {}",
                      ctx.mcpdAddress, ctx.mcpdPort, ec.message());
        return 1;
    }

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

    // Find the active command and run it.
    auto active = std::find_if(
        std::begin(commands), std::end(commands),
        [] (const auto &cmd) { return cmd->active(); });

    if (active != std::end(commands))
    {
        //spdlog::info("mcpdAddress={}, mcpdId={}", ctx.mcpdAddress, ctx.mcpdId);
        return (*active)->runCommand(ctx);
    }

    std::cerr << cli << std::endl;
    return 1;
}
