#include "mcpd_py_lib.h"

#include <optional>

#include <mesytec-mcpd/util/logging.h>

namespace py = pybind11;

namespace mesytec::mcpd::py_lib
{

namespace
{
// WorkerBase methods may be called with or without the GIL held (python
// bindings vs. C++ tests). They must not hold it while waiting on the mutex or
// joining the worker, because the worker thread needs it to put packets into
// the queue.
struct ReleaseGilIfHeld
{
    std::optional<py::gil_scoped_release> release;
    ReleaseGilIfHeld()
    {
        if (PyGILState_Check())
            release.emplace();
    }
};
} // namespace

PyQueueTap::PyQueueTap(py::handle queue, bool blocking)
    : queue_(queue)
    , blocking_(blocking)
{
}

void PyQueueTap::consume(const ReceivedPacket &rp)
{
    if (shutDown_)
        return;

    py::gil_scoped_acquire gil;

    try
    {
        AugmentedDataPacket aug{rp.packet, rp.srcAddr, rp.srcPort, {}};
        queue_.attr("put")(std::move(aug), blocking_);
    }
    catch (py::error_already_set &e)
    {
        auto queueModule = py::module_::import("queue");

        if (e.matches(queueModule.attr("ShutDown")))
            shutDown_ = true;
        else if (e.matches(queueModule.attr("Full")))
            ++dropped_;
        else
            throw;
    }
}

void PyQueueTap::finished()
{
    py::gil_scoped_acquire gil;
    queue_.attr("shutdown")(false);
}

WorkerBase::WorkerBase(size_t queueSize)
    : queueSize_(queueSize)
    , queue_(py::module_::import("queue").attr("Queue")(queueSize))
{
}

WorkerBase::~WorkerBase()
{
    ReleaseGilIfHeld release;
    stop(true);
}

bool WorkerBase::start()
{
    ReleaseGilIfHeld release;
    std::lock_guard<std::mutex> lock(mutex_);

    if (worker_ && worker_->isRunning())
    {
        spdlog::warn("{}: already running, not starting again", PRETTY_FUNCTION);
        return false;
    }

    auto source = makeSource();

    {
        py::gil_scoped_acquire gil;
        if (queueUsed_)
            queue_ = py::module_::import("queue").attr("Queue")(queueSize_);
        queueUsed_ = true;
    }

    // Join a previous worker that ended by itself (end of replay, exception).
    worker_.reset();

    stats_ = std::make_shared<SourceStatsCollector>();
    tap_ = std::make_shared<PyQueueTap>(queue_, blockingQueue());
    worker_ = std::make_unique<ReadoutWorker>(
        std::move(source), nullptr, std::vector<std::shared_ptr<PacketConsumer>>{stats_, tap_});
    worker_->start();
    return true;
}

bool WorkerBase::stop(bool immediate)
{
    ReleaseGilIfHeld release;
    std::lock_guard<std::mutex> lock(mutex_);

    if (!worker_)
        return false;

    const bool wasRunning = worker_->isRunning();

    {
        // Wakes up a worker blocked in queue.put() and consumers blocked in get().
        py::gil_scoped_acquire gil;
        queue_.attr("shutdown")(immediate);
    }

    worker_->stop();
    return wasRunning;
}

bool WorkerBase::isRunning() const
{
    ReleaseGilIfHeld release;
    std::lock_guard<std::mutex> lock(mutex_);
    return worker_ && worker_->isRunning();
}

bool WorkerBase::hasException() const
{
    ReleaseGilIfHeld release;
    std::lock_guard<std::mutex> lock(mutex_);
    return worker_ && worker_->hasException();
}

void WorkerBase::rethrowException()
{
    ReleaseGilIfHeld release;
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_)
        worker_->rethrowException();
}

Counters WorkerBase::getCounters() const
{
    ReleaseGilIfHeld release;
    std::lock_guard<std::mutex> lock(mutex_);

    if (!worker_)
        return {};

    const auto wc = worker_->getCounters();
    const auto totals = stats_->getTotals();

    Counters result;
    result.packets = wc.packets;
    result.bytes = wc.bytes;
    result.timeouts = wc.timeouts;
    result.events = totals.events;
    result.packetsLost = totals.packetsLost;
    result.packetsDropped = tap_->dropped();
    return result;
}

Readout::Readout(int listenPort, size_t queueSize)
    : WorkerBase(queueSize)
    , listenPort_(listenPort)
{
}

std::unique_ptr<PacketSource> Readout::makeSource()
{
    auto source = std::make_unique<UdpPacketSource>(listenPort_);
    localPort_ = source->localPort();
    return source;
}

Replay::Replay(size_t queueSize)
    : WorkerBase(queueSize)
{
}

Replay::Replay(const std::string &filename, size_t queueSize)
    : WorkerBase(queueSize)
    , filename_(filename)
{
}

std::unique_ptr<PacketSource> Replay::makeSource()
{
    return std::make_unique<ListfilePacketSource>(filename_);
}

} // namespace mesytec::mcpd::py_lib
