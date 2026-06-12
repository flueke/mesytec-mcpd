#include "mcpd_core.h"
#include <spdlog/spdlog.h> // for fmt::

namespace
{
    class McpdErrorCategory: public std::error_category
    {
        const char *name() const noexcept override
        {
            return "mcpd_error";
        }

        std::string message(int ev) const override
        {
            using mesytec::mcpd::CommandError;

            switch (static_cast<CommandError>(ev))
            {
                case CommandError::NoError:
                    return "No Error";

                case CommandError::IdMismatch:
                    return "ID mismatch";

                default:
                    break;
            }

            return fmt::format("Unknown error code {}", ev);
        }
    };

    const McpdErrorCategory theMcpdErrorCategory {};
}

namespace mesytec::mcpd
{

std::error_code make_error_code(CommandError error)
{
    return { static_cast<int>(error), theMcpdErrorCategory };
}

const char *to_string(const CommandType &cmd)
{
    switch (cmd)
    {
        case CommandType::Reset: return "Reset";
        case CommandType::StartDAQ: return "StartDAQ";
        case CommandType::StopDAQ: return "StopDAQ";
        case CommandType::ContinueDAQ: return "ContinueDAQ";
        case CommandType::SetId: return "SetId";
        case CommandType::SetProtoParams: return "SetProtoParams";
        case CommandType::SetTiming: return "SetTiming";
        case CommandType::SetClock: return "SetClock";
        case CommandType::SetRunId: return "SetRunId";
        case CommandType::SetCell: return "SetCell";
        case CommandType::SetAuxTimer: return "SetAuxTimer";
        case CommandType::SetParam: return "SetParam";
        case CommandType::GetParams: return "GetParams";
        case CommandType::SetGain: return "SetGain";
        case CommandType::SetThreshold: return "SetThreshold";
        case CommandType::SetPulser: return "SetPulser";
        case CommandType::SetMpsdMode: return "SetMpsdMode";
        case CommandType::SetDAC: return "SetDAC";
        case CommandType::SendSerial: return "SendSerial";
        case CommandType::ReadSerial: return "ReadSerial";
        case CommandType::SetTTLOutputs: return "SetTTLOutputs";
        case CommandType::GetBusCapabilities: return "GetBusCapabilities";
        case CommandType::SetBusCapabilities: return "SetBusCapabilities";
        case CommandType::GetMpsdParams: return "GetMpsdParams";
        case CommandType::SetMpsdTxFormat: return "SetMpsdTxFormat";
        case CommandType::SetMstdGain: return "SetMstdGain";
        case CommandType::ReadIds: return "ReadIds";
        case CommandType::GetVersion: return "GetVersion";

        case CommandType::ReadPeripheralRegister: return "ReadPeripheralRegister";
        case CommandType::WritePeripheralRegister: return "WritePeripheralRegister";

        case CommandType::MdllSetTresholds: return "MdllSetTresholds";
        case CommandType::MdllSetSpectrum: return "MdllSetSpectrum";
        case CommandType::MdllSetPulser: return "MdllSetPulser";
        case CommandType::MdllSetTxDataSet: return "MdllSetTxDataSet";
        case CommandType::MdllSetTimingWindow: return "MdllSetTimingWindow";
        case CommandType::MdllSetEnergyWindow: return "MdllSetEnergyWindow";

        case CommandType::WriteRegister: return "WriteRegister";
        case CommandType::ReadRegister: return "ReadRegister";
    }

    return "<unknown CommandType>";
}

template<typename Out>
Out &format(Out &out, const CommandPacket &packet, bool logData = true)
{
    out << "CommandPacket:" << std::endl;
    out << fmt::format("  bufferLength={}", packet.bufferLength) << std::endl;
    out << fmt::format("  bufferType=0x{:04X}", packet.bufferType) << std::endl;
    out << fmt::format("  headerLength={}", packet.headerLength) << std::endl;
    out << fmt::format("  bufferNumber={}", packet.bufferNumber) << std::endl;
    u16 cmd = (packet.cmd & CommandNumberMask);
    u16 cmdError = (packet.cmd & CommandErrorMask) >> CommandErrorShift;
    out << fmt::format("  cmd={}/0x{:04X}/{}, err={}, cmd={}",
                       packet.cmd,
                       packet.cmd,
                       mcpd_cmd_to_string(packet.cmd),
                       cmdError, cmd
                      )
        << std::endl;
    out << fmt::format("  deviceStatus=0x{:02X}", packet.deviceStatus) << std::endl;
    out << fmt::format("  deviceId=0x{:02X}", packet.deviceId) << std::endl;
    out << fmt::format("  time={}, {}, {}", packet.time[0], packet.time[1], packet.time[2]) << std::endl;
    out << fmt::format("  headerChecksum=0x{:04X}", packet.headerChecksum) << std::endl;

    const u16 dataLen = get_data_length(packet);

    out << fmt::format("  calculated data length={}", dataLen) << std::endl;

    if (logData)
        for (unsigned i=0; i<dataLen; ++i)
            out << fmt::format("    data[{}] = 0x{:04X}", i, packet.data[i]) << std::endl;

    return out;
}

std::string to_string(const CommandPacket &packet)
{
    std::stringstream ss;
    format(ss, packet);
    return ss.str();
}

std::string raw_data_to_string(const CommandPacket &packet)
{
    const u16 dataLen = get_data_length(packet);
    const auto rawShortData = reinterpret_cast<const u16 *>(&packet);
    const auto rawShortSize = sizeof(CommandPacket) / sizeof(u16) - CommandPacketMaxDataWords + dataLen;
    assert(rawShortSize < sizeof(CommandPacket)); (void) rawShortSize;

    std::stringstream out;
    out << "raw packet header (including header checksum):\n";
    for (size_t i = 0; i < packet.headerLength; ++i)
        out << fmt::format("  [{:02d}] {:#06x}\n", i, rawShortData[i]);

    out << "raw packet data:\n";
    for (size_t i = packet.headerLength; i < packet.headerLength + dataLen; ++i)
    {
        out << fmt::format("  [{:02d}] {:#06x}\n", i, rawShortData[i]);
    }

    return out.str();
}

template<typename Out>
Out &format(Out &out, const DataPacket &packet)
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

    auto dataLen = get_data_length(packet);

    out << fmt::format("  calculated data length={}", dataLen) << std::endl;

    for (int i=0; i<dataLen; ++i)
        out << fmt::format("    data[{}] = 0x{:04X}", i, packet.data[i]) << std::endl;

    return out;
}

std::string to_string(const DataPacket &packet)
{
    std::stringstream ss;
    format(ss, packet);
    return ss.str();
}

std::string packet_buffer_type_to_string(u16 bufferType)
{
    switch (bufferType)
    {
        case CommandPacketBufferType: return "CommandPacket";
        case McpdDataBufferType: return "McpdData";
        case MdllDataBufferType: return "MdllData";
        default:
            break;
    }

    return fmt::format("<unknown> (0x{:04X})", bufferType);
}

template<typename Out>
Out &format(Out &out, const DecodedEvent &event)
{
    out << "Event: ";

    switch (event.type)
    {
        case EventType::Neutron:
            out << fmt::format("type={}", to_string(event.type));
            out << fmt::format(", mcpdId={}", event.deviceId);
            out << fmt::format(", mpsdId={}", event.neutron.mpsdId);
            out << fmt::format(", channel={}", event.neutron.channel);
            out << fmt::format(", amplitude={}", event.neutron.amplitude);
            out << fmt::format(", position={}", event.neutron.position);
            break;

        case EventType::Trigger:
            out << fmt::format("type={}", to_string(event.type));
            out << fmt::format(", mcpdId={}", event.deviceId);
            out << fmt::format(", triggerId={}", event.trigger.triggerId);
            out << fmt::format(", dataId={}", event.trigger.dataId);
            out << fmt::format(", value={}", event.trigger.value);
            break;

        case EventType::MdllNeutron:
            out << fmt::format("type={}", to_string(event.type));
            out << fmt::format(", amplitude={}", event.mdllNeutron.amplitude);
            out << fmt::format(", xPos={}", event.mdllNeutron.xPos);
            out << fmt::format(", yPos={}", event.mdllNeutron.yPos);
            break;
    }

    out << fmt::format(", timestamps: packet={}, event={}, full={}",
        event.packet_timestamp, event.event_timestamp, event.timestamp);

    return out;
}

std::string to_string(const DecodedEvent &event)
{
    std::stringstream ss;
    format(ss, event);
    return ss.str();
}

}
