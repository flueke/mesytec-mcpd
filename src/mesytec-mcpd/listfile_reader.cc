#include <fstream>
#include <sstream>
#include <iostream>
#include <spdlog/spdlog.h>

#include "mesytec-mcpd.h"

using namespace mesytec::mcpd;

unsigned get_data_length(const DataPacketHeader &packet)
{
    assert(packet.bufferLength >= packet.headerLength);
    unsigned dataLen = packet.bufferLength - packet.headerLength;
    return dataLen;
}

template<typename Out>
Out &format(Out &out, const DataPacketHeader &packet)
{
    out << "DataPacket:" << std::endl;
    out << fmt::format("  bufferLength={}", packet.bufferLength) << std::endl;
    out << fmt::format("  bufferType=0x{:04X}", packet.bufferType) << std::endl;
    out << fmt::format("  headerLength={}", packet.headerLength) << std::endl;
    out << fmt::format("  bufferNumber={}", packet.bufferNumber) << std::endl;
    out << fmt::format("  runId={}", packet.runId) << std::endl;
    out << fmt::format("  deviceStatus=0x{:02X}", packet.deviceStatus) << std::endl;
    out << fmt::format("  deviceId=0x{:02X}", packet.deviceId) << std::endl;
    out << fmt::format("  time={}, {}, {}", packet.time[0], packet.time[1], packet.time[2]) << std::endl;

    for (size_t i=0; i<McpdParamCount; ++i)
    {
        auto &param = packet.param[i];
        out << fmt::format("  param[{}]: {} {} {}",
                           i, param[0], param[1], param[2]) << std::endl;
    }

    u16 dataLen = get_data_length(packet);

    out << fmt::format("  calculated data length={}", dataLen) << std::endl;

    for (unsigned i=0; i<dataLen; ++i)
        out << fmt::format("    data[{}] = 0x{:04X}", i, packet.data[i]) << std::endl;

    return out;
}

std::string to_string(const DataPacketHeader &packet)
{
    std::stringstream ss;
    format(ss, packet);
    return ss.str();
}

size_t get_event_count(const DataPacketHeader &packet)
{
    size_t eventCount = get_data_length(packet) / 3;
    return eventCount;
}

u64 get_event(const DataPacketHeader &packet, size_t eventNum)
{
    const size_t idx = eventNum * 3;

    if (idx + 2 >= get_data_length(packet))
        throw std::runtime_error("eventNum out of range");

    u64 result = (  (static_cast<u64>(packet.data[idx + 0]) <<  0)
                  | (static_cast<u64>(packet.data[idx + 1]) << 16)
                  | (static_cast<u64>(packet.data[idx + 2]) << 32));

    return result;
}

struct DecodedEvent
{
    EventType type;
    u8 mpsdId;
    u8 channel;
    u16 amplitude;
    u16 position;
    u32 timestamp;
};

DecodedEvent decode_event(u64 event)
{
    namespace ec = event_constants;

    DecodedEvent result = {};

    result.type = static_cast<EventType>((event >> ec::IdShift) & ec::IdMask);
    result.mpsdId = (event >> ec::MpsdIdShift) & ec::MpsdIdMask;
    result.channel = (event >> ec::ChannelShift) & ec::ChannelMask;
    result.amplitude = (event >> ec::AmplitudeShift) & ec::AmplitudeMask;
    result.position = (event >> ec::PositionShift) & ec::PositionMask;
    result.timestamp = (event >> ec::TimestampShift) & ec::TimestampMask;

    return result;
}

template<typename Out>
Out &format(Out &out, const DecodedEvent &event)
{
    out << "Event:" << std::endl;
    out << fmt::format("  type={}", to_string(event.type)) << std::endl;
    out << fmt::format("  mpsdId={}", event.mpsdId) << std::endl;
    out << fmt::format("  channel={}", event.channel) << std::endl;
    out << fmt::format("  amplitude={}", event.amplitude) << std::endl;
    out << fmt::format("  position={}", event.position) << std::endl;
    out << fmt::format("  timestamp={}", event.timestamp) << std::endl;

    return out;
}

int main(int argc, char *argv[])
{
    std::ifstream lstIn("./mcpd.list");
    size_t packetNumber = 0;

    while (!lstIn.eof())
    {
        DataPacketHeader packet = {};

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
            u64 rawEvent = get_event(packet, eventNum);
            auto event = decode_event(rawEvent);
            format(std::cout, event);
            std::cout << std::endl;
        }

        ++packetNumber;
    }

    return 0;
}
