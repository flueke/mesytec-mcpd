#include "mcpd_py_lib.h"
#include <memory>
#include <mesytec-mcpd/util/logging.h>
#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace mesytec::mcpd::py_lib
{

WorkerBase::WorkerBase(size_t packetBufferMaxPackets)
    : packetBufferMaxPackets_(packetBufferMaxPackets)
{
}

WorkerBase::~WorkerBase() { stop(); }

bool WorkerBase::start()
{
    spdlog::debug("{}", PRETTY_FUNCTION);

    std::unique_lock<std::mutex> lock(startStopMutex_);

    if (isRunning_())
    {
        spdlog::warn("{}: already running, not starting again", PRETTY_FUNCTION);
        return false;
    }

    keepRunning_ = true;
    resetCounters();
    *readoutException_.lock() = nullptr;

    std::promise<bool> promise;
    auto f = promise.get_future();

    std::unique_ptr<py::gil_scoped_release> gil_release;
    if (PyGILState_Check())
        gil_release = std::make_unique<py::gil_scoped_release>();

    spdlog::debug("{}: starting readout thread", PRETTY_FUNCTION);
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

bool WorkerBase::stop()
{
    std::unique_lock<std::mutex> lock(startStopMutex_);

    if (!isRunning_())
    {
        return false;
    }

    keepRunning_ = false;

    if (workerThread_.joinable())
    {
        std::unique_ptr<py::gil_scoped_release> gil_release;
        if (PyGILState_Check())
            gil_release = std::make_unique<py::gil_scoped_release>();

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

u64 WorkerBase::getPacketCount() const { return packetBuffer_.lock()->size(); }

std::vector<DataPacket> WorkerBase::getPackets()
{
    auto packetBuffer = packetBuffer_.lock();
    auto result = std::move(*packetBuffer);
    *packetBuffer = {};
    spdlog::debug("{}: returning {} packets", PRETTY_FUNCTION, result.size());
    return result;
}


Readout::Readout(int listenPort, size_t packetBufferMaxPackets)
    : WorkerBase(packetBufferMaxPackets)
    , listenPort_(listenPort)
{
    spdlog::debug("{}: listenPort={}", PRETTY_FUNCTION, listenPort_);
}

void Readout::workerLoop(std::promise<bool> promise)
{
    spdlog::debug("entering {}", PRETTY_FUNCTION);

    auto cleanup = [this]()
    {
        if (dataSocket_ != -1)
        {
            close_socket(dataSocket_);
            dataSocket_ = -1;
        }
    };

    assert(dataSocket_ == -1); // should not happen as we do proper cleanup :)
    cleanup();                 // in release builds close the socket and try to reopen it later

    try
    {
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
        // Exception from the startup phase: clean up, store the exception, return.
        cleanup();
        promise.set_exception(std::current_exception());
        spdlog::warn("readout thread startup failed with exception: {}", e.what());
        return;
    }

    // TODO: calculate packet loss and update counters.packetsLost
    try
    {
        while (keepRunning())
        {
            size_t bytesTransferred = 0u;
            sockaddr_in srcAddr = {};
            DataPacket dataPacket = {};

            auto ec = receive_one_packet(dataSocket_, reinterpret_cast<u8 *>(&dataPacket),
                                         sizeof(dataPacket), bytesTransferred,
                                         DefaultReadTimeout_ms, &srcAddr);

            if (ec)
            {
                if (ec != SocketErrorType::Timeout)
                    throw std::system_error(ec);

                getCounters_().lock()->timeouts++;
            }
            else if (bytesTransferred)
            {
                auto counters = getCounters_().lock();
                counters->packets++;
                counters->bytes += bytesTransferred;
                counters->events += get_event_count(dataPacket);

                auto packetBuffer = getPacketBuffer().lock();

                if (packetBuffer->size() >= getPacketBufferMaxPackets())
                {
                    counters->packetsDropped++;
                    spdlog::warn("{}: packet buffer full ({} packets), dropping packet",
                                 PRETTY_FUNCTION, packetBuffer->size());
                }
                else
                {
                    packetBuffer->emplace_back(std::move(dataPacket));
                }
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

} // namespace mesytec::mcpd::py_lib
