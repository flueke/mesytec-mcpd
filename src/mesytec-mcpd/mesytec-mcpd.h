#ifndef __MESYTEC_MCPD_H__
#define __MESYTEC_MCPD_H__

#include "util/int_types.h"

namespace mesytec
{
namespace mcpd
{

static const u16 CommandFailBit = 1u << 15;
static const std::size_t McpdParamCount = 4;
static const std::size_t McpdParamWords = 3;

static const std::size_t CommandPacketMaxDataWords = 726;
static const std::size_t DataPacketMaxDataWords = 715;

#pragma pack(push, 1)
struct CommandPacketHeader
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

struct DataPacketHeader
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

// Standard MTU is 1500 bytes
// IPv4 header is 20 bytes
// UDP header is 8 bytes
static const std::size_t MaxPacketSize = 1500 - (20 + 8);

static_assert(sizeof(CommandPacketHeader) <= MaxPacketSize,
              "CommandPacket too large for standard MTU");

static_assert(sizeof(DataPacketHeader) <= MaxPacketSize,
              "DataPacket too large for standard MTU");

static const u32 BUFTYPE = 0x8000;
static const u32 CMDBUFLEN = 1u;
static const u32 CMDHEADLEN = 10u;
static const u32 STDBUFLEN = 1;
static const u16 BufferTerminator = 0xffff;

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
    //ScanPeriphery = 20, // FIXME: not in official docs!
    SetTTLOutputs = 21,
    GetBusCapabilities = 22,
    SetBusCapabilities = 23,
    GetMpsdParams = 24,
    SetFastTxMode = 25,
    GetVersion = 51,
};

static const char *DefaultMcpdIpAddress = "192.168.168.121";
static const u16 DefaultMcpdPort = 54321u;

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
