#ifndef E7B93B2B_DB43_49A2_A29F_480864086D05
#define E7B93B2B_DB43_49A2_A29F_480864086D05

#include <condition_variable>
#include <exception>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <pybind11/pybind11.h>
#include <thread>
#include <vector>

#include <mesytec-mcpd/mesytec-mcpd.h>
#include <mesytec-mcpd/util/locked_ptr.h>

namespace mesytec::mcpd::py_lib
{

namespace py = pybind11;

struct Counters
{
    u64 packets = 0u;
    u64 bytes = 0u;
    u64 timeouts = 0u;
    u64 events = 0u;
    u64 packetsLost = 0u;
    u64 packetsDropped = 0u;
};

const size_t DefaultQueueSize = 1000;

struct AugmentedDataPacket
{
    DataPacket packet;
    u32 srcAddr;
    u16 srcPort;
    mutable std::vector<u64> rawEvents; // buffer for raw event data, allocated on demand

    py::buffer_info getBufferInfo() const
    {
        if (rawEvents.empty())
        {
            const auto eventCount = get_event_count(packet);
            rawEvents.reserve(eventCount);

            for (size_t i = 0; i < eventCount; ++i)
                rawEvents.push_back(get_event(packet, i));
        }

        return py::buffer_info(
            rawEvents.data(),           // Pointer to buffer
            sizeof(u64),               // Size of one scalar
            py::format_descriptor<u64>::format(), // Python struct-style format descriptor
            1,                         // Number of dimensions
            {rawEvents.size()},        // Buffer dimensions
            {sizeof(u64)}              // Strides (in bytes) for each index
        );
    }
};

class WorkerBase
{
  public:
    explicit WorkerBase(size_t queueSize = DefaultQueueSize);
    virtual ~WorkerBase();

    bool start();
    bool stop(bool immediate = false);
    bool isRunning() const;
    bool hasException() const;
    void rethrowException();

    py::object getQueue() const { return queue_; }

    Counters getCounters() const { return *counters_.lock(); }
    void resetCounters() { *counters_.lock() = Counters{}; }

  protected:
    virtual void workerLoop(std::promise<bool> promise) = 0;
    locked_ptr<Counters> &getCounters_() { return counters_; }

  private:
    void workerLoop_(std::promise<bool> promise);
    bool isRunning_() const { return workerThread_.joinable(); }

    WorkerBase(const WorkerBase &) = delete;
    WorkerBase &operator=(const WorkerBase &) = delete;
    WorkerBase(WorkerBase &&) = delete;
    WorkerBase &operator=(WorkerBase &&) = delete;

    std::thread workerThread_;

    mutable locked_ptr<Counters> counters_ = locked_ptr<Counters>(std::in_place);

    mutable locked_ptr<std::exception_ptr> readoutException_ =
        locked_ptr<std::exception_ptr>(std::in_place);

    mutable std::mutex startStopMutex_;

    py::object queue_;
};

class Readout: public WorkerBase
{
  public:
    explicit Readout(int listenPort = McpdDefaultPort, size_t queueSize = DefaultQueueSize);

  protected:
    void workerLoop(std::promise<bool> promise) override;

  private:
    int listenPort_ = McpdDefaultPort;
    int dataSocket_ = -1;
};

class Replay: public WorkerBase
{
  public:
    explicit Replay(size_t queueSize = DefaultQueueSize);
    explicit Replay(const std::string &filename,
                    size_t queueSize = DefaultQueueSize);

  protected:
    void workerLoop(std::promise<bool> promise) override;

  private:
    std::string filename_;
    std::ifstream inputFile_;
};

} // namespace mesytec::mcpd::py_lib

#endif /* E7B93B2B_DB43_49A2_A29F_480864086D05 */
