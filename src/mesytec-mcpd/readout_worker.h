#ifndef D41F7C2B_9A6E_4E3D_B8C1_5F0A2E6D9B47
#define D41F7C2B_9A6E_4E3D_B8C1_5F0A2E6D9B47

// Readout/replay thread: reads packets from a PacketSource, optionally writes
// them to a listfile and passes valid packets to a list of consumers.

#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "packet_io.h"

namespace mesytec::mcpd
{

// Consumers are called from the worker thread. Implementations that are also
// queried from other threads have to do their own locking.
class MESYTEC_MCPD_EXPORT PacketConsumer
{
  public:
    virtual ~PacketConsumer() = default;

    // Called for every packet that passed is_valid_data_packet().
    virtual void consume(const ReceivedPacket &rp) = 0;

    // Called once when the worker loop exits: stop requested, end of data or
    // an exception.
    virtual void finished() {}
};

struct MESYTEC_MCPD_EXPORT ReadoutWorkerCounters
{
    u64 packets = 0;         // all packets received/read, including invalid ones
    u64 bytes = 0;
    u64 timeouts = 0;
    u64 invalidPackets = 0;  // failed is_valid_data_packet()
    u64 listfileBytes = 0;
};

// One instance handles one run: the source is consumed by the worker and
// cannot be restarted. Create a new worker for the next run.
class MESYTEC_MCPD_EXPORT ReadoutWorker
{
  public:
    ReadoutWorker(std::unique_ptr<PacketSource> source,
                  std::unique_ptr<ListfileWriter> listfile,
                  std::vector<std::shared_ptr<PacketConsumer>> consumers);
    ~ReadoutWorker();

    ReadoutWorker(const ReadoutWorker &) = delete;
    ReadoutWorker &operator=(const ReadoutWorker &) = delete;

    void start();
    // Requests the loop to quit and joins the thread. Closes the source (e.g.
    // the UDP socket) and the listfile. Counters stay available.
    void stop();

    // False once the loop exited, i.e. after stop(), at the end of a listfile
    // or when an exception occurred.
    bool isRunning() const { return running_; }

    bool hasException() const;
    // Rethrows and clears the stored exception, if any.
    void rethrowException();

    ReadoutWorkerCounters getCounters() const;

  private:
    void loop();

    std::unique_ptr<PacketSource> source_;
    std::unique_ptr<ListfileWriter> listfile_;
    std::vector<std::shared_ptr<PacketConsumer>> consumers_;

    std::thread thread_;
    std::atomic<bool> quit_ = false;
    std::atomic<bool> running_ = false;

    mutable std::mutex mutex_; // guards counters_ and exception_
    ReadoutWorkerCounters counters_;
    std::exception_ptr exception_;
};

} // namespace mesytec::mcpd

#endif /* D41F7C2B_9A6E_4E3D_B8C1_5F0A2E6D9B47 */
