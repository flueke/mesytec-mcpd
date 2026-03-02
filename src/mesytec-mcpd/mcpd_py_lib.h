#ifndef E7B93B2B_DB43_49A2_A29F_480864086D05
#define E7B93B2B_DB43_49A2_A29F_480864086D05

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <mesytec-mcpd/mesytec-mcpd.h>

namespace mesytec::mcpd
{

struct ReadoutCounters
{
    size_t packets = 0u;
    size_t bytes = 0u;
    size_t timeouts = 0u;
    size_t events = 0u;
};

class Readout
{
public:
    explicit Readout(int listenPort = McpdDefaultPort)
        : listenPort_(listenPort)
    {
        spdlog::debug("{}: listenPort={}", __PRETTY_FUNCTION__, listenPort_);
        packetBuffer_.reserve(1024);
    }

    ~Readout()
    {
        spdlog::debug("{}", __PRETTY_FUNCTION__);
        stop();
    }

    void start()
    {
        spdlog::debug("{}", __PRETTY_FUNCTION__);

        std::lock_guard<std::mutex> lock(mutex_);
        // Do not call isRunning() here as we do hold and need to hold the lock.
        if (readoutThread_.joinable())
        {
            spdlog::warn("{}: already running, not starting again", __PRETTY_FUNCTION__);
            return;
        }

        quit_ = false;
        counters_ = {};
        readoutException_ = nullptr;
        spdlog::debug("{}: starting readout thread", __PRETTY_FUNCTION__);
        readoutThread_ = std::thread(&Readout::readoutLoop, this);
        spdlog::debug("{}: readout thread started, returning", __PRETTY_FUNCTION__);
    }

    void stop()
    {
        spdlog::debug("{}", __PRETTY_FUNCTION__);

        std::lock_guard<std::mutex> lock(mutex_);

        if (!readoutThread_.joinable())
            return;

        spdlog::debug("{}: stopping readout", __PRETTY_FUNCTION__);
        quit_ = true;

        if (readoutThread_.joinable())
        {
            spdlog::debug("{}: joining readout thread", __PRETTY_FUNCTION__);
            readoutThread_.join();
        }
        else
        {
            spdlog::warn("{}: readout thread not joinable", __PRETTY_FUNCTION__);
        }
    }

    bool isRunning() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto ret = readoutThread_.joinable();
        spdlog::debug("{}, result={}", __PRETTY_FUNCTION__, ret);
        return ret;
    }

    std::vector<DataPacket> getPackets()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto ret = std::move(packetBuffer_);
        packetBuffer_ = {};
        packetBuffer_.reserve(1024);
        spdlog::debug("{}: returning {} packets", __PRETTY_FUNCTION__, ret.size());
        return ret;
    }

    ReadoutCounters getCounters()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        spdlog::debug("{}", __PRETTY_FUNCTION__);
        return counters_;
    }

    bool hasReadoutException() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto ret = readoutException_ != nullptr;
        spdlog::debug("{}, result={}", __PRETTY_FUNCTION__, ret);
        return ret;
    }

    std::exception_ptr getReadoutException()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        spdlog::debug("{}", __PRETTY_FUNCTION__);
        return readoutException_;
    }

private:
    int listenPort_ = McpdDefaultPort;

    std::atomic<bool> quit_ = false;
    std::thread readoutThread_;

    mutable std::mutex mutex_; // protects counters_, packetBuffer_ and readoutException_
    ReadoutCounters counters_;
    std::vector<DataPacket> packetBuffer_;
    std::exception_ptr readoutException_;

    Readout(const Readout &) = delete;
    Readout &operator=(const Readout &) = delete;
    Readout(Readout &&) = delete;
    Readout &operator=(Readout &&) = delete;

    void readoutLoop()
    {
        spdlog::debug("entering {}", __PRETTY_FUNCTION__);
        int dataSock = -1;

        try
        {
            std::error_code ec;
            dataSock = create_bound_udp_socket(listenPort_, &ec);

            if (ec)
            {
                spdlog::error("{}: failed to create and bind UDP socket to port {}, ec={}", __PRETTY_FUNCTION__, listenPort_, ec.message());
                throw std::system_error(ec);
            }
            else
            {
                spdlog::debug("{}: created and bound UDP socket to port {}, sockfd={}, ec={}", __PRETTY_FUNCTION__, listenPort_, dataSock, ec.message());
            }

            ec = set_socket_read_timeout(dataSock, 100); // ms

            if (ec)
            {
                spdlog::error("{}: failed to set socket read timeout, ec={}", __PRETTY_FUNCTION__, ec.message());
                throw std::system_error(ec);
            }
            else
            {
                spdlog::debug("{}: set socket read timeout to 100ms, ec={}", __PRETTY_FUNCTION__, ec.message());
            }

            {
                u16 localPort = get_local_socket_port(dataSock);
                spdlog::info("{}: listening for data on port {}", __PRETTY_FUNCTION__, localPort);
            }

            while (!quit_)
            {
                size_t bytesTransferred = 0u;
                sockaddr_in srcAddr = {};
                DataPacket dataPacket = {};

                auto ec = receive_one_packet(
                    dataSock,
                    reinterpret_cast<u8 *>(&dataPacket), sizeof(dataPacket),
                    bytesTransferred, DefaultReadTimeout_ms, &srcAddr);

                if (ec)
                {
                    if (ec != SocketErrorType::Timeout)
                        throw std::system_error(ec);

                    std::lock_guard<std::mutex> lock(mutex_);
                    ++counters_.timeouts;
                }
                else if (bytesTransferred)
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    packetBuffer_.push_back(dataPacket);
                    ++counters_.packets;
                    counters_.bytes += bytesTransferred;
                    counters_.events += get_event_count(dataPacket);
                }
            }
        }
        catch (const std::exception &e)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            readoutException_ = std::current_exception();
            spdlog::warn("readout thread exiting with exception: {}", e.what());
        }

        if (dataSock != -1)
        {
            close_socket(dataSock);
        }

        spdlog::debug("exiting {}", __PRETTY_FUNCTION__);
    }
};

}

#endif /* E7B93B2B_DB43_49A2_A29F_480864086D05 */
