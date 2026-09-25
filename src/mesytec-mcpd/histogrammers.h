#ifndef F29C5E1B_6D8A_4F0C_A3B7_8E4D1F9C2A56
#define F29C5E1B_6D8A_4F0C_A3B7_8E4D1F9C2A56

// Native resolution event histograms per data source. Counts are u64 so that
// long runs with a fixed position pulser can not overflow a bin.

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "source_stats.h"

namespace mesytec::mcpd
{

struct MESYTEC_MCPD_EXPORT MdllHistograms
{
    static constexpr u16 BufferType = MdllDataBufferType;
    static constexpr size_t AmplitudeBins = 1u << event_constants::mdll_neutron::AmplitudeBits;
    static constexpr size_t XBins = 1u << event_constants::mdll_neutron::xPosBits;
    static constexpr size_t YBins = 1u << event_constants::mdll_neutron::yPosBits;

    std::vector<u64> amplitude = std::vector<u64>(AmplitudeBins);
    std::vector<u64> x = std::vector<u64>(XBins);
    std::vector<u64> y = std::vector<u64>(YBins);
    std::vector<u64> xy = std::vector<u64>(XBins * YBins); // xy[y * XBins + x]

    // Fills the neutron events of the packet. Trigger events are skipped.
    void fill(const DataPacket &packet);
    void clear();
};

// MCPD with MPSD modules: one amplitude and one position histogram per
// (mpsdId, channel). mpsdId is the 3 bit bus number, channel is 5 bits wide
// in the event data even though MPSD-8 modules only use 8 channels.
struct MESYTEC_MCPD_EXPORT McpdHistograms
{
    static constexpr u16 BufferType = McpdDataBufferType;
    static constexpr size_t MpsdCount = 1u << event_constants::neutron::MpsdIdBits;
    static constexpr size_t ChannelCount = 1u << event_constants::neutron::ChannelBits;
    static constexpr size_t AmplitudeBins = 1u << event_constants::neutron::AmplitudeBits;
    static constexpr size_t PositionBins = 1u << event_constants::neutron::PositionBits;

    // amplitude[index(mpsdId, channel, bin)]
    std::vector<u64> amplitude = std::vector<u64>(MpsdCount * ChannelCount * AmplitudeBins);
    std::vector<u64> position = std::vector<u64>(MpsdCount * ChannelCount * PositionBins);

    static size_t index(size_t mpsdId, size_t channel, size_t bin)
    {
        return (mpsdId * ChannelCount + channel) * AmplitudeBins + bin;
    }

    void fill(const DataPacket &packet);
    void clear();
};

static_assert(McpdHistograms::AmplitudeBins == McpdHistograms::PositionBins,
              "McpdHistograms::index() assumes equal bin counts");

// Keeps one Histos instance per SourceKey and fills it from packets with a
// matching buffer type. Thread-safe.
template <typename Histos>
class Histogrammer: public PacketConsumer
{
  public:
    void consume(const ReceivedPacket &rp) override
    {
        if (rp.packet.bufferType != Histos::BufferType)
            return;

        std::lock_guard<std::mutex> guard(mutex_);
        auto &h = histos_[source_key(rp)];
        if (!h)
            h = std::make_unique<Histos>();
        h->fill(rp.packet);
    }

    std::vector<SourceKey> getSources() const
    {
        std::lock_guard<std::mutex> guard(mutex_);
        std::vector<SourceKey> result;
        for (const auto &[key, h]: histos_)
            result.push_back(key);
        return result;
    }

    // Returns a copy of the histograms of the given source.
    std::optional<Histos> getHistograms(const SourceKey &key) const
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (auto it = histos_.find(key); it != histos_.end())
            return *it->second;
        return std::nullopt;
    }

    // Zeroes all histograms, keeps the known sources.
    void clearHistograms()
    {
        std::lock_guard<std::mutex> guard(mutex_);
        for (auto &[key, h]: histos_)
            h->clear();
    }

    // Forgets all sources.
    void clear()
    {
        std::lock_guard<std::mutex> guard(mutex_);
        histos_.clear();
    }

  private:
    mutable std::mutex mutex_;
    std::map<SourceKey, std::unique_ptr<Histos>> histos_;
};

using MdllHistogrammer = Histogrammer<MdllHistograms>;
using McpdHistogrammer = Histogrammer<McpdHistograms>;

} // namespace mesytec::mcpd

#endif /* F29C5E1B_6D8A_4F0C_A3B7_8E4D1F9C2A56 */
