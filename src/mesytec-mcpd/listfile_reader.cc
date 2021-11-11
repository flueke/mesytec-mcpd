#include <fstream>
#include <sstream>
#include <iostream>
#include <spdlog/spdlog.h>

#include "mesytec-mcpd.h"

using namespace mesytec::mcpd;

int main(int argc, char *argv[])
{
    std::ifstream lstIn("./mcpd.list");
    size_t packetNumber = 0;

    while (!lstIn.fail() && !lstIn.eof())
    {
        DataPacket packet = {};

        lstIn.read(reinterpret_cast<char *>(&packet), sizeof(packet));

        if (lstIn.eof())
            break;

        //std::cout << fmt::format("packet#{}: {}",
        //                         packetNumber, to_string(packet)) << std::endl;

        std::cout << fmt::format("packet#{}:",
                                 packetNumber) << std::endl;

        size_t eventCount = get_data_length(packet) / 3;

        for (size_t eventNum=0; eventNum<eventCount; ++eventNum)
        {
            auto event = decode_event(packet, eventNum);
            format(std::cout, event);
            std::cout << std::endl;
        }

        ++packetNumber;
    }

    return 0;
}
