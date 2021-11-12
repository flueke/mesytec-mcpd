#include <iostream>
#include <mesytec-mcpd/mesytec-mcpd.h>
#include <lyra/lyra.hpp>
#include <spdlog/spdlog.h>

int main(int argc, char *argv[])
{
    bool showHelp = false;
    std::string currentIp, newIp;
    unsigned curId = 0u, newId = 0u;

    auto cli = (lyra::help(showHelp)

                | lyra::opt(currentIp, "currentIp")
                ["--current-ip"]("current ip address/host")
                .required()

                | lyra::opt(curId, "currentId")
                ["--cur-id"]("current mcpd id")
                .required()

                | lyra::opt(newIp, "newIp")
                ["--new-ip"]("new ip address")

                | lyra::opt(newId, "newId")
                ["--new-id"]("new mcpd id")
               );

    auto result = cli.parse({argc, argv});

    if (!result)
    {
        std::cerr << "Error parsing command line: " << result.errorMessage() << std::endl;
        std::cerr << cli << std::endl;
        return 1;
    }

    if (showHelp)
    {
        std::cerr << cli << std::endl;
        return 0;
    }

    return 0;
}

