#ifndef __MESYTEC_MCPD_H__
#define __MESYTEC_MCPD_H__

#include <cassert>
#include <fmt/format.h>

#include "util/int_types.h"
#include "util/udp_sockets.h"

namespace mesytec
{
namespace mcpd
{

static const u16 CommandNumberMask = 0x00FFu;
static const u16 CommandErrorMask = 0xFF00u;
static const u16 CommandErrorShift = 8u;

static const std::size_t McpdParamCount = 4;
static const std::size_t McpdParamWords = 3;

static const std::size_t CommandPacketMaxDataWords = 726;
static const std::size_t DataPacketMaxDataWords = 715;

#pragma pack(push, 1)
struct CommandPacket
{
    u16 bufferLength;
    u16 bufferType;
    u16 headerLength;
    u16 bufferNumber;
    u16 cmd;
    u8 deviceStatus;
    u8 deviceId;
    u16 time[3];
    u16 headerChecksum;
    u16 data[CommandPacketMaxDataWords];
};

struct DataPacket
{
    u16 bufferLength;
    u16 bufferType;
    u16 headerLength;
    u16 bufferNumber;
    u16 runId;
    u8 deviceStatus;
    u8 deviceId;
    u16 time[3];
    u16 param[McpdParamCount][McpdParamWords];
    u16 data[DataPacketMaxDataWords];
};
#pragma pack(pop)

static_assert(sizeof(CommandPacket) <= MaxOutgoingPayloadSize,
              "CommandPacket too large for standard MTU");

static_assert(sizeof(DataPacket) <= MaxOutgoingPayloadSize,
              "DataPacket too large for standard MTU");

static const u32 CMDBUFTYPE = 0x8000;
static const u32 CMDHEADLEN = 10u;
static const u16 BufferTerminator = 0xFFFFu;

enum class CommandType: u16
{
    Reset = 0,
    StartDAQ = 1,
    StopDAQ = 2,
    ContinueDAQ = 3,
    SetId = 4,
    SetProtoParams = 5,
    SetTiming = 6,
    SetClock = 7,
    SetRunId = 8,
    SetCell = 9,
    SetAuxTimer = 10,
    SetParam = 11,
    GetParams = 12,
    SetGain = 13,
    SetThreshold = 14,
    SetPulser = 15,
    SetMpsdMode = 16,
    SetDAC = 17,
    SendSerial = 18,
    ReadSerial = 19,
    //ScanPeriphery = 20, // FIXME: not in docs!
    SetTTLOutputs = 21,
    GetBusCapabilities = 22,
    SetBusCapabilities = 23,
    GetMpsdParams = 24,
    SetFastTxMode = 25,

    ReadId = 36, // FIXME: not in docs, scans the busses for MPSD-8 modules

    GetVersion = 51,
};

inline const char *to_string(const CommandType &cmd)
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
        case CommandType::SetFastTxMode: return "SetFastTxMode";
        case CommandType::ReadId: return "ReadId";
        case CommandType::GetVersion: return "GetVersion";
    }

    return "<unknown CommandType>";
}

inline const char *mcpd_cmd_to_string(u16 cmd)
{
    return to_string(static_cast<CommandType>(cmd & ~CommandErrorMask));
}

static const char *DefaultMcpdIpAddress = "192.168.168.121";
static const u16 DefaultMcpdPort = 54321u;

struct McpdVersionInfo
{
    // major and minor version numbers for cpu and fpga
    u16 cpu[2];
    u8 fpga[2];
};

struct McpdParams
{
    u16 adc[2];
    u16 dac[2];
    u16 ttlOut;
    u16 ttlIn;
    u16 eventCounters[3];
    u16 params[McpdParamCount][3];
};

namespace bus_capabilities
{
    // FIXME: improve the names once I understand them
    static const unsigned PosOrAmp = 1u << 0;
    static const unsigned TofPosOrAmp = 1u << 1;
    static const unsigned TofPosAndAmp = 1u << 2;
};

struct BusCapabilities
{
    u8 available;
    u8 selected;
};

enum class TimingRole
{
    Slave = 0,
    Master = 1,
};

enum class BusTermination
{
    On = 0,
    Off = 1,
};

enum class CellName: u16
{
    // Frontpanel monitor/chooper inputs.
    Monitor0 = 0,
    Monitor1 = 1,
    Monitor2 = 2,
    Monitor3 = 3,

    // Backpanel digital inputs
    DigitalIn1 = 4,
    DigitalIn2 = 5,

    // Backpanel ADC inputs.
    ADC1 = 6,
    ADC2 = 7,
};

enum class TriggerSource: u16
{
    NoTrigger = 0,
    AuxTimer0,
    AuxTimer1,
    AuxTimer2,
    AuxTimer3,
    RisingEdgeRearInput1,
    RisingEdgeRearInput2,
    CompareRegister, // Counter-type cells only
};

namespace compare_register_special_values
{
    // Values from 0 to 20 specify the bit index to trigger on.
    static const u16 TriggerOnCounterOverflow = 21;
    static const u16 TriggerOnRisingEdge = 22;
}

static const std::size_t CellCount = 6;

enum class CounterSource: u16
{
    Monitor0 = 0,
    Monitor1 = 1,
    Monitor2 = 2,
    Monitor3 = 3,
    DigitalIn1 = 4,
    DigitalIn2 = 5,
    AllDigitalAndAdcInputs = 6,
    EventCounter,
    MasterClock,
};

enum class ChannelPosition
{
    Left,
    Right,
    Center,
};

enum class PulserState
{
    Off,
    On
};

enum class MpsdMode
{
    Position,
    Amplitude,
};

struct MpsdParameters
{
    u8 mpsdId;
    u16 busTxCaps;
    u16 fastTxFormat;
    u16 firmwareRevision;
};

enum class EventType
{
    Neutron,
    Trigger
};

inline const char *to_string(const EventType &et)
{
    switch (et)
    {
        case EventType::Neutron:
            return "Neutron";
        case EventType::Trigger:
            return "Trigger";
    }

    return "<unknown EventType>";
}

namespace event_constants
{
    static const std::size_t IdBits = 1u;
    static const std::size_t IdShift = 47u;
    static const std::size_t IdMask = (1u << IdBits) - 1;

    static const std::size_t MpsdIdBits = 3u;
    static const std::size_t MpsdIdShift = 44u;
    static const std::size_t MpsdIdMask = (1u << MpsdIdBits) - 1;

    static const std::size_t ChannelBits = 5u;
    static const std::size_t ChannelShift = 39u;
    static const std::size_t ChannelMask = (1u << ChannelBits) - 1;

    static const std::size_t AmplitudeBits = 10u;
    static const std::size_t AmplitudeShift = 29u;
    static const std::size_t AmplitudeMask = (1u << AmplitudeBits) - 1;

    static const std::size_t PositionBits = 10u;
    static const std::size_t PositionShift = 19u;
    static const std::size_t PositionMask = (1u << PositionBits) - 1;

    static const std::size_t TimestampBits = 19u;
    static const std::size_t TimestampShift = 0u;
    static const std::size_t TimestampMask = (1u << TimestampBits) - 1;
}

inline u16 calculate_checksum(const CommandPacket &cmd)
{
    u16 result = 0u;
    const u16 *b = reinterpret_cast<const u16 *>(&cmd);

	for (unsigned i = 0; i < cmd.bufferLength; i++)
		result ^= b[i];

    return result;
}

inline unsigned get_data_length(const CommandPacket &packet)
{
    assert(packet.bufferLength >= packet.headerLength);
    unsigned dataLen = packet.bufferLength - packet.headerLength;
    return dataLen;
}

inline unsigned get_data_length(const DataPacket &packet)
{
    assert(packet.bufferLength >= packet.headerLength);
    unsigned dataLen = packet.bufferLength - packet.headerLength;
    return dataLen;
}

template<typename Out>
Out &format(Out &out, const CommandPacket &packet)
{
    out << "CommandPacket:" << std::endl;
    out << fmt::format("  bufferLength={}", packet.bufferLength) << std::endl;
    out << fmt::format("  bufferType=0x{:04X}", packet.bufferType) << std::endl;
    out << fmt::format("  headerLength={}", packet.headerLength) << std::endl;
    out << fmt::format("  bufferNumber={}", packet.bufferNumber) << std::endl;
    u16 cmd = (packet.cmd & CommandNumberMask);
    u16 cmdError = (packet.cmd & CommandErrorMask) >> CommandErrorShift;
    out << fmt::format("  cmd={}/0x{:04X}/{} err={},cmd={}",
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

    u16 dataLen = get_data_length(packet);

    out << fmt::format("  calculated data length={}", dataLen) << std::endl;

    for (unsigned i=0; i<dataLen; ++i)
        out << fmt::format("    data[{}] = 0x{:04X}", i, packet.data[i]) << std::endl;

    return out;
}

inline std::string to_string(const CommandPacket &packet)
{
    std::stringstream ss;
    format(ss, packet);
    return ss.str();
}

inline std::array<u64, McpdParamCount> get_parameter_values(const DataPacket &packet)
{
    std::array<u64, McpdParamCount> result = {};

    for (size_t i=0; i<McpdParamCount; ++i)
    {
        u64 pv = (  (static_cast<u64>(packet.param[i][0]) <<  0)
                  | (static_cast<u64>(packet.param[i][1]) << 16)
                  | (static_cast<u64>(packet.param[i][2]) << 32));

        result[i] = pv;
    }

    return result;
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

    u16 dataLen = get_data_length(packet);

    out << fmt::format("  calculated data length={}", dataLen) << std::endl;

    for (unsigned i=0; i<dataLen; ++i)
        out << fmt::format("    data[{}] = 0x{:04X}", i, packet.data[i]) << std::endl;

    return out;
}

inline std::string to_string(const DataPacket &packet)
{
    std::stringstream ss;
    format(ss, packet);
    return ss.str();
}

inline size_t get_event_count(const DataPacket &packet)
{
    size_t eventCount = get_data_length(packet) / 3;
    return eventCount;
}

inline u64 get_header_timestamp(const DataPacket &packet)
{
    u64 result = (  (static_cast<u64>(packet.time[0]) <<  0)
                  | (static_cast<u64>(packet.time[1]) << 16)
                  | (static_cast<u64>(packet.time[2]) << 32));
    return result;
}

inline u64 get_event(const DataPacket &packet, size_t eventNum)
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
    u64 timestamp;
};

inline DecodedEvent decode_event(const DataPacket &packet, size_t eventNum)
{
    namespace ec = event_constants;

    u64 event = get_event(packet, eventNum);

    DecodedEvent result = {};

    result.type = static_cast<EventType>((event >> ec::IdShift) & ec::IdMask);
    result.mpsdId = (event >> ec::MpsdIdShift) & ec::MpsdIdMask;
    result.channel = (event >> ec::ChannelShift) & ec::ChannelMask;
    result.amplitude = (event >> ec::AmplitudeShift) & ec::AmplitudeMask;
    result.position = (event >> ec::PositionShift) & ec::PositionMask;
    result.timestamp = (event >> ec::TimestampShift) & ec::TimestampMask;

    // Add the 48 bit header timestamp to the events 19 bit timestamp value.
    result.timestamp += get_header_timestamp(packet);

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

inline std::string to_string(const DecodedEvent &event)
{
    std::stringstream ss;
    format(ss, event);
    return ss.str();
}

#if 0
class Mcpd
{
    public:
    private:
        int m_cmdSock = -1;
        int m_dataSock = -1;
        struct sockaddr_in m_cmdAddr = {};
        struct sockaddr_in m_dataAddr = {};
};
#endif

}
}

#endif /* __MESYTEC_MCPD_H__ */
