#include <chrono>
#include <deque>
#include <filesystem>
#include <thread>

#include <gtest/gtest.h>

#include "daq.h"

using namespace mesytec::mcpd;

namespace
{

u64 mdll_neutron(u64 amp, u64 x, u64 y)
{
    namespace ec = event_constants::mdll_neutron;
    return (amp << ec::AmplitudeShift) | (x << ec::xPosShift) | (y << ec::yPosShift);
}

u64 mpsd_neutron(u64 mpsd, u64 channel, u64 amp, u64 pos)
{
    namespace ec = event_constants::neutron;
    return (mpsd << ec::MpsdIdShift) | (channel << ec::ChannelShift)
        | (amp << ec::AmplitudeShift) | (pos << ec::PositionShift);
}

u64 trigger_event() { return u64(1) << event_constants::IdShift; }

ReceivedPacket make_packet(u16 bufferType, u8 deviceId, u16 bufferNumber,
                           const std::vector<u64> &events, u32 srcAddr = 0x7f000001)
{
    ReceivedPacket rp;
    auto &p = rp.packet;
    p.bufferType = bufferType;
    p.deviceId = deviceId;
    p.bufferNumber = bufferNumber;
    p.headerLength = 21;
    p.bufferLength = p.headerLength + events.size() * 3;

    for (size_t i = 0; i < events.size(); ++i)
    {
        auto [w0, w1, w2] = from_48bit_value(events[i]);
        p.data[i * 3 + 0] = w0;
        p.data[i * 3 + 1] = w1;
        p.data[i * 3 + 2] = w2;
    }

    rp.bytes = p.bufferLength * sizeof(u16);
    rp.srcAddr = srcAddr;
    return rp;
}

// Hands out prepared results: a packet, a timeout, or end of data when empty.
class TestSource: public PacketSource
{
  public:
    std::deque<std::optional<ReceivedPacket>> items;

    Result read(ReceivedPacket &dest) override
    {
        if (items.empty())
            return Result::EndOfData;
        auto item = items.front();
        items.pop_front();
        if (!item)
            return Result::Timeout;
        dest = *item;
        return Result::Packet;
    }
};

void wait_until_stopped(const ReadoutWorker &w)
{
    for (int i = 0; i < 200 && w.isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_FALSE(w.isRunning());
}

} // namespace

TEST(readout_core, BufferNumberGap)
{
    EXPECT_EQ(buffer_number_gap(0, 1), 0);
    EXPECT_EQ(buffer_number_gap(0, 3), 2);
    EXPECT_EQ(buffer_number_gap(65535, 0), 0);
    EXPECT_EQ(buffer_number_gap(65534, 1), 2);
    EXPECT_EQ(buffer_number_gap(0, 0x7fff), 0x7ffe);
    EXPECT_EQ(buffer_number_gap(5, 5), std::nullopt);
    EXPECT_EQ(buffer_number_gap(10, 3), std::nullopt);
    EXPECT_EQ(buffer_number_gap(0, 0x8000), std::nullopt);
}

TEST(readout_core, PacketValidation)
{
    auto rp = make_packet(MdllDataBufferType, 0, 0, {mdll_neutron(1, 2, 3)});
    EXPECT_TRUE(is_valid_data_packet(rp));

    auto truncated = rp;
    truncated.bytes -= 2;
    EXPECT_FALSE(is_valid_data_packet(truncated));

    auto shortHeader = rp;
    shortHeader.bytes = 10;
    EXPECT_FALSE(is_valid_data_packet(shortHeader));

    auto badHeaderLength = rp;
    badHeaderLength.packet.headerLength = rp.packet.bufferLength + 1;
    EXPECT_FALSE(is_valid_data_packet(badHeaderLength));
}

TEST(readout_core, SourceStats)
{
    SourceStatsCollector stats;
    stats.consume(make_packet(MdllDataBufferType, 0, 10, {mdll_neutron(1, 2, 3), trigger_event()}));
    stats.consume(make_packet(MdllDataBufferType, 0, 12, {}));
    stats.consume(make_packet(MdllDataBufferType, 0, 12, {}));
    stats.consume(make_packet(McpdDataBufferType, 1, 0, {mpsd_neutron(1, 2, 3, 4)}));

    auto s = stats.getStats();
    ASSERT_EQ(s.size(), 2u);
    const auto &a = s[{0x7f000001, 0}];
    EXPECT_EQ(a.packets, 3u);
    EXPECT_EQ(a.neutronEvents, 1u);
    EXPECT_EQ(a.triggerEvents, 1u);
    EXPECT_EQ(a.packetsLost, 1u);
    EXPECT_EQ(a.bufferNumberJumps, 1u);

    const auto &b = s[{0x7f000001, 1}];
    EXPECT_EQ(b.bufferType, McpdDataBufferType);
    EXPECT_EQ(b.neutronEvents, 1u);

    EXPECT_EQ(stats.getTotals().packets, 4u);

    stats.reset();
    EXPECT_EQ(stats.getStats().size(), 2u);
    EXPECT_EQ(stats.getTotals().packets, 0u);
    stats.clear();
    EXPECT_TRUE(stats.getStats().empty());
}

TEST(readout_core, Histogrammers)
{
    MdllHistogrammer mdll;
    McpdHistogrammer mcpd;

    auto mdllPacket = make_packet(MdllDataBufferType, 0, 0,
                                  {mdll_neutron(255, 1023, 5), mdll_neutron(255, 1023, 5), trigger_event()});
    auto mcpdPacket = make_packet(McpdDataBufferType, 0, 0, {mpsd_neutron(7, 31, 1023, 0)});

    for (auto *rp: {&mdllPacket, &mcpdPacket})
    {
        mdll.consume(*rp);
        mcpd.consume(*rp);
    }

    auto h = mdll.getHistograms({0x7f000001, 0});
    ASSERT_TRUE(h);
    EXPECT_EQ(h->amplitude[255], 2u);
    EXPECT_EQ(h->x[1023], 2u);
    EXPECT_EQ(h->y[5], 2u);
    EXPECT_EQ(h->xy[5 * MdllHistograms::XBins + 1023], 2u);

    auto m = mcpd.getHistograms({0x7f000001, 0});
    ASSERT_TRUE(m);
    EXPECT_EQ(m->amplitude[McpdHistograms::index(7, 31, 1023)], 1u);
    EXPECT_EQ(m->position[McpdHistograms::index(7, 31, 0)], 1u);

    EXPECT_FALSE(mdll.getHistograms({0x7f000001, 1}));

    mdll.clearHistograms();
    EXPECT_EQ(mdll.getHistograms({0x7f000001, 0})->amplitude[255], 0u);
    mdll.clear();
    EXPECT_TRUE(mdll.getSources().empty());
}

TEST(readout_core, WorkerCountsAndDispatch)
{
    auto source = std::make_unique<TestSource>();
    source->items.push_back(make_packet(MdllDataBufferType, 0, 0, {mdll_neutron(1, 2, 3)}));
    source->items.push_back(std::nullopt);
    auto invalid = make_packet(MdllDataBufferType, 0, 1, {});
    invalid.bytes = 4;
    source->items.push_back(invalid);
    source->items.push_back(make_packet(MdllDataBufferType, 0, 2, {mdll_neutron(1, 2, 3)}));

    auto stats = std::make_shared<SourceStatsCollector>();
    ReadoutWorker worker(std::move(source), nullptr, {stats});
    worker.start();
    wait_until_stopped(worker);

    auto c = worker.getCounters();
    EXPECT_EQ(c.packets, 3u);
    EXPECT_EQ(c.timeouts, 1u);
    EXPECT_EQ(c.invalidPackets, 1u);
    EXPECT_FALSE(worker.hasException());
    EXPECT_EQ(stats->getTotals().packets, 2u);
    EXPECT_EQ(stats->getTotals().packetsLost, 1u);
}

TEST(readout_core, ListfileRoundtrip)
{
    const auto path = (std::filesystem::temp_directory_path() / "readout_core_test.mcpdlst").string();

    {
        auto source = std::make_unique<TestSource>();
        for (u16 i = 0; i < 5; ++i)
            source->items.push_back(make_packet(MdllDataBufferType, 3, i, {mdll_neutron(i, i, i)}));
        ReadoutWorker worker(std::move(source), std::make_unique<ListfileWriter>(path, true), {});
        worker.start();
        wait_until_stopped(worker);
        worker.stop();
        EXPECT_EQ(worker.getCounters().listfileBytes, 5 * sizeof(DataPacket));
    }

    EXPECT_EQ(std::filesystem::file_size(path), 5 * sizeof(DataPacket));
    EXPECT_THROW(ListfileWriter(path, false), std::runtime_error);

    Daq daq;
    daq.startReplay(path);
    for (int i = 0; i < 200 && daq.isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    daq.stop();

    auto stats = daq.stats().getStats();
    ASSERT_EQ(stats.size(), 1u);
    const auto &st = stats[{0, 3}];
    EXPECT_EQ(st.packets, 5u);
    EXPECT_EQ(st.packetsLost, 0u);
    EXPECT_EQ(daq.mdllHistos().getHistograms({0, 3})->amplitude[4], 1u);

    std::filesystem::remove(path);
}

TEST(readout_core, WorkerStoresSourceException)
{
    class ThrowingSource: public PacketSource
    {
        Result read(ReceivedPacket &) override { throw std::runtime_error("boom"); }
    };

    ReadoutWorker worker(std::make_unique<ThrowingSource>(), nullptr, {});
    worker.start();
    wait_until_stopped(worker);
    EXPECT_TRUE(worker.hasException());
    EXPECT_THROW(worker.rethrowException(), std::runtime_error);
    EXPECT_FALSE(worker.hasException());
}
