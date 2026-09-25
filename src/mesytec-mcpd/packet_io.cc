#include "packet_io.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <system_error>

#include "util/logging.h"
#include "util/udp_sockets.h"

#ifdef SOCKET_PLATFORM_POSIX
#include <sys/socket.h>
#else
#include <winsock2.h>
#endif

namespace mesytec::mcpd
{

namespace
{
constexpr size_t DataPacketHeaderWords = (sizeof(DataPacket) - sizeof(DataPacket::data)) / sizeof(u16);
static_assert(DataPacketHeaderWords == 21);

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

bool is_valid_data_packet(const ReceivedPacket &rp)
{
    const auto &p = rp.packet;

    return rp.bytes >= DataPacketHeaderWords * sizeof(u16)
        && p.bufferLength * sizeof(u16) <= rp.bytes
        && p.headerLength <= p.bufferLength
        && static_cast<size_t>(get_data_length(p)) <= DataPacketMaxDataWords;
}

UdpPacketSource::UdpPacketSource(u16 listenPort, int readTimeout_ms, int receiveBufferSize)
    : readTimeout_ms_(readTimeout_ms)
{
    std::error_code ec;
    sock_ = create_bound_udp_socket(listenPort, &ec);

    if (ec)
        throw std::system_error(ec, fmt::format("Failed to bind UDP port {}", listenPort));

    if ((ec = set_socket_read_timeout(sock_, readTimeout_ms_)))
    {
        close_socket(sock_);
        throw std::system_error(ec, "Failed to set socket read timeout");
    }

    receiveBufferSize_ = set_receive_buffer_size(sock_, receiveBufferSize);
    localPort_ = get_local_socket_port(sock_);

    spdlog::info("UdpPacketSource: listening on port {}, socket receive buffer size = {} bytes",
                 localPort_, receiveBufferSize_);

    if (receiveBufferSize_ < receiveBufferSize)
        spdlog::warn("UdpPacketSource: socket receive buffer smaller than requested ({} < {}). On "
                     "linux raise net.core.rmem_max to avoid packet loss at high rates.",
                     receiveBufferSize_, receiveBufferSize);
}

UdpPacketSource::~UdpPacketSource()
{
    if (sock_ >= 0)
        close_socket(sock_);
}

PacketSource::Result UdpPacketSource::read(ReceivedPacket &dest)
{
    sockaddr_in srcAddr = {};
    size_t bytes = 0;

    auto ec = receive_one_packet(sock_, reinterpret_cast<u8 *>(&dest.packet), sizeof(dest.packet),
                                 bytes, readTimeout_ms_, &srcAddr);

    if (ec)
    {
        if (ec == SocketErrorType::Timeout || ec == std::errc::interrupted)
            return Result::Timeout;
        throw std::system_error(ec, "UdpPacketSource: receive error");
    }

    // Zero the unused tail so that listfile records never contain stale data.
    if (bytes < sizeof(dest.packet))
        std::memset(reinterpret_cast<u8 *>(&dest.packet) + bytes, 0, sizeof(dest.packet) - bytes);

    dest.bytes = bytes;
    dest.srcAddr = ntohl(srcAddr.sin_addr.s_addr);
    dest.srcPort = ntohs(srcAddr.sin_port);
    return Result::Packet;
}

ListfilePacketSource::ListfilePacketSource(const std::string &filename)
    : filename_(filename)
    , file_(filename, std::ios::in | std::ios::binary)
{
    if (!file_)
        throw std::runtime_error(fmt::format("Failed to open listfile '{}' for reading: {}",
                                             filename, std::strerror(errno)));
}

PacketSource::Result ListfilePacketSource::read(ReceivedPacket &dest)
{
    file_.read(reinterpret_cast<char *>(&dest.packet), sizeof(dest.packet));
    const auto count = static_cast<size_t>(file_.gcount());

    if (count != sizeof(dest.packet))
    {
        if (file_.bad())
            throw std::runtime_error(fmt::format("Error reading listfile '{}'", filename_));
        if (count > 0)
            spdlog::warn("ListfilePacketSource: ignoring {} trailing bytes at end of '{}'", count,
                         filename_);
        return Result::EndOfData;
    }

    dest.bytes = sizeof(dest.packet);
    dest.srcAddr = 0;
    dest.srcPort = 0;
    return Result::Packet;
}

ListfileWriter::ListfileWriter(const std::string &filename, bool overwrite)
    : filename_(filename)
    , buffer_(ListfileBufferSize)
{
    if (!overwrite && std::filesystem::exists(filename))
        throw std::runtime_error(fmt::format("Listfile '{}' already exists", filename));

    // The buffer has to be set before opening the file to have an effect.
    file_.rdbuf()->pubsetbuf(buffer_.data(), buffer_.size());
    file_.open(filename, std::ios::out | std::ios::binary | std::ios::trunc);

    if (!file_)
        throw std::runtime_error(fmt::format("Failed to open listfile '{}' for writing: {}",
                                             filename, std::strerror(errno)));

    file_.exceptions(std::ios::failbit | std::ios::badbit);
}

void ListfileWriter::write(const DataPacket &packet)
{
    file_.write(reinterpret_cast<const char *>(&packet), sizeof(packet));
    bytesWritten_ += sizeof(packet);
}

void ListfileWriter::close()
{
    if (file_.is_open())
        file_.close();
}

} // namespace mesytec::mcpd
