#ifndef E83B0D6A_2F4C_4A9B_9E7D_1C5B8A3F6E20
#define E83B0D6A_2F4C_4A9B_9E7D_1C5B8A3F6E20

#include <map>
#include <mutex>
#include <optional>
#include <tuple>

#include "readout_worker.h"

namespace mesytec::mcpd
{

// Data sources are identified by the IPv4 address they send from and the
// device id in the packet header. srcAddr is 0 for listfile data.
struct MESYTEC_MCPD_EXPORT SourceKey
{
    u32 srcAddr = 0;
    u8 deviceId = 0;

    bool operator<(const SourceKey &o) const
    {
        return std::tie(srcAddr, deviceId) < std::tie(o.srcAddr, o.deviceId);
    }

    bool operator==(const SourceKey &o) const
    {
        return srcAddr == o.srcAddr && deviceId == o.deviceId;
    }
};

inline SourceKey source_key(const ReceivedPacket &rp) { return {rp.srcAddr, rp.packet.deviceId}; }

// Number of packets lost between two consecutive 16 bit buffer numbers.
// Returns std::nullopt if the number repeated or went backwards, i.e. the
// forward distance (modulo 2^16) is 0 or >= 0x8000. This happens after a DAQ
// reset or restart and is not counted as loss.
inline std::optional<u16> buffer_number_gap(u16 last, u16 current)
{
    const u16 distance = current - last;
    if (distance == 0 || distance >= 0x8000u)
        return std::nullopt;
    return distance - 1u;
}

struct MESYTEC_MCPD_EXPORT SourceStats
{
    u16 bufferType = 0;     // of the last packet
    u64 packets = 0;
    u64 bytes = 0;
    u64 events = 0;
    u64 neutronEvents = 0;  // MPSD or MDLL neutron events, depending on bufferType
    u64 triggerEvents = 0;
    u64 packetsLost = 0;
    u64 bufferNumberJumps = 0;
    u16 lastBufferNumber = 0;
    bool haveBufferNumber = false;
    u8 lastDeviceStatus = 0;
    u16 lastRunId = 0;
    u64 lastHeaderTimestamp = 0;
    std::array<u64, McpdParamCount> lastParams = {};
};

// Counts packets, events and packet loss per SourceKey. Events are only counted
// for MCPD and MDLL data buffer types.
class MESYTEC_MCPD_EXPORT SourceStatsCollector: public PacketConsumer
{
  public:
    void consume(const ReceivedPacket &rp) override;

    std::map<SourceKey, SourceStats> getStats() const;
    // Totals over all sources. The last* fields are not meaningful.
    SourceStats getTotals() const;

    // Zeroes all counters including the buffer number baseline. Keeps the
    // known sources.
    void reset();
    // Forgets all sources.
    void clear();

  private:
    mutable std::mutex mutex_;
    std::map<SourceKey, SourceStats> stats_;
};

} // namespace mesytec::mcpd

#endif /* E83B0D6A_2F4C_4A9B_9E7D_1C5B8A3F6E20 */
