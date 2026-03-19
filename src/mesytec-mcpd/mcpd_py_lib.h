#ifndef E7B93B2B_DB43_49A2_A29F_480864086D05
#define E7B93B2B_DB43_49A2_A29F_480864086D05

#include <condition_variable>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <mesytec-mcpd/mesytec-mcpd.h>
#include <mesytec-mcpd/util/locked_ptr.h>

namespace mesytec::mcpd::py_lib
{

struct Counters
{
    u64 packets = 0u;
    u64 bytes = 0u;
    u64 timeouts = 0u;
    u64 events = 0u;
    u64 packetsLost = 0u;
    u64 packetsDropped = 0u;
};

const size_t DefaultPacketBufferMaxPackets = 10000;

class WorkerBase
{
  public:
    explicit WorkerBase(size_t packetBufferMaxPackets = DefaultPacketBufferMaxPackets);
    virtual ~WorkerBase();

    bool start();
    bool stop();
    bool isRunning() const;
    bool hasException() const;
    void rethrowException();

    u64 getPacketCount() const;
    std::vector<DataPacket> getPackets();

    Counters getCounters() const { return *counters_.lock(); }
    void resetCounters() { *counters_.lock() = Counters{}; }

  protected:
    virtual void workerLoop(std::promise<bool> promise) = 0;
    locked_ptr<Counters> &getCounters_() { return counters_; }
    bool keepRunning() const { return keepRunning_.load(std::memory_order_relaxed); }
    locked_ptr<std::vector<DataPacket>> &getPacketBuffer() { return packetBuffer_; }
    size_t getPacketBufferMaxPackets() const { return packetBufferMaxPackets_; }

  private:
    void workerLoop_(std::promise<bool> promise);
    bool isRunning_() const { return workerThread_.joinable(); }

    WorkerBase(const WorkerBase &) = delete;
    WorkerBase &operator=(const WorkerBase &) = delete;
    WorkerBase(WorkerBase &&) = delete;
    WorkerBase &operator=(WorkerBase &&) = delete;

    size_t packetBufferMaxPackets_;
    std::atomic<bool> keepRunning_;
    std::thread workerThread_;

    mutable locked_ptr<Counters> counters_ = locked_ptr<Counters>(std::in_place);

    mutable locked_ptr<std::vector<DataPacket>> packetBuffer_ =
        locked_ptr<std::vector<DataPacket>>(std::in_place);

    mutable locked_ptr<std::exception_ptr> readoutException_ =
        locked_ptr<std::exception_ptr>(std::in_place);

    mutable std::mutex startStopMutex_;
};

// Helper class for the python bindings to read out data packets from MCPD/MDLL
// modules.
// Runs its own thread, reading packets in a loop and buffering them. Packets
// can be consumed using getPackets().
//
// Error handling is done via exceptions. The current implementation throws
// std::system_error() to transmit error codes to the outside.

class Readout: public WorkerBase
{
  public:
    explicit Readout(int listenPort = McpdDefaultPort,
                     size_t packetBufferMaxPackets = DefaultPacketBufferMaxPackets);

  protected:
    void workerLoop(std::promise<bool> promise) override;

  private:
    int listenPort_ = McpdDefaultPort;
    int dataSocket_ = -1;
};

} // namespace mesytec::mcpd::py_lib

#endif /* E7B93B2B_DB43_49A2_A29F_480864086D05 */
