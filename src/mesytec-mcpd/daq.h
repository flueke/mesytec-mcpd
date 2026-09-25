#ifndef A7D3E9C4_1B5F_4C2A_8D6E_3F9B0A4C7E12
#define A7D3E9C4_1B5F_4C2A_8D6E_3F9B0A4C7E12

// Standard readout setup: ReadoutWorker + SourceStatsCollector + MDLL and MCPD
// histogrammers. Stats and histograms survive stop/start, they are only
// modified by the explicit reset/clear calls.
//
// Not thread-safe: control it from one thread. The stats() and *Histos()
// accessors may be used from any thread.

#include <memory>
#include <string>

#include "histogrammers.h"
#include "readout_worker.h"
#include "source_stats.h"

namespace mesytec::mcpd
{

class MESYTEC_MCPD_EXPORT Daq
{
  public:
    explicit Daq(u16 listenPort = McpdDefaultPort);
    ~Daq();

    Daq(const Daq &) = delete;
    Daq &operator=(const Daq &) = delete;

    // Binds the data socket and starts receiving. Writes a listfile if
    // listfile is non-empty. Throws if already running or on setup errors.
    void startReadout(const std::string &listfile = {}, bool overwriteListfile = false);
    void startReplay(const std::string &listfile);
    void stop();

    bool isRunning() const { return worker_ && worker_->isRunning(); }
    bool hasException() const { return worker_ && worker_->hasException(); }
    void rethrowException();

    u16 listenPort() const { return listenPort_; }
    // Bound data port while a readout is active, 0 otherwise.
    u16 localPort() const { return localPort_; }
    int socketReceiveBufferSize() const { return receiveBufferSize_; }

    // Counters of the current or last run.
    ReadoutWorkerCounters getCounters() const;

    SourceStatsCollector &stats() { return *stats_; }
    MdllHistogrammer &mdllHistos() { return *mdllHistos_; }
    McpdHistogrammer &mcpdHistos() { return *mcpdHistos_; }

    void clearHistograms();
    void resetStats() { stats_->reset(); }
    // Forgets all sources and their stats and histograms.
    void clearSources();

  private:
    void startWorker(std::unique_ptr<PacketSource> source, std::unique_ptr<ListfileWriter> listfile);

    u16 listenPort_;
    u16 localPort_ = 0;
    int receiveBufferSize_ = 0;
    std::shared_ptr<SourceStatsCollector> stats_ = std::make_shared<SourceStatsCollector>();
    std::shared_ptr<MdllHistogrammer> mdllHistos_ = std::make_shared<MdllHistogrammer>();
    std::shared_ptr<McpdHistogrammer> mcpdHistos_ = std::make_shared<McpdHistogrammer>();
    std::unique_ptr<ReadoutWorker> worker_;
};

} // namespace mesytec::mcpd

#endif /* A7D3E9C4_1B5F_4C2A_8D6E_3F9B0A4C7E12 */
