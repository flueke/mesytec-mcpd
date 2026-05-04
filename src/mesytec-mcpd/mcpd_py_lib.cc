#include "mcpd_py_lib.h"
#include <cstring>
#include <filesystem>
#include <memory>
#include <mesytec-mcpd/util/logging.h>

namespace py = pybind11;

namespace mesytec::mcpd::py_lib
{

WorkerBase::WorkerBase(size_t queueSize)
{
    py::object Queue = py::module_::import("queue").attr("Queue");
    queue_ = Queue(queueSize);
}

WorkerBase::~WorkerBase()
{
    spdlog::trace("{}: stopping worker thread in destructor", PRETTY_FUNCTION);
    stop(true);
}

bool WorkerBase::start()
{
    spdlog::debug("{}", PRETTY_FUNCTION);

    std::unique_lock<std::mutex> lock(startStopMutex_);

    if (isRunning_())
    {
        spdlog::warn("{}: already running, not starting again", PRETTY_FUNCTION);
        return false;
    }

    resetCounters();
    *readoutException_.lock() = nullptr;

    std::promise<bool> promise;
    auto f = promise.get_future();

    spdlog::debug("{}: starting readout thread", PRETTY_FUNCTION);

    std::unique_ptr<py::gil_scoped_release> gil_release;
    if (PyGILState_Check())
        gil_release = std::make_unique<py::gil_scoped_release>();

    workerThread_ = std::thread(&WorkerBase::workerLoop_, this, std::move(promise));
    spdlog::debug("{}: readout thread started, returning", PRETTY_FUNCTION);

    // In case f.get() throws we have to stop the thread, otherwise isRunning()
    // will still return true, despite the worker being stopped.
    try
    {
        spdlog::debug("{}: worker thread startup completed, waiting for result", PRETTY_FUNCTION);
        auto result = f.get();
        spdlog::debug("{}: worker thread startup completed successfully, returning",
                      PRETTY_FUNCTION);
        return result;
    }
    catch (...)
    {
        if (workerThread_.joinable())
        {
            spdlog::debug("{}: joining readout thread after exception in startup", PRETTY_FUNCTION);
            workerThread_.join();
            spdlog::debug("{}: readout thread joined after exception in startup, rethrowing",
                          PRETTY_FUNCTION);
        }
        throw; // rethrow
    }
}

bool WorkerBase::stop(bool immediate)
{
    std::unique_lock<std::mutex> lock(startStopMutex_);

    auto gil_acquire = std::make_unique<py::gil_scoped_acquire>();

    if (!isRunning_())
    {
        return false;
    }

    queue_.attr("shutdown")(immediate);

    if (workerThread_.joinable())
    {
        py::gil_scoped_release gil_release;
        workerThread_.join();
        spdlog::debug("{}: worker thread joined, returning", PRETTY_FUNCTION);
        return true;
    }
    else
    {
        // This should not happen because we checked isRunning_() above.
        return false;
    }
}

void WorkerBase::workerLoop_(std::promise<bool> promise)
{
    try
    {
        workerLoop(std::move(promise));
    }
    catch (const std::exception &e)
    {
        spdlog::error("{}: worker loop exiting with exception: {}", PRETTY_FUNCTION, e.what());
        *readoutException_.lock() = std::current_exception();
    }
}

bool WorkerBase::isRunning() const
{
    std::lock_guard<std::mutex> lock(startStopMutex_);
    return isRunning_();
}
bool WorkerBase::hasException() const { return *readoutException_.lock() != nullptr; }

void WorkerBase::rethrowException()
{
    if (auto exPtr = readoutException_.lock(); *exPtr)
    {
        // Throw, clear, rethrow
        try
        {
            std::rethrow_exception(*exPtr);
        }
        catch (...)
        {
            *exPtr = {};
            throw;
        }
    }
}

Readout::Readout(int listenPort, size_t queueSize)
    : WorkerBase(queueSize)
    , listenPort_(listenPort)
{
    spdlog::debug("{}: listenPort={}", PRETTY_FUNCTION, listenPort_);
}

void Readout::workerLoop(std::promise<bool> promise)
{
    spdlog::debug("entering {}", PRETTY_FUNCTION);

    // Hold the GIL. We'll release it when it's not needed.
    py::gil_scoped_acquire gil_acquire;
    py::object pyqueue = py::module_::import("queue");

    auto cleanup = [this]()
    {
        if (dataSocket_ != -1)
        {
            close_socket(dataSocket_);
            dataSocket_ = -1;
        }
    };

    assert(dataSocket_ == -1); // would be a cleanup bug
    cleanup();                 // in release builds just close the socket

    try
    {
        py::gil_scoped_release gil_release;

        std::error_code ec;
        dataSocket_ = create_bound_udp_socket(listenPort_, &ec);

        if (ec)
        {
            spdlog::error("{}: failed to create and bind UDP socket to port {}, ec={}",
                          PRETTY_FUNCTION, listenPort_, ec.message());
            throw std::system_error(ec);
        }
        else
        {
            spdlog::debug("{}: created and bound UDP socket to port {}, sockfd={}, ec={}",
                          PRETTY_FUNCTION, listenPort_, dataSocket_, ec.message());
        }

        ec = set_socket_read_timeout(dataSocket_, 100); // ms

        if (ec)
        {
            spdlog::error("{}: failed to set socket read timeout, ec={}", PRETTY_FUNCTION,
                          ec.message());
            throw std::system_error(ec);
        }
        else
        {
            spdlog::debug("{}: set socket read timeout to 100ms, ec={}", PRETTY_FUNCTION,
                          ec.message());
        }

        {
            u16 localPort = get_local_socket_port(dataSocket_);
            spdlog::info("{}: listening for data on port {}", PRETTY_FUNCTION, localPort);
        }

        promise.set_value(true); // unblock the caller waiting for startup to complete
    }
    catch (const std::exception &e)
    {
        // Exception from the startup phase: clean up, store the exception in the promise, return.
        cleanup();
        promise.set_exception(std::current_exception());
        spdlog::warn("readout thread startup failed with exception: {}", e.what());
        return;
    }

    // TODO: calculate packet loss and update counters.packetsLost
    try
    {
        while (true)
        {
            size_t bytesTransferred = 0u;
            sockaddr_in srcAddr = {};
            std::optional<AugmentedDataPacket> augPacket = AugmentedDataPacket{};
            std::error_code ec;

            {
                py::gil_scoped_release gil_release;
                ec = receive_one_packet(dataSocket_, reinterpret_cast<u8 *>(&augPacket->packet),
                                        sizeof(augPacket->packet), bytesTransferred,
                                        DefaultReadTimeout_ms, &srcAddr);

                if (ec)
                {
                    augPacket = std::nullopt;
                    if (ec != SocketErrorType::Timeout)
                        throw std::system_error(ec);

                    getCounters_().lock()->timeouts++;
                }
                else
                {
                    augPacket->srcAddr = ntohl(srcAddr.sin_addr.s_addr);
                    augPacket->srcPort = ntohs(srcAddr.sin_port);

                    auto counters = getCounters_().lock();
                    counters->packets++;
                    counters->bytes += sizeof(augPacket->packet);
                    counters->events += get_event_count(augPacket->packet);
                }
            }

            assert(PyGILState_Check());

            try
            {
                if (augPacket.has_value())
                {
                    getQueue().attr("put")(std::move(augPacket.value()), false); // non-blocking
                }
                else
                {
                    // We have to enqueue something to detect shutdown. There is no other way to query this.
                    getQueue().attr("put")(py::none(), false); // non-blocking
                }
            }
            catch (py::error_already_set &e)
            {
                if (e.matches(pyqueue.attr("ShutDown")))
                    break;
                else
                    spdlog::warn("{}: exception while putting packet into queue: {}", PRETTY_FUNCTION, e.what());

                if (augPacket.has_value())
                    getCounters_().lock()->packetsDropped++;
            }
        }

        cleanup();
    }
    catch (const std::exception &e)
    {
        cleanup();
        throw; // WorkerBase handles it
    }

    spdlog::debug("exiting {}", PRETTY_FUNCTION);
}

Replay::Replay(size_t packetBufferMaxPackets)
    : WorkerBase(packetBufferMaxPackets)
{
}

Replay::Replay(const std::string &filename, size_t queueSize)
    : WorkerBase(queueSize)
    , filename_(filename)
{
}

void Replay::workerLoop(std::promise<bool> promise)
{
    spdlog::debug("entering {}", PRETTY_FUNCTION);

    // Hold the GIL. We'll release it when it's not needed.
    py::gil_scoped_acquire gil_acquire;
    py::object pyqueue = py::module_::import("queue");

    try
    {
        py::gil_scoped_release gil_release;

        if (!inputFile_.is_open())
        {
            inputFile_.open(filename_, std::ios::in | std::ios::binary);
            if (!inputFile_)
            {
                if (!std::filesystem::exists(filename_))
                {
                    throw std::runtime_error(
                        fmt::format("Input file '{}' does not exist", filename_));
                }

                throw std::runtime_error(
                    fmt::format("Failed to open input file '{}' for reading {}", filename_,
                                std::strerror(errno)));
            }
            inputFile_.exceptions(std::ios::badbit);
            spdlog::debug("{}: opened input file '{}'", PRETTY_FUNCTION, filename_);
        }
        else
        {
            spdlog::debug("{}: input file '{}' already open, reopening", PRETTY_FUNCTION,
                          filename_);
            inputFile_.clear();
            inputFile_.seekg(0);
        }

        promise.set_value(true); // unblock the caller waiting for startup to complete
    }
    catch (const std::exception &e)
    {
        promise.set_exception(std::current_exception());
        spdlog::warn("readout thread startup failed with exception: {}", e.what());
        return;
    }

    spdlog::info("{}: replaying from file '{}'", PRETTY_FUNCTION, filename_);

    // TODO: calculate packet loss (recording time loss, not replay loss) and update
    // counters.packetsLost
    try
    {
        while (true)
        {
            AugmentedDataPacket augPacket = {};
            auto bytesRead = getCounters_().lock()->bytes;
            auto mbRead = bytesRead / (1024.0 * 1024.0);
            spdlog::debug(
                "{}: reading packet from file '{}', eof={}, totalBytesRead={} MB ({} bytes)",
                PRETTY_FUNCTION, filename_, inputFile_.eof(), mbRead, bytesRead);

            {
                py::gil_scoped_release gil_release;
                inputFile_.read(reinterpret_cast<char *>(&augPacket.packet),
                                sizeof(augPacket.packet));

                if (inputFile_.eof())
                {
                    spdlog::info("{}: reached end of file, exiting replay loop", PRETTY_FUNCTION);
                    break;
                }

                auto counters = getCounters_().lock();
                counters->packets++;
                counters->bytes += sizeof(augPacket.packet);
                counters->events += get_event_count(augPacket.packet);
            }

            spdlog::debug("{}: read packet from file, bytesTransferred={}", PRETTY_FUNCTION,
                          sizeof(augPacket.packet));

            try
            {
                assert(PyGILState_Check());
                getQueue().attr("put")(std::move(augPacket), true); // blocking
            }
            catch (py::error_already_set &e)
            {
                assert(PyGILState_Check());
                getCounters_().lock()->packetsDropped++;
                if (e.matches(pyqueue.attr("ShutDown")))
                    break;
            }
        }
    }
    catch (const std::exception &e)
    {
        // cleanup();
        throw; // WorkerBase handles it
    }

    spdlog::debug("shutting down queue and exiting {}", PRETTY_FUNCTION);
    getQueue().attr("shutdown")(false);
}

} // namespace mesytec::mcpd::py_lib
