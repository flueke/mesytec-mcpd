#include <cassert>
#include <fstream>
#include <iostream>
#include <mesytec-mcpd/mesytec-mcpd.h>
#include <spdlog/spdlog.h>

#include <cctype>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace mesytec::mcpd;

// Convert a hex char '0'–'9','a'–'f','A'–'F' into 0–15
static int hex_digit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

std::vector<uint8_t> hex_to_bytes(const std::string &hex)
{
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2 + 1);

    for (size_t i = 0; i < hex.size();)
    {
        // Skip non‑hex
        if (!std::isxdigit(static_cast<unsigned char>(hex[i])))
        {
            i++;
            continue;
        }

        int hi = hex_digit(hex[i++]);
        if (hi < 0)
            return out;

        if (i >= hex.size())
            return out;
        int lo = hex_digit(hex[i++]);
        if (lo < 0)
            return out;

        out.push_back((hi << 4) | lo);
    }
    return out;
}


int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: parse_mcpd_packet_from_hex_string <hex-string>\n";
        return 1;
    }

    std::string hexInput = argv[1];
    auto bytes = hex_to_bytes(hexInput);

    const DataPacket *packet = reinterpret_cast<const DataPacket *>(bytes.data());
    std::cout << to_string(*packet) << std::endl;

    const auto eventCount = get_event_count(*packet);
    std::cout << "Packet contains " << eventCount << " events:\n";
    for (size_t ei = 0; ei < eventCount; ++ei)
    {
        auto event = decode_event(*packet, ei);
        std::cout << "  " << to_string(event) << "\n";
    }
}
