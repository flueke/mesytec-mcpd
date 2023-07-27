#include "udp_sockets.h"
#include <system_error>
#include <spdlog/spdlog.h>

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

namespace
{

class SocketErrorCategory: public std::error_category
{
    const char *name() const noexcept override
    {
        return "socket_error";
    }

    std::string message(int ev) const override
    {
        using EC = mesytec::mcpd::SocketErrorCode;

        switch (static_cast<EC>(ev))
        {
            case EC::NoError:
                return "NoError";
            case EC::EmptyHostname:
                return "EmptyHostname";
            case EC::HostLookupError:
                return "HostLookupError";
            case EC::SocketWriteTimeout:
                return "SocketWriteTimeout";
            case EC::SocketReadTimeout:
                return "SocketReadTimeout";
            case EC::GenericSocketError:
                return "GenericSocketError";
        }

        return "unknown socket error";
    }

    std::error_condition default_error_condition(int ev) const noexcept override
    {
        using EC = mesytec::mcpd::SocketErrorCode;
        using ET = mesytec::mcpd::SocketErrorType;

        switch (static_cast<EC>(ev))
        {
            case EC::NoError:
                return ET::Success;

            case EC::EmptyHostname:
            case EC::HostLookupError:
                return ET::LookupError;

            case EC::SocketWriteTimeout:
            case EC::SocketReadTimeout:
                return ET::Timeout;

            case EC::GenericSocketError:
                return ET::ConnectionError;
        }
        assert(false);
        return {};
    }
};

const SocketErrorCategory theSocketErrorCategory{};

class SocketErrorTypeCategory: public std::error_category
{
    const char *name() const noexcept override
    {
        return "socket error type";
    }

    std::string message(int ev) const override
    {
        using ET = mesytec::mcpd::SocketErrorType;

        switch (static_cast<ET>(ev))
        {
            case ET::Success:
                return "Success";
            case ET::LookupError:
                return "LookupError";
            case ET::Timeout:
                return "Timeout";
            case ET::ConnectionError:
                return "ConnectionError";
        }

        return "unknown error type";
    }

    // Equivalence between local conditions and any error code
    bool equivalent(const std::error_code &ec, int condition) const noexcept override
    {
        using ET = mesytec::mcpd::SocketErrorType;

        switch (static_cast<ET>(condition))
        {
            case ET::Timeout:
                return ec == std::error_code(EAGAIN, std::system_category());

            default:
                break;
        }

        return false;
    }
};

const SocketErrorTypeCategory theSocketErrorTypeCategory{};

}

namespace mesytec
{
namespace mcpd
{

std::error_code make_error_code(SocketErrorCode error)
{
    return { static_cast<int>(error), theSocketErrorCategory };
}

std::error_condition make_error_condition(SocketErrorType et)
{
    return { static_cast<int>(et), theSocketErrorTypeCategory };
}

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

    static bool socketSystemInitialized = false;

    void init_socket_system()
    {
        if (!socketSystemInitialized)
        {
#ifdef __WIN32
            WORD wVersionRequested;
            WSADATA wsaData;
            wVersionRequested = MAKEWORD(2, 1);
            int res = WSAStartup(wVersionRequested, &wsaData);
            if (res != 0)
                spdlog::error("init_socket_system(): WSAStartup failed: {}", gai_strerror(res));
#endif
            socketSystemInitialized = true;
        }
    }

} // end anon namespace

int connect_udp_socket(const std::string &host, u16 port, std::error_code *ecp)
{
    init_socket_system();

    std::error_code ec_;
    std::error_code &ec = ecp ? *ecp : ec_;

    struct sockaddr_in addr = {};

    if ((ec = lookup(host, port, addr)))
        return -1;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0)
    {
        ec = std::error_code(errno, std::system_category());
        return -1;
    }

    // bind the socket
    {
        struct sockaddr_in localAddr = {};
        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = INADDR_ANY;

        if (::bind(sock, reinterpret_cast<struct sockaddr *>(&localAddr),
                   sizeof(localAddr)))
        {
            ec = std::error_code(errno, std::system_category());
            close_socket(sock);
            return -1;
        }
    }

    // connect
    if (::connect(sock, reinterpret_cast<struct sockaddr *>(&addr),
                  sizeof(addr)))
    {
        ec = std::error_code(errno, std::system_category());
        close_socket(sock);
        return -1;
    }

    // set the socket timeouts
    if ((ec = set_socket_read_timeout(sock, DefaultReadTimeout_ms)))
    {
        close_socket(sock);
        return -1;
    }

    if ((ec = set_socket_write_timeout(sock, DefaultWriteTimeout_ms)))
    {
        close_socket(sock);
        return -1;
    }

    return sock;
}

int bind_udp_socket(u16 localPort, std::error_code *ecp)
{
    init_socket_system();

    std::error_code ec_;
    std::error_code &ec = ecp ? *ecp : ec_;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0)
    {
        ec = std::error_code(errno, std::system_category());
        return -1;
    }

    // bind the socket
    {
        struct sockaddr_in localAddr = {};
        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = INADDR_ANY;
        localAddr.sin_port = htons(localPort);

        if (::bind(sock, reinterpret_cast<struct sockaddr *>(&localAddr),
                   sizeof(localAddr)))
        {
            ec = std::error_code(errno, std::system_category());
            close_socket(sock);
            return -1;
        }
    }

    // set the socket timeouts
    if ((ec = set_socket_read_timeout(sock, DefaultReadTimeout_ms)))
    {
        close_socket(sock);
        return -1;
    }

    if ((ec = set_socket_write_timeout(sock, DefaultWriteTimeout_ms)))
    {
        close_socket(sock);
        return -1;
    }

    return sock;
}

u16 get_local_socket_port(int sock, std::error_code *ecp)
{
    init_socket_system();

    std::error_code ec_;
    std::error_code &ec = ecp ? *ecp : ec_;

    struct sockaddr_in localAddr = {};
    socklen_t localAddrLen = sizeof(localAddr);

    if (::getsockname(
            sock, reinterpret_cast<struct sockaddr *>(&localAddr),
            &localAddrLen) != 0)
    {
        ec = std::error_code(errno, std::system_category());
        return 0u;
    }

    u16 localPort = ntohs(localAddr.sin_port);

    return localPort;
}

std::error_code lookup(const std::string &host, u16 port, sockaddr_in &dest)
{
    using namespace mesytec::mcpd;

    if (host.empty())
        return SocketErrorCode::EmptyHostname;

    init_socket_system();

    dest = {};
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *result = nullptr, *rp = nullptr;

    int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(),
                         &hints, &result);

    if (rc != 0)
    {
        #ifdef __WIN32
        spdlog::error("getaddrinfo(): host={}, error={}", host, gai_strerror(rc));
        #endif
        return SocketErrorCode::HostLookupError;
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
        return SocketErrorCode::HostLookupError;

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
    init_socket_system();

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
    assert(size <= MaxPayloadSize);

    init_socket_system();

    bytesTransferred = 0;

    ssize_t res = ::send(socket, reinterpret_cast<const char *>(buffer), size, 0);

    if (res == SOCKET_ERROR)
    {
        int err = WSAGetLastError();

        if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK)
            return SocketErrorCode::SocketWriteTimeout;

        // Maybe TODO: use WSAGetLastError here with a WSA specific error
        // category like this: https://gist.github.com/bbolli/710010adb309d5063111889530237d6d
        return SocketErrorCode::GenericSocketError;
    }

    bytesTransferred = res;
    return {};
}
#else // !__WIN32
std::error_code write_to_socket(
    int socket, const u8 *buffer, size_t size, size_t &bytesTransferred)
{
    assert(size <= MaxPayloadSize);

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
    size_t &bytesTransferred, int timeout_ms, sockaddr_in *src_addr)
{
    init_socket_system();

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);

    struct timeval tv = ms_to_timeval(timeout_ms);

    int sres = ::select(0, &fds, nullptr, nullptr, &tv);

    if (sres == 0)
        return SocketErrorCode::SocketReadTimeout;

    if (sres == SOCKET_ERROR)
        return SocketErrorCode::GenericSocketError;

    // Not sure why but windows returns EFAULT (10014) when passing the the
    // src_addr pointer directly to recvfrom(). Using a local sockaddr_in
    // structure works.
    struct sockaddr_in srcAddr;
    int srcAddrSize = sizeof(srcAddr);

    ssize_t res = ::recvfrom(sockfd, reinterpret_cast<char *>(dest), size, 0,
        reinterpret_cast<struct sockaddr *>(&srcAddr), &srcAddrSize);

    if (src_addr)
        std::memcpy(src_addr, &srcAddr, std::min(srcAddrSize, static_cast<int>(sizeof(*src_addr))));

    if (res == SOCKET_ERROR)
    {
        int err = WSAGetLastError();

        if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK)
            return SocketErrorCode::SocketReadTimeout;

        return SocketErrorCode::GenericSocketError;
    }

    bytesTransferred = res;

    return {};
}
#else
std::error_code receive_one_packet(int sockfd, u8 *dest, size_t size,
    size_t &bytesTransferred, int /*timeout_ms*/, sockaddr_in *src_addr)
{
    bytesTransferred = 0u;

    socklen_t addrlen = sizeof(sockaddr_in);
    ssize_t res = ::recvfrom(sockfd, reinterpret_cast<char *>(dest), size, 0,
        reinterpret_cast<sockaddr *>(src_addr), &addrlen);

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
