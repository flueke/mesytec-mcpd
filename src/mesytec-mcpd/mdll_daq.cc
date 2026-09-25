#include "mdll_daq.h"

#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <system_error>

#ifdef SOCKET_PLATFORM_POSIX
#include <sys/socket.h>
#else
#include <winsock2.h>
#endif

#include <mesytec-mcpd/util/logging.h>

namespace mesytec::mcpd::py_lib
{

namespace
{
constexpr size_t DataPacketHeaderWords = (sizeof(DataPacket) - sizeof(DataPacket::data)) / sizeof(u16);
static_assert(DataPacketHeaderWords == 21);
constexpr int SocketReadTimeout_ms = 100;
constexpr int RequestedReceiveBufferSize = 16 * 1024 * 1024;
constexpr size_t ListfileBufferSize = 1024 * 1024;

int set_receive_buffer_size(int sock, int size)
{
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&size), sizeof(size));

    int result = 0;
#ifdef SOCKET_PLATFORM_POSIX
    socklen_t len = sizeof(result);
#else
    int len = sizeof(result);
#endif
    if (getsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char *>(&result), &len) != 0)
        return 0;
    return result;
}
} // namespace

void MdllHistograms::clear()
{
    for (auto *v: {&amplitude, &x, &y, &xy})
        std::fill(v->begin(), v->end(), 0u);
}

MdllDaq::MdllDaq(u16 listenPort)
    : listenPort_(listenPort)
{
}

MdllDaq::~MdllDaq() { stop(); }

void MdllDaq::startReadout(const std::string &listfilePath, bool overwriteListfile)
{
    joinWorker();

    if (!listfilePath.empty())
    {
        if (!overwriteListfile && std::filesystem::exists(listfilePath))
            throw std::runtime_error(fmt::format("Listfile '{}' already exists", listfilePath));

        listfileBuffer_.resize(ListfileBufferSize);
        listfileOut_ = std::ofstream();
        listfileOut_.rdbuf()->pubsetbuf(listfileBuffer_.data(), listfileBuffer_.size());
        listfileOut_.open(listfilePath, std::ios::out | std::ios::binary | std::ios::trunc);

        if (!listfileOut_)
            throw std::runtime_error(fmt::format("Failed to open listfile '{}' for writing: {}",
                                                 listfilePath, std::strerror(errno)));

        listfileOut_.exceptions(std::ios::failbit | std::ios::badbit);
    }

    std::error_code ec;
    dataSocket_ = create_bound_udp_socket(listenPort_, &ec);

    if (ec)
    {
        listfileOut_.close();
        throw std::system_error(ec, fmt::format("Failed to bind UDP port {}", listenPort_));
    }

    set_socket_read_timeout(dataSocket_, SocketReadTimeout_ms);
    rcvBufSize_ = set_receive_buffer_size(dataSocket_, RequestedReceiveBufferSize);
    localPort_ = get_local_socket_port(dataSocket_);

    spdlog::info("MdllDaq: listening on port {}, socket receive buffer size = {} bytes",
                 localPort_.load(), rcvBufSize_);

    if (rcvBufSize_ < RequestedReceiveBufferSize)
        spdlog::warn("MdllDaq: socket receive buffer smaller than requested ({} < {}). On linux "
                     "raise net.core.rmem_max to avoid packet loss at high rates.",
                     rcvBufSize_, RequestedReceiveBufferSize);

    running_ = true;
    worker_ = std::thread(&MdllDaq::runLoop, this, &MdllDaq::readoutLoop);
}

void MdllDaq::startReplay(const std::string &listfilePath)
{
    joinWorker();

    listfileIn_ = std::ifstream(listfilePath, std::ios::in | std::ios::binary);

    if (!listfileIn_)
        throw std::runtime_error(fmt::format("Failed to open listfile '{}' for reading: {}",
                                             listfilePath, std::strerror(errno)));

    running_ = true;
    worker_ = std::thread(&MdllDaq::runLoop, this, &MdllDaq::replayLoop);
}

void MdllDaq::stop()
{
    quit_ = true;
    joinWorker();
}

void MdllDaq::joinWorker()
{
    if (worker_.joinable())
        worker_.join();

    quit_ = false;
    running_ = false;

    if (dataSocket_ >= 0)
    {
        close_socket(dataSocket_);
        dataSocket_ = -1;
    }

    localPort_ = 0;

    if (listfileOut_.is_open())
    {
        try
        {
            listfileOut_.close();
        }
        catch (const std::exception &e)
        {
            spdlog::error("MdllDaq: error closing listfile: {}", e.what());
        }
    }

    if (listfileIn_.is_open())
        listfileIn_.close();
}

bool MdllDaq::hasException() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return exception_ != nullptr;
}

void MdllDaq::rethrowException()
{
    std::exception_ptr ex;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        std::swap(ex, exception_);
    }
    if (ex)
        std::rethrow_exception(ex);
}

void MdllDaq::runLoop(void (MdllDaq::*loop)())
{
    try
    {
        (this->*loop)();
    }
    catch (const std::exception &e)
    {
        spdlog::error("MdllDaq: worker exiting with exception: {}", e.what());
        std::lock_guard<std::mutex> guard(mutex_);
        exception_ = std::current_exception();
    }

    running_ = false;
}

void MdllDaq::readoutLoop()
{
    DataPacket packet = {};

    while (!quit_)
    {
        size_t bytes = 0;
        sockaddr_in srcAddr = {};

        auto ec = receive_one_packet(dataSocket_, reinterpret_cast<u8 *>(&packet), sizeof(packet),
                                     bytes, SocketReadTimeout_ms, &srcAddr);

        if (ec)
        {
            if (ec == SocketErrorType::Timeout || ec == std::errc::interrupted)
            {
                std::lock_guard<std::mutex> guard(mutex_);
                ++counters_.timeouts;
                continue;
            }

            throw std::system_error(ec, "MdllDaq: receive error");
        }

        if (bytes < sizeof(packet))
            std::memset(reinterpret_cast<u8 *>(&packet) + bytes, 0, sizeof(packet) - bytes);

        if (listfileOut_.is_open())
            listfileOut_.write(reinterpret_cast<const char *>(&packet), sizeof(packet));

        processPacket(packet, bytes, ntohl(srcAddr.sin_addr.s_addr));
    }
}

void MdllDaq::replayLoop()
{
    DataPacket packet = {};

    while (!quit_)
    {
        listfileIn_.read(reinterpret_cast<char *>(&packet), sizeof(packet));

        if (static_cast<size_t>(listfileIn_.gcount()) != sizeof(packet))
        {
            if (listfileIn_.gcount() > 0)
                spdlog::warn("MdllDaq: ignoring {} trailing bytes at end of listfile",
                             listfileIn_.gcount());
            spdlog::info("MdllDaq: replay reached end of listfile");
            break;
        }

        processPacket(packet, sizeof(packet), 0);
    }
}

void MdllDaq::processPacket(DataPacket &packet, size_t bytes, u32 srcAddr)
{
    namespace ec = event_constants;

    std::lock_guard<std::mutex> guard(mutex_);

    ++counters_.packets;
    counters_.bytes += bytes;

    if (listfileOut_.is_open())
        counters_.listfileBytes += sizeof(packet);

    const bool valid = bytes >= DataPacketHeaderWords * sizeof(u16)
        && packet.bufferLength * sizeof(u16) <= bytes
        && packet.headerLength <= packet.bufferLength
        && static_cast<size_t>(get_data_length(packet)) <= DataPacketMaxDataWords;

    if (!valid)
    {
        ++counters_.invalidPackets;
        return;
    }

    if (packet.bufferType != MdllDataBufferType)
    {
        ++counters_.nonMdllPackets;
        return;
    }

    auto &dev = devices_[MdllDeviceKey{srcAddr, packet.deviceId}];
    auto &stats = dev.stats;
    auto &histos = *dev.histos;

    if (stats.haveBufferNumber)
    {
        const u16 gap = packet.bufferNumber - stats.lastBufferNumber - 1u;
        if (gap < 0x8000u)
            stats.packetsLost += gap;
        else
            ++stats.bufferNumberJumps;
    }

    stats.lastBufferNumber = packet.bufferNumber;
    stats.haveBufferNumber = true;
    ++stats.packets;
    stats.bytes += bytes;
    stats.lastDeviceStatus = packet.deviceStatus;
    stats.lastRunId = packet.runId;
    stats.lastHeaderTimestamp = get_header_timestamp(packet);
    stats.lastParams = get_parameter_values(packet);

    const size_t eventCount = get_event_count(packet);
    stats.events += eventCount;

    for (size_t i = 0; i < eventCount; ++i)
    {
        const u16 *w = packet.data + i * 3;
        const u64 event = to_48bit_value(w[0], w[1], w[2]);

        if (static_cast<EventType>((event >> ec::IdShift) & ec::IdMask) != EventType::Neutron)
        {
            ++stats.triggerEvents;
            continue;
        }

        ++stats.neutronEvents;

        const auto amp = (event >> ec::mdll_neutron::AmplitudeShift) & ec::mdll_neutron::AmplitudeMask;
        const auto x = (event >> ec::mdll_neutron::xPosShift) & ec::mdll_neutron::xPosMask;
        const auto y = (event >> ec::mdll_neutron::yPosShift) & ec::mdll_neutron::yPosMask;

        ++histos.amplitude[amp];
        ++histos.x[x];
        ++histos.y[y];
        ++histos.xy[y * MdllHistograms::XBins + x];
    }
}

MdllDaqCounters MdllDaq::getCounters() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return counters_;
}

std::map<MdllDeviceKey, MdllDeviceStats> MdllDaq::getDeviceStats() const
{
    std::map<MdllDeviceKey, MdllDeviceStats> result;
    std::lock_guard<std::mutex> guard(mutex_);
    for (const auto &[key, dev]: devices_)
        result[key] = dev.stats;
    return result;
}

std::optional<MdllHistograms> MdllDaq::getHistograms(const MdllDeviceKey &key) const
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (auto it = devices_.find(key); it != devices_.end())
        return *it->second.histos;
    return std::nullopt;
}

void MdllDaq::clearHistograms()
{
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto &[key, dev]: devices_)
        dev.histos->clear();
}

void MdllDaq::resetStats()
{
    std::lock_guard<std::mutex> guard(mutex_);
    counters_ = {};
    for (auto &[key, dev]: devices_)
        dev.stats = {};
}

void MdllDaq::clearDevices()
{
    std::lock_guard<std::mutex> guard(mutex_);
    devices_.clear();
}

} // namespace mesytec::mcpd::py_lib
