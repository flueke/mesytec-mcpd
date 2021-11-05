#ifndef __MESYTEC_MCPD_H__
#define __MESYTEC_MCPD_H__

#include "util/int_types.h"

namespace mesytec
{
namespace mcpd
{

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
    u16 data[750];
}; // FIXME: this is >1500 bytes which is too long for a single udp packet (@mtu=1500)

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
    u16 param[4][3];
    u16 data[750];
}; // FIXME: this is >1500 bytes which is too long for a single udp packet (@mtu=1500)

static const u32 BUFTYPE = 0x8000;
static const u32 CMDBUFLEN = 1u;
static const u32 CMDHEADLEN = 10u;
static const u32 STDBUFLEN = 1;
static const u16 BufferTerminator = 0xffff;

// Standard MTU is 1500 bytes
// IPv4 header is 20 bytes
// UDP header is 8 bytes
static const std::size_t MaxPacketSize = 1500 - 20 - 8;

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
    GetParam = 12,
    SetGain = 13,
    SetThreshold = 14,
    SetPulser = 15,
    SetMode = 16,
    SetDAC = 17,
    SendSerial = 18,
    ReadSerial = 19,
    //ScanPeriphery = 20, // FIXME: not in official docs!
    SetTTLOutputs = 21,
    GetBusCapabilities = 22,
    SetBusCapabilities = 23,
    GetParameters = 24,
    SetFastTxMode = 25,
    GetVersion = 51,
};

static const char *DefaultMcpdIpAddress = "192.168.168.121";
static const u16 DefaultMcpdPort = 54321u;

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
