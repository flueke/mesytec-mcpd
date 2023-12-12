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

#ifdef __WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

using namespace mesytec::mcpd;

namespace
{

static std::atomic<bool> g_interrupted(false);

void signal_handler(int signum)
{
    spdlog::trace("signal_handler invoked with signal={}", signum);
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
    u16 dataPort_ = McpdDefaultPort;


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
                .help("new mcpd ip-address (0.0.0.0 to keep current setting)")
                )

            .add_argument(
                lyra::arg(newId_, "newId")
                .required()
                .help("new mcpd id")
                )

            .add_argument(
                lyra::arg(dataDestAddress_, "[dataDestAddress]")
                .optional()
                .help("new mcpd data destination ip-address (0.0.0.0 to use this computers address)")
                )

            .add_argument(
                lyra::arg(dataPort_, "[dataPort]")
                .optional()
                .help("mcpd data destination port (default=54321)")
                )
            );
    }

    int runCommand(CliContext &ctx) override
    {
        if (newAddress_.empty())
            return 1;

        spdlog::debug("{} {} {} {} {}",
                      __PRETTY_FUNCTION__, newAddress_, newId_, dataDestAddress_, dataPort_);

        // Note: setting the new mcpd id is not part of the SetProtoParams
        // command. It was added here purely for convenience to have a single
        // 'setup' command handling all settings for the MCPD_8-v1.
        auto ec = mcpd_set_id(ctx.cmdSock, ctx.mcpdId, newId_);

        if (ec)
        {
            spdlog::error("Error setting mcpd id: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        // Update context with the new mcpdId and change the ip address.
        // Note: we might not receive a response if the mcpd ip address is
        // changed by this call.
        ctx.mcpdId = newId_;
        ec = mcpd_set_ip_address_and_data_dest(ctx.cmdSock, ctx.mcpdId, newAddress_, dataDestAddress_, dataPort_);

        if (ec && ec != std::errc::timed_out)
        {
            spdlog::error("Error from setup command: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

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
            spdlog::error("Error setting mcpdId: {} (code={}, category={})",
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
    std::string extSync_;

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
                .choices("master", "slave", "1", "0")
                .help("role=master|slave|1|0")
                )

            .add_argument(
                lyra::arg(term_, "termination")
                .required()
                .choices("on", "off", "1", "0")
                .help("termination=on|off|1|0")
                )

            .add_argument(
                lyra::arg(extSync_, "[external sync]")
                .optional()
                .choices("on", "off", "1", "0")
                .help("extSync=on|off|1|0 (default=off)")
                )
            );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{} role={} term={} extSync={}", __PRETTY_FUNCTION__, role_, term_, extSync_);

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

        TimingRole role{};

        if (role_ == "master" || role_ == "1")
            role = TimingRole::Master;
        else
            role = TimingRole::Slave;

        BusTermination term{};

        if (term_ == "on" || term_ == "1")
            term = BusTermination::On;
        else
            term = BusTermination::Off;

        bool extSync = false;

        if (extSync_ == "on" || extSync_ == "1")
            extSync = true;

        auto ec = mcpd_set_timing_options(ctx.cmdSock, ctx.mcpdId, role, term, extSync);

        if (ec)
        {
            spdlog::error("Error setting timing options: {} (code={}, category={})",
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
            spdlog::error("Error setting runid: {} (code={}, category={})",
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
                .help("0: NoTrigger, 1-4: AuxTimer0-3, 5/6: Digital Input 1/2, 7: Compare Register")
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
            spdlog::error("cell: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
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
            spdlog::error("cell: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

struct SetMasterClockCommand: public BaseCommand
{
    u64 clockValue_;

    SetMasterClockCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "set_master_clock",
                [this] (const lyra::group &) { this->run_ = true; }
            )
            .help("Set master clock value")

            .add_argument(
                lyra::arg(clockValue_, "clockValue")
                .required()
                .help("clock value (48 bit unsigned)")
            )
        );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{}, clockValue={}", __PRETTY_FUNCTION__, clockValue_);

        auto ec = mcpd_set_master_clock_value(ctx.cmdSock, ctx.mcpdId, clockValue_);

        if (ec)
        {
            spdlog::error("set_master_clock: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
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

        auto ec = mcpd_set_param_source(ctx.cmdSock, ctx.mcpdId, param_, static_cast<DataSource>(source_));

        if (ec)
        {
            spdlog::error("param_source: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
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
            spdlog::error("get_parameters: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
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
            spdlog::error("version: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        spdlog::info("MCPD cpu={}.{}, fpga={}.{})", vi.cpu[0], vi.cpu[1], vi.fpga[0], vi.fpga[1]);

        return 0;
    }
};

struct McpdFindIdCommand: public BaseCommand
{
    McpdFindIdCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "find_id",
                 [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Find the 'id' value of MCPD-8_v1 (older) modules.")
            );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{}", __PRETTY_FUNCTION__);

        for (u8 id=0; id<=std::numeric_limits<u8>::max(); ++id)
        {
            McpdVersionInfo vi = {};
            auto ec = mcpd_get_version(ctx.cmdSock, id, vi);

            if (!ec)
            {
                if (vi.cpu[0] >= 10)
                    spdlog::warn("Detected MCPD-8_v2 which mirrors the given id value!");
                spdlog::info("Found mcpd_id={}", id);
                return 0;
            }

            if (ec != make_error_code(CommandError::IdMismatch))
            {
                spdlog::error("find_id: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
                return 1;
            }
        }

        spdlog::error("Unknown error while finding the mcpd_id value");
        return 1;
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
            spdlog::error("dac_setup: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
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
            spdlog::error("scan_busses: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        spdlog::info("scan_busses result:");

        for (size_t bus=0; bus<scanDest.size(); ++bus)
            spdlog::info("  [{}]: {}", bus, scanDest[bus]);

        return 0;
    }
};

struct GetBusCapabilitiesCommand: public BaseCommand
{
    GetBusCapabilitiesCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "get_bus_capabilities",
                 [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Get MCPD bus transmit capabilities")
            );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{}", __PRETTY_FUNCTION__);

        BusCapabilities caps = {};
        auto ec = mcpd_get_bus_capabilities(ctx.cmdSock, ctx.mcpdId, caps);

        if (ec)
        {
            spdlog::error("mcpd_get_bus_capabilities: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        spdlog::info("mcpd_get_bus_capabilities: available=\"{}\" (0x{:02X}), current=\"{}\" (0x{:02X})",
            bus_capabilities_to_string(caps.available), caps.available,
            bus_capabilities_to_string(caps.selected), caps.selected);

        return 0;
    }
};

struct SetBusCapabilitiesCommand: public BaseCommand
{
    u16 capsValue_;

    SetBusCapabilitiesCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "set_bus_capabilities",
                 [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("Set MCPD bus transmit capabilities")

            .add_argument(
                lyra::arg(capsValue_, "value")
                .required()
                .help("new bus transmit capabilities value")
                )
            );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{} capsValue={} (\"{}\")", __PRETTY_FUNCTION__,
            capsValue_, bus_capabilities_to_string(capsValue_));

        u8 result = {};

        auto ec = mcpd_set_bus_capabilities(ctx.cmdSock, ctx.mcpdId, capsValue_, result);

        if (ec)
        {
            spdlog::error("mcpd_set_bus_capabilities: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        spdlog::info("mcpd_set_bus_capabilities: wanted={} ({}), got={} ({})",
            capsValue_, bus_capabilities_to_string(capsValue_),
            result, bus_capabilities_to_string(result));

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
            spdlog::error("mcpd_write_register: {} (code={}, category={})",
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
            spdlog::error("mcpd_read_register: {} (code={}, category={})",
                          ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        spdlog::info("mcdp_read_register: 0x{0:04X} = 0x{1:08X} ({1} decimal)", address_, dest);

        return 0;
    }
};

struct ReadPeripheralRegisterCommand: public BaseCommand
{
    u16 mpsdId_ = 0;
    u16 registerNumber_ = 0;

    ReadPeripheralRegisterCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "read_peripheral_register",
                [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("read peripheral module (MCPD/MSTD) register")

            .add_argument(
                lyra::arg(mpsdId_, "mpsdId")
                .required()
                .help("MPSD/MSTD ID (bus number)")
            )

            .add_argument(
                lyra::arg(registerNumber_, "registerNumber")
                .required()
                .help("register to read")
            )
        );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{}, mpsdId={}, registerNumber={}", __PRETTY_FUNCTION__, mpsdId_, registerNumber_);

        u16 dest{};

        auto ec = read_peripheral_register(ctx.cmdSock, ctx.mcpdId, mpsdId_, registerNumber_, dest);

        if (ec)
        {
            spdlog::error("Error reading peripheral register: {} (code={}, category={})",
                ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        spdlog::info("read_peripheral_register: mpsdId={0:}, register={1:}, value=0x{2:04X} ({2:} decimal)",
            mpsdId_, registerNumber_, dest);

        return 0;
    }
};

struct WritePeripheralRegisterCommand: public BaseCommand
{
    u16 mpsdId_ = 0;
    u16 registerNumber_ = 0;
    u16 registerValue_ = 0;

    WritePeripheralRegisterCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "write_peripheral_register",
                [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("write peripheral module (MPSD/MSTD) register")

            .add_argument(
                lyra::arg(mpsdId_, "mpsdId")
                .required()
                .help("MPSD/MSTD ID (bus number)")
            )

            .add_argument(
                lyra::arg(registerNumber_, "registerNumber")
                .required()
                .help("register to write")
            )

            .add_argument(
                lyra::arg(registerValue_, "registerValue")
                .required()
                .help("value to write")
            )
        );
    }

    int runCommand(CliContext &ctx) override
    {
        spdlog::debug("{}, mpsdId={}, registerNumber={}, registerValue={}",
            __PRETTY_FUNCTION__, mpsdId_, registerNumber_, registerValue_);

        auto ec = write_peripheral_register(ctx.cmdSock, ctx.mcpdId, mpsdId_, registerNumber_, registerValue_);

        if (ec)
        {
            spdlog::error("Error writing peripheral register: {} (code={}, category={})",
                ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        spdlog::info("write_peripheral_register: mpsdId={0:}, register={1:}, value=0x{2:04X} ({2:} decimal)",
            mpsdId_, registerNumber_, registerValue_);

        return 0;
    }
};

struct MpsdSetMode: public BaseCommand
{
    u16 mpsdId_ = 0u;
    std::string mode_ = "position";

    MpsdSetMode(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "mpsd_set_mode",
                [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("set mpsd mode")

            .add_argument(
                lyra::arg(mpsdId_, "mpsdid")
                .required()
                .help("mpsd id")
                )

            .add_argument(
                lyra::arg(mode_, "mode")
                .required()
                .choices("0", "pos", "position", "1", "amp", "amplitude")
                .help("mode: 0|pos|position|1|amp|amplitude")
                )
            );
    }

    int runCommand(CliContext &ctx)
    {
        spdlog::debug("{} mpsdId={}, mode={}", __PRETTY_FUNCTION__, mpsdId_, mode_);

        MpsdMode mode{};

        if (mode_ == "0" || mode_ == "pos" || mode_ == "position")
            mode = MpsdMode::Position;

        if (mode_ == "1" || mode_ == "amp" || mode_ == "amplitude")
            mode = MpsdMode::Amplitude;

        auto ec = mpsd_set_mode(ctx.cmdSock, ctx.mcpdId, mpsdId_, mode);

        if (ec)
        {
            spdlog::error("mpsd_set_mode: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        return 0;
    }
};

struct MpsdSetTxFormat: public BaseCommand
{
    u16 mpsdId_ = 0u;
    unsigned txFormat_ = 0u;

    MpsdSetTxFormat(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "mpsd_set_tx_format",
                [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("set mpsd bus tx format")

            .add_argument(
                lyra::arg(mpsdId_, "mpsdid")
                .required()
                .help("mpsd id")
                )

            .add_argument(
                lyra::arg(txFormat_, "txFormat")
                .required()
                .help("bus transmit format (1|2|4)")
                )
            );
    }

    int runCommand(CliContext &ctx)
    {
        spdlog::debug("{} mpsdId={}, txFormat={}", __PRETTY_FUNCTION__, mpsdId_, txFormat_);

        auto ec = mpsd_set_tx_format(ctx.cmdSock, ctx.mcpdId, mpsdId_, txFormat_);

        if (ec)
        {
            spdlog::error("mpsd_set_tx_format: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
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
            spdlog::error("mpsd_set_gain: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
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
            spdlog::error("mpsd_set_threshold: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
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
            spdlog::error("mpsd_set_pulser: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
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
            .help("get mpsd parameters")

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
            spdlog::error("mpsd_get_parameters: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

        spdlog::info("MPSD{} parameters:", params.mpsdId);
        spdlog::info("  busTxCapabilities={}", params.busTxCaps);
        spdlog::info("  txFormat={}", params.txFormat);
        spdlog::info("  firmwareRevision={:#06x}", params.firmwareRevision);

        return 0;
    }
};

struct MstdSetGainCommand: public BaseCommand
{
    u16 mstdId_ = 0u;
    u16 channel_ = 0u;
    u16 gain_ = 0u;

    MstdSetGainCommand(lyra::cli &cli)
    {
        cli.add_argument(
            lyra::command(
                "mstd_set_gain",
                [this] (const lyra::group &) { this->run_ = true; }
                )
            .help("set per-channel mstd gain")

            .add_argument(
                lyra::arg(mstdId_, "mstdId")
                .required()
                .help("mstd id")
                )

            .add_argument(
                lyra::arg(channel_, "channel")
                .required()
                .help("channel within mstd (0..15, 16=all channels)")
                )

            .add_argument(
                lyra::arg(gain_, "gain")
                .required()
                .help("gain value (0..255)")
                )
            );
    }

    int runCommand(CliContext &ctx)
    {
        spdlog::debug("{} mstdId_={}, channel={}, gain={}",
                      __PRETTY_FUNCTION__, mstdId_, channel_, gain_);

        auto ec = mstd_set_gain(ctx.cmdSock, ctx.mcpdId, mstdId_, channel_, gain_);

        if (ec)
        {
            spdlog::error("mstd_set_gain: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
            return 1;
        }

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
            spdlog::error("daq {}: {} (code={}, category={})", subCommand_, ec.message(), ec.value(), ec.category().name());
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
    u16 dataPort_ = McpdDefaultPort;
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
            spdlog::error("readout: error listening on data port {}: {} (code={}, category={})",
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

        spdlog::info("readout: entering readout loop, press ctrl-c to quit");

        auto tStart = std::chrono::steady_clock::now();
        auto tReport = tStart;

        while (!g_interrupted)
        {
            size_t bytesTransferred = 0u;
            sockaddr_in srcAddr = {};

            auto ec = receive_one_packet(
                dataSock,
                reinterpret_cast<u8 *>(&dataPacket), sizeof(dataPacket),
                bytesTransferred, DefaultReadTimeout_ms, &srcAddr);

            if (ec)
            {
                if (ec == std::errc::interrupted)
                {
                    spdlog::trace("readout: interrupted while reading from network: {}",
                                  ec.message());
                    continue;
                }

                if (ec != SocketErrorType::Timeout)
                {
                    spdlog::error("readout: error reading from network: {} (code={}, category={})",
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
                    char srcAddrBuf[16];

                    inet_ntop(AF_INET, &srcAddr.sin_addr, srcAddrBuf, sizeof(srcAddrBuf));

                    spdlog::info(
                        "packet#{}: bufferType=0x{:04x}, bufferNumber={}, runId={}, "
                        "devStatus=0x{:04x}, deviceId={}, timestamp={}, srcAddr={}",
                        counters.packets, dataPacket.bufferType, dataPacket.bufferNumber,
                        dataPacket.runId, dataPacket.deviceStatus, dataPacket.deviceId,
                        get_header_timestamp(dataPacket), srcAddrBuf);

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
                        u64 rawevent = get_event(dataPacket, ei);
                        spdlog::info("{} (raw_value={:#x})", to_string(event), rawevent);
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
                    "devStatus={}, deviceId={}, timestamp={}",
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
            spdlog::error("custom: {} (code={}, category={})", ec.message(), ec.value(), ec.category().name());
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
            spdlog::error("mdll_set_thresholds: {} (code={}, category={})",
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
            spdlog::error("mdll_set_spectrum: {} (code={}, category={})",
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
            spdlog::error("mdll_set_pulser: {} (code={}, category={})",
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
            spdlog::error("mdll_set_tx_data_set: {} (code={}, category={})",
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
            spdlog::error("mdll_set_timing_window: {} (code={}, category={})",
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
            spdlog::error("mdll_set_energy_window: {} (code={}, category={})",
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

    // MCPD/MDLL core commands
    commands.emplace_back(std::make_unique<VersionCommand>(cli));
    commands.emplace_back(std::make_unique<McpdFindIdCommand>(cli));
    commands.emplace_back(std::make_unique<SetupCommand>(cli));
    commands.emplace_back(std::make_unique<SetIdCommand>(cli));
    commands.emplace_back(std::make_unique<TimingCommand>(cli));
    commands.emplace_back(std::make_unique<RunIdCommand>(cli));
    commands.emplace_back(std::make_unique<CellCommand>(cli));
    commands.emplace_back(std::make_unique<TimerCommand>(cli));
    commands.emplace_back(std::make_unique<SetMasterClockCommand>(cli));
    commands.emplace_back(std::make_unique<ParamSourceCommand>(cli));
    commands.emplace_back(std::make_unique<GetParametersCommand>(cli));
    commands.emplace_back(std::make_unique<DacSetupCommand>(cli));
    commands.emplace_back(std::make_unique<ScanBussesCommand>(cli));
    commands.emplace_back(std::make_unique<GetBusCapabilitiesCommand>(cli));
    commands.emplace_back(std::make_unique<SetBusCapabilitiesCommand>(cli));

    commands.emplace_back(std::make_unique<ReadPeripheralRegisterCommand>(cli));
    commands.emplace_back(std::make_unique<WritePeripheralRegisterCommand>(cli));

    // extension for the modern FPGA based MCPD/MDLL versions
    commands.emplace_back(std::make_unique<WriteRegisterCommand>(cli));
    commands.emplace_back(std::make_unique<ReadRegisterCommand>(cli));

    // MPSD
    commands.emplace_back(std::make_unique<MpsdSetTxFormat>(cli));
    commands.emplace_back(std::make_unique<MpsdSetMode>(cli));
    commands.emplace_back(std::make_unique<MpsdSetGainCommand>(cli));
    commands.emplace_back(std::make_unique<MpsdSetTresholdCommand>(cli));
    commands.emplace_back(std::make_unique<MpsdSetPulserCommand>(cli));
    commands.emplace_back(std::make_unique<MpsdGetParametersCommand>(cli));

    // MSTD
    commands.emplace_back(std::make_unique<MstdSetGainCommand>(cli));

    // MDLL
    commands.emplace_back(std::make_unique<MdllSetThresholds>(cli));
    commands.emplace_back(std::make_unique<MdllSetSpectrum>(cli));
    commands.emplace_back(std::make_unique<MdllSetTxDataSet>(cli));
    commands.emplace_back(std::make_unique<MdllSetTimingWindow>(cli));
    commands.emplace_back(std::make_unique<MdllSetEnergyWindow>(cli));
    commands.emplace_back(std::make_unique<MdllSetPulser>(cli));

    // Non-device specific commands (DAQ control, readout, replay, ...)
    commands.emplace_back(std::make_unique<CustomCommand>(cli));
    commands.emplace_back(std::make_unique<DaqCommand>(cli));
    commands.emplace_back(std::make_unique<ReadoutCommand>(cli));
    commands.emplace_back(std::make_unique<ReplayCommand>(cli));

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
        std::cout << fmt::format("mcpd-cli {}\nCopyright (c) 2021-23 mesytec GmbH & Co. KG\nLicense: Boost Software License - Version 1.0 - August 17th, 2003",
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
        spdlog::debug("Connecting to mcpd @ {}:{}, mcpdId={} ...",
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
                spdlog::error("Error reading mcpd version: {} (code={}, category={})",
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
