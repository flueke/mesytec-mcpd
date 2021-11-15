#include <iostream>

#include <mesytec-mcpd/mesytec-mcpd.h>
#include <lyra/lyra.hpp>
#include <spdlog/spdlog.h>

using namespace mesytec::mcpd;

struct SetupCommand
{
    SetupCommand(lyra::cli &cli)
    {
    }

    void run_command(const lyra::group &g)
    {
    }
};

int main(int argc, char *argv[])
{
    bool showHelp = false;
    std::string mcpdAddress = McpdDefaultAddress;
    u16 mcpdId = 0u;

    auto cliMain = (
        lyra::help(showHelp)

        | lyra::opt(mcpdAddress, "mcpdAddress")
        ["--address"]("MCPD IP-address / hostname")
        .optional()

        | lyra::opt(mcpdId, "mcpdId")
        ["--id"]("MCPD id")
        .optional()

        );

    auto result = cliMain.parse({argc, argv});

    if (!result)
    {
        std::cerr << "Error parsing command line: " << result.errorMessage() << std::endl;
        std::cerr << cliMain << std::endl;
        return 1;
    }

    if (showHelp)
    {
        std::cerr << cliMain << std::endl;
        return 0;
    }

    return 0;
}
