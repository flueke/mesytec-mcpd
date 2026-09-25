#include "histogrammers.h"

#include <algorithm>

namespace mesytec::mcpd
{

namespace
{
inline u64 get_raw_event(const DataPacket &packet, size_t eventIndex)
{
    const u16 *w = packet.data + eventIndex * 3;
    return to_48bit_value(w[0], w[1], w[2]);
}

inline bool is_neutron(u64 event)
{
    namespace ec = event_constants;
    return static_cast<EventType>((event >> ec::IdShift) & ec::IdMask) == EventType::Neutron;
}

void zero(std::vector<u64> &v) { std::fill(v.begin(), v.end(), 0u); }
} // namespace

void MdllHistograms::fill(const DataPacket &packet)
{
    namespace ec = event_constants::mdll_neutron;

    const size_t eventCount = get_event_count(packet);

    for (size_t i = 0; i < eventCount; ++i)
    {
        const u64 event = get_raw_event(packet, i);

        if (!is_neutron(event))
            continue;

        const auto amp = (event >> ec::AmplitudeShift) & ec::AmplitudeMask;
        const auto xPos = (event >> ec::xPosShift) & ec::xPosMask;
        const auto yPos = (event >> ec::yPosShift) & ec::yPosMask;

        ++amplitude[amp];
        ++x[xPos];
        ++y[yPos];
        ++xy[yPos * XBins + xPos];
    }
}

void MdllHistograms::clear()
{
    zero(amplitude);
    zero(x);
    zero(y);
    zero(xy);
}

void McpdHistograms::fill(const DataPacket &packet)
{
    namespace ec = event_constants::neutron;

    const size_t eventCount = get_event_count(packet);

    for (size_t i = 0; i < eventCount; ++i)
    {
        const u64 event = get_raw_event(packet, i);

        if (!is_neutron(event))
            continue;

        const auto mpsdId = (event >> ec::MpsdIdShift) & ec::MpsdIdMask;
        const auto channel = (event >> ec::ChannelShift) & ec::ChannelMask;
        const auto amp = (event >> ec::AmplitudeShift) & ec::AmplitudeMask;
        const auto pos = (event >> ec::PositionShift) & ec::PositionMask;

        ++amplitude[index(mpsdId, channel, amp)];
        ++position[index(mpsdId, channel, pos)];
    }
}

void McpdHistograms::clear()
{
    zero(amplitude);
    zero(position);
}

} // namespace mesytec::mcpd
