#include "source_stats.h"

namespace mesytec::mcpd
{

void SourceStatsCollector::consume(const ReceivedPacket &rp)
{
    namespace ec = event_constants;

    const auto &packet = rp.packet;
    const bool isData = packet.bufferType == McpdDataBufferType
        || packet.bufferType == MdllDataBufferType;
    const size_t eventCount = isData ? get_event_count(packet) : 0;
    size_t triggers = 0;

    for (size_t i = 0; i < eventCount; ++i)
    {
        const u16 *w = packet.data + i * 3;
        const u64 event = to_48bit_value(w[0], w[1], w[2]);
        if (static_cast<EventType>((event >> ec::IdShift) & ec::IdMask) == EventType::Trigger)
            ++triggers;
    }

    std::lock_guard<std::mutex> guard(mutex_);
    auto &st = stats_[source_key(rp)];

    if (st.haveBufferNumber)
    {
        if (auto gap = buffer_number_gap(st.lastBufferNumber, packet.bufferNumber))
            st.packetsLost += *gap;
        else
            ++st.bufferNumberJumps;
    }

    st.bufferType = packet.bufferType;
    st.lastBufferNumber = packet.bufferNumber;
    st.haveBufferNumber = true;
    ++st.packets;
    st.bytes += rp.bytes;
    st.events += eventCount;
    st.triggerEvents += triggers;
    st.neutronEvents += eventCount - triggers;
    st.lastDeviceStatus = packet.deviceStatus;
    st.lastRunId = packet.runId;
    st.lastHeaderTimestamp = get_header_timestamp(packet);
    st.lastParams = get_parameter_values(packet);
}

std::map<SourceKey, SourceStats> SourceStatsCollector::getStats() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return stats_;
}

SourceStats SourceStatsCollector::getTotals() const
{
    SourceStats result;
    std::lock_guard<std::mutex> guard(mutex_);

    for (const auto &[key, st]: stats_)
    {
        result.packets += st.packets;
        result.bytes += st.bytes;
        result.events += st.events;
        result.neutronEvents += st.neutronEvents;
        result.triggerEvents += st.triggerEvents;
        result.packetsLost += st.packetsLost;
        result.bufferNumberJumps += st.bufferNumberJumps;
    }

    return result;
}

void SourceStatsCollector::reset()
{
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto &[key, st]: stats_)
        st = {};
}

void SourceStatsCollector::clear()
{
    std::lock_guard<std::mutex> guard(mutex_);
    stats_.clear();
}

} // namespace mesytec::mcpd
