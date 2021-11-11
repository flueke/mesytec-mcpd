#include "udp_sockets.h"

#ifndef __WIN32
    #include <netdb.h>
    #include <sys/stat.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <unistd.h>

    #ifdef __linux__
        #include <sys/prctl.h>
        #include <linux/netlink.h>
        #include <linux/rtnetlink.h>
        #include <linux/inet_diag.h>
        #include <linux/sock_diag.h>
    #endif

    #include <arpa/inet.h>
#else // __WIN32
    #include <ws2tcpip.h>
    #include <stdio.h>
    #include <fcntl.h>
    #include <mmsystem.h>
#endif

#include <cassert>
#include <cstring>

namespace mesytec
{
namespace mcpd
{

namespace
{
    inline struct timeval ms_to_timeval(unsigned ms)
    {
        unsigned seconds = ms / 1000;
        ms -= seconds * 1000;

        struct timeval tv;
        tv.tv_sec  = seconds;
        tv.tv_usec = ms * 1000;

        return tv;
    }
} // end anon namespace

std::error_code lookup(const std::string &host, u16 port, sockaddr_in &dest)
{
    using namespace mesytec::mcpd;

    if (host.empty())
    {
        // FIXME return MVLCErrorCode::EmptyHostname;
        throw std::runtime_error("empty hostname");
    }

    dest = {};
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *result = nullptr, *rp = nullptr;

    int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(),
                         &hints, &result);

    // TODO: check getaddrinfo specific error codes. make and use getaddrinfo error category
    if (rc != 0)
    {
        //qDebug("%s: HostLookupError, host=%s, error=%s", __PRETTY_FUNCTION__, host.c_str(),
        //       gai_strerror(rc));
        // FIXME return make_error_code(MVLCErrorCode::HostLookupError);
        throw std::runtime_error("host lookup error");
    }

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        if (rp->ai_addrlen == sizeof(dest))
        {
            std::memcpy(&dest, rp->ai_addr, rp->ai_addrlen);
            break;
        }
    }

    freeaddrinfo(result);

    if (!rp)
    {
        //qDebug("%s: HostLookupError, host=%s, no result found", __PRETTY_FUNCTION__, host.c_str());
        // FIXME return make_error_code(MVLCErrorCode::HostLookupError);
        throw std::runtime_error("host lookup error");
    }

    return {};
}

#ifndef __WIN32
std::error_code set_socket_timeout(int optname, int sock, unsigned ms)
{
    struct timeval tv = ms_to_timeval(ms);

    int res = setsockopt(sock, SOL_SOCKET, optname, &tv, sizeof(tv));

    if (res != 0)
        return std::error_code(errno, std::system_category());

    return {};
}
#else // WIN32
std::error_code set_socket_timeout(int optname, int sock, unsigned ms)
{
    DWORD optval = ms;
    int res = setsockopt(sock, SOL_SOCKET, optname,
                         reinterpret_cast<const char *>(&optval),
                         sizeof(optval));

    if (res != 0)
        return std::error_code(errno, std::system_category());

    return {};
}
#endif

std::error_code set_socket_write_timeout(int sock, unsigned ms)
{
    return set_socket_timeout(SO_SNDTIMEO, sock, ms);
}

std::error_code set_socket_read_timeout(int sock, unsigned ms)
{
    return set_socket_timeout(SO_RCVTIMEO, sock, ms);
}

#ifndef __WIN32
std::error_code close_socket(int sock)
{
    int res = ::close(sock);
    if (res != 0)
        return std::error_code(errno, std::system_category());
    return {};
}
#else // WIN32
std::error_code close_socket(int sock)
{
    int res = ::closesocket(sock);
    if (res != 0)
        return std::error_code(errno, std::system_category());
    return {};
}
#endif

// Note: it is not necessary to split writes into multiple calls to send()
// because outgoing MVLC command buffers have to be smaller than the maximum,
// non-jumbo ethernet MTU.
// The send() call should return EMSGSIZE if the payload is too large to be
// atomically transmitted.
#ifdef __WIN32
std::error_code write_to_socket(
    int socket, const u8 *buffer, size_t size, size_t &bytesTransferred)
{
    assert(size <= MaxOutgoingPayloadSize);

    bytesTransferred = 0;

    ssize_t res = ::send(socket, reinterpret_cast<const char *>(buffer), size, 0);

    if (res == SOCKET_ERROR)
    {
        int err = WSAGetLastError();

        if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK)
            return make_error_code(MVLCErrorCode::SocketWriteTimeout);

        // Maybe TODO: use WSAGetLastError here with a WSA specific error
        // category like this: https://gist.github.com/bbolli/710010adb309d5063111889530237d6d
        return make_error_code(MVLCErrorCode::SocketError);
    }

    bytesTransferred = res;
    return {};
}
#else // !__WIN32
std::error_code write_to_socket(
    int socket, const u8 *buffer, size_t size, size_t &bytesTransferred)
{
    assert(size <= MaxOutgoingPayloadSize);

    bytesTransferred = 0;

    ssize_t res = ::send(socket, reinterpret_cast<const char *>(buffer), size, 0);

    if (res < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return std::error_code(EAGAIN, std::system_category());

        return std::error_code(errno, std::system_category());
    }

    bytesTransferred = res;
    return {};
}
#endif // !__WIN32

#ifdef __WIN32
std::error_code receive_one_packet(int sockfd, u8 *dest, size_t size,
                                   size_t &bytesTransferred, int timeout_ms)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);

    struct timeval tv = ms_to_timeval(timeout_ms);

    int sres = ::select(0, &fds, nullptr, nullptr, &tv);

    if (sres == 0)
    {
        // FIXME return make_error_code(MVLCErrorCode::SocketReadTimeout);
        throw std::runtime_error("socket read timeout");
    }

    if (sres == SOCKET_ERROR)
    {
        // FIXME return make_error_code(MVLCErrorCode::SocketError);
        throw std::runtime_error("socket error");
    }

    ssize_t res = ::recv(sockfd, reinterpret_cast<char *>(dest), size, 0);

    //logger->trace("::recv res={}", res);

    if (res == SOCKET_ERROR)
    {
        int err = WSAGetLastError();

        if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK)
        {
            // FIXME return make_error_code(MVLCErrorCode::SocketReadTimeout);
            return std::error_code(EAGAIN, std::system_category()); // FIXME: test this under windows
        }

        // FIXME return make_error_code(MVLCErrorCode::SocketError);
        throw std::runtime_error("socket error");
    }

#if 0
    if (res >= static_cast<ssize_t>(sizeof(u32)))
    {
        util::log_buffer(
            std::cerr,
            basic_string_view<const u32>(reinterpret_cast<const u32 *>(dest), res / sizeof(u32)),
            "32-bit words in buffer from ::recv()");
    }
#endif

    bytesTransferred = res;

    return {};
}
#else
std::error_code receive_one_packet(int sockfd, u8 *dest, size_t size,
                                   size_t &bytesTransferred, int)
{
    bytesTransferred = 0u;

    ssize_t res = ::recv(sockfd, reinterpret_cast<char *>(dest), size, 0);

    if (res < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return std::error_code(EAGAIN, std::system_category());

        return std::error_code(errno, std::system_category());
    }

    bytesTransferred = res;
    return {};
}
#endif

}
}
