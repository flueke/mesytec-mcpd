#include <fstream>
#include <iostream>

#include <mesytec-mcpd/mesytec-mcpd.h>
#include <lyra/lyra.hpp>
#include <spdlog/spdlog.h>

using namespace mesytec::mcpd;

struct CliContext
{
    std::string mcpdAddress = McpdDefaultAddress;
    u16 mcpdPort = McpdDefaultPort;
    u16 mcpdId = 0;
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

static const bool g_interrupted = false;

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
        spdlog::debug("{} {} {}", __PRETTY_FUNCTION__, role_, term_);

        auto role = role_ == "master" ? TimingRole::Master : TimingRole::Slave;
        auto term = term_ == "on" ? BusTermination::On : BusTermination::Off;

        auto ec = mcpd_set_timing_options(ctx.cmdSock, ctx.mcpdId, role, term);

        if (ec)
        {
            spdlog::error("Error setting timing options: {} ({}, {})", ec.message(), ec.value(), ec.category().name());
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
                lyra::arg(listfilePath_, "listfilePath")
                .required()
                .help("Path to the output listfile")
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

        std::ofstream listfile(listfilePath_);
        DataPacket dataPacket = {};

        while (!g_interrupted) // TODO: signal handling
        {
            size_t bytesTransferred = 0u;

            auto ec = receive_one_packet(
                dataSock,
                reinterpret_cast<u8 *>(&dataPacket), sizeof(dataPacket),
                bytesTransferred, DefaultReadTimeout_ms);

            if (ec && ec != SocketErrorType::Timeout)
            {
                spdlog::error("readout: error reading from network: {} ({}, {})",
                              ec.message(), ec.value(), ec.category().name());
                return 1;
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
            }
        }

        return 0;
    }
};

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::debug);

    bool showHelp = false;
    std::string mcpdAddress = McpdDefaultAddress;
    u16 mcpdId = 0u;
    CliContext ctx = {};

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

        );

    std::vector<std::unique_ptr<BaseCommand>> commands;

    commands.emplace_back(std::make_unique<SetupCommand>(cli));
    commands.emplace_back(std::make_unique<TimingCommand>(cli));
    commands.emplace_back(std::make_unique<DaqCommand>(cli));
    commands.emplace_back(std::make_unique<ReadoutCommand>(cli));

    auto parsed = cli.parse({argc, argv});

    if (!parsed)
    {
        spdlog::error("Error parsing command line: {}", parsed.errorMessage());
        std::cerr << std::endl << cli << std::endl;
        return 1;
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
        std::cerr << cli << std::endl;
        return 0;
    }

    // Connect to the mcpd

    std::error_code ec;

    ctx.cmdSock = connect_udp_socket(ctx.mcpdAddress, ctx.mcpdPort, &ec);

    if (ec)
    {
        spdlog::error("Error connecting to mcpd@{}:{}: {}",
                      ctx.mcpdAddress, ctx.mcpdPort, ec.message());
        return 1;
    }

#if 0 // FIXME: disabled while testing
    McpdVersionInfo vi = {};
    ec = mcpd_get_version(ctx.cmdSock, ctx.mcpdId, vi);

    if (ec)
    {
        spdlog::error("Error reading mcpd version: {} ({}, {})", ec.message(), ec.value(), ec.category().name());
        return 1;
    }

    spdlog::info("Connected to mcpd (cpu={}.{}, fpga={}.{})", vi.cpu[0], vi.cpu[1], vi.fpga[0], vi.fpga[1]);
#endif

    // Find the active command and run it.
    auto active = std::find_if(
        std::begin(commands), std::end(commands),
        [] (const auto &cmd) { return cmd->active(); });

    if (active != std::end(commands))
    {
        spdlog::info("mcpdAddress={}, mcpdId={}", mcpdAddress, mcpdId);
        return (*active)->runCommand(ctx);
    }

    std::cerr << cli << std::endl;
    return 1;
}
