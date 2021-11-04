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
    u16 headerChksum;
};

struct DataPacketHeader
{
    u16 bufferLength;
    u16 bufferType;
    u16 headerLength;
    u16 bufferNumber;
    u16 cmd;
    u8 deviceStatus;
    u8 deviceId;
    u16 time[3];
    u16 param[4][3];
};

static const u32 BUFTYPE = 0x8000;
static const u32 CMDBUFLEN = 1u;
static const u32 CMDHEADLEN = 10u;
static const u32 STDBUFLEN = 1;

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
    ScanPeriphery = 20, // FIXME: not in official docs!
    SetTTLOutputs = 21,
    GetBusCapabilities = 22,
    SetBusCapabilities = 23,
    GetParameters = 24,
    SetFastTxMode = 25,
    GetVersion = 51,
};

}
}

#endif /* __MESYTEC_MCPD_H__ */
