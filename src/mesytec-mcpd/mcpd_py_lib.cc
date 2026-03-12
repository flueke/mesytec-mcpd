#include "mcpd_py_lib.h"
#include <memory>
#include <mesytec-mcpd/util/logging.h>
#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace mesytec::mcpd
{

Readout::Readout(int listenPort, size_t packetBufferMaxSize)
    : listenPort_(listenPort)
{
    spdlog::debug("{}: listenPort={}", PRETTY_FUNCTION, listenPort_);
    packetBuffer_.lock()->reserve(packetBufferMaxSize);
}

Readout::~Readout()
{
    stop();
}

bool Readout::start()
{
    spdlog::debug("{}", PRETTY_FUNCTION);

    std::unique_lock<std::mutex> lock(startStopMutex_);

    if (isRunning_())
    {
        spdlog::warn("{}: already running, not starting again", PRETTY_FUNCTION);
        return false;
    }

    keepRunning_ = true;
    counters_ = {};
    *readoutException_.lock() = nullptr;

    std::promise<bool> promise;
    auto f = promise.get_future();

    std::unique_ptr<py::gil_scoped_release> gil_release;
    if (PyGILState_Check())
        gil_release = std::make_unique<py::gil_scoped_release>();

    spdlog::debug("{}: starting readout thread", PRETTY_FUNCTION);
    readoutThread_ = std::thread(&Readout::readoutLoop, this, std::move(promise));
    spdlog::debug("{}: readout thread started, returning", PRETTY_FUNCTION);

    // If we just return f.get() here it immediately throws any exceptions but
    // the readout thread will still be running for a bit. So to the outside
    // isRunning() will still return true. To fix this we immediately join the
    // thread in here if an exception was thrown.
    try
    {
        spdlog::debug("{}: readout thread startup completed, waiting for result", PRETTY_FUNCTION);
        auto result = f.get();
        spdlog::debug("{}: readout thread startup completed successfully, returning", PRETTY_FUNCTION);
        return result;
    }
    catch (...)
    {
        if (readoutThread_.joinable())
        {
            spdlog::debug("{}: joining readout thread after exception in startup", PRETTY_FUNCTION);
            readoutThread_.join();
            spdlog::debug("{}: readout thread joined after exception in startup, rethrowing", PRETTY_FUNCTION);
        }
        throw; // rethrow
    }
}

bool Readout::stop()
{
    std::unique_lock<std::mutex> lock(startStopMutex_);

    if (!isRunning_())
    {
        return false;
    }

    keepRunning_ = false;

    if (readoutThread_.joinable())
    {
        std::unique_ptr<py::gil_scoped_release> gil_release;
        if (PyGILState_Check())
            gil_release = std::make_unique<py::gil_scoped_release>();

        readoutThread_.join();
        spdlog::debug("{}: readout thread joined, returning", PRETTY_FUNCTION);
        return true;
    }
    else
    {
        // This should not happen because we checked isRunning_() above.
        return false;
    }
}

void Readout::readoutLoop(std::promise<bool> promise)
{
    spdlog::debug("entering {}", PRETTY_FUNCTION);
    int dataSock = -1;

    try
    {
        std::error_code ec;
        dataSock = create_bound_udp_socket(listenPort_, &ec);

        if (ec)
        {
            spdlog::error("{}: failed to create and bind UDP socket to port {}, ec={}",
                          PRETTY_FUNCTION, listenPort_, ec.message());
            throw std::system_error(ec);
        }
        else
        {
            spdlog::debug("{}: created and bound UDP socket to port {}, sockfd={}, ec={}",
                          PRETTY_FUNCTION, listenPort_, dataSock, ec.message());
        }

        ec = set_socket_read_timeout(dataSock, 100); // ms

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
            u16 localPort = get_local_socket_port(dataSock);
            spdlog::info("{}: listening for data on port {}", PRETTY_FUNCTION, localPort);
        }

        promise.set_value(true); // unblock the caller waiting for startup to complete
    }
    catch (const std::exception &e)
    {
        if (dataSock != -1)
            close_socket(dataSock);
        promise.set_exception(std::current_exception());
        spdlog::warn("readout thread startup failed with exception: {}", e.what());
        return;
    }

    try
    {
        while (keepRunning_.load(std::memory_order_relaxed))
        {
            size_t bytesTransferred = 0u;
            sockaddr_in srcAddr = {};
            DataPacket dataPacket = {};

            auto ec = receive_one_packet(dataSock, reinterpret_cast<u8 *>(&dataPacket),
                                         sizeof(dataPacket), bytesTransferred,
                                         DefaultReadTimeout_ms, &srcAddr);

            if (ec)
            {
                if (ec != SocketErrorType::Timeout)
                    throw std::system_error(ec);

                counters_.lock()->timeouts++;
            }
            else if (bytesTransferred)
            {
                packetBuffer_.lock()->push_back(std::move(dataPacket));
                auto counters = counters_.lock();
                counters->packets++;
                counters->bytes += bytesTransferred;
                counters->events += get_event_count(dataPacket);
            }
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("{}: readout loop exiting with exception: {}", PRETTY_FUNCTION, e.what());
        *readoutException_.lock() = std::current_exception();
    }

    if (dataSock != -1)
        close_socket(dataSock);

    spdlog::debug("exiting {}", PRETTY_FUNCTION);
}

bool Readout::isRunning_() const { return readoutThread_.joinable(); }

bool Readout::isRunning() const
{
    std::lock_guard<std::mutex> lock(startStopMutex_);
    return isRunning_();
}

std::vector<DataPacket> Readout::getPackets()
{
    auto packetBuffer = packetBuffer_.lock();
    auto result = std::move(*packetBuffer);
    *packetBuffer = {};
    packetBuffer->reserve(1024);
    spdlog::debug("{}: returning {} packets", PRETTY_FUNCTION, result.size());
    return result;
}

ReadoutCounters Readout::getCounters() { return *counters_.lock(); }

bool Readout::hasReadoutException() const { return *readoutException_.lock() != nullptr; }

std::exception_ptr Readout::getReadoutException() { return *readoutException_.lock(); }

void Readout::rethrowReadoutException()
{
    auto excPtr = *readoutException_.lock();
    if (excPtr)
        std::rethrow_exception(excPtr);
}


} // namespace mesytec::mcpd
