#ifndef __MESYTEC_MCPD_UTIL_UDP_SOCKETS_H__
#define __MESYTEC_MCPD_UTIL_UDP_SOCKETS_H__

#ifndef __WIN32
#include <netinet/ip.h> // sockaddr_in
#else
#include <winsock2.h>
#endif

#include <sstream>
#include <string>
#include <system_error>

#include "int_types.h"

namespace mesytec
{
namespace mcpd
{

static const unsigned DefaultWriteTimeout_ms = 500;
static const unsigned DefaultReadTimeout_ms  = 500;

// Standard MTU is 1500 bytes
// IPv4 header is 20 bytes
// UDP header is 8 bytes
static const size_t MaxPayloadSize = 1500 - 20 - 8;


// Creates, binds and connects a UDP socket. Uses an OS assigned local port
// number.
//
// Returns the socket on success or -1 on error. If ecp is non-null and an
// error occurs it will be stored in *ecp.
int connect_udp_socket(const std::string &remoteHost, u16 remotePort, std::error_code *ecp = nullptr);

// Returns an unconnected UDP socket bound to the specified local port or -1 on
// error. If ecp is non-null and an error occurs it will be stored in *ecp.
int bind_udp_socket(u16 localPort, std::error_code *ecp = nullptr);

// Returns the local port the socket is bound to or 0 on error. If ecp is
// non-null and an error occurs it will be stored in *ecp.
u16 get_local_socket_port(int sock, std::error_code *ecp = nullptr);

// Does IPv4 host lookup for a UDP socket. On success the resulting struct
// sockaddr_in is copied to dest.
std::error_code lookup(const std::string &host, u16 port, sockaddr_in &dest);

std::error_code set_socket_write_timeout(int sock, unsigned ms);
std::error_code set_socket_read_timeout(int sock, unsigned ms);

std::error_code close_socket(int sock);

std::error_code write_to_socket(
    int socket, const u8 *buffer, size_t size, size_t &bytesTransferred);

std::error_code receive_one_packet(
    int sockfd, u8 *dest, size_t size, size_t &bytesTransferred, int timeout_ms);

inline std::string format_ipv4(u32 a)
{
    std::stringstream ss;

    ss <<  ((a >> 24) & 0xff) << '.'
        << ((a >> 16) & 0xff) << '.'
        << ((a >>  8) & 0xff) << '.'
        << ((a >>  0) & 0xff);

    return ss.str();
}

enum class SocketErrorCode
{
    NoError,
    EmptyHostname,
    HostLookupError,
    SocketWriteTimeout,
    SocketReadTimeout,
    GenericSocketError,
};

std::error_code make_error_code(SocketErrorCode error);

enum class SocketErrorType
{
    Success,
    LookupError,
    Timeout,
    ConnectionError,
};

std::error_condition make_error_condition(SocketErrorType et);

}
}

namespace std
{
    template<> struct is_error_code_enum<mesytec::mcpd::SocketErrorCode>: true_type {};
    template<> struct is_error_condition_enum<mesytec::mcpd::SocketErrorType>: true_type {};
} // end namespace std

namespace mesytec
{
namespace mcpd
{

inline bool is_timeout(const std::error_code &ec)
{
    return ec == SocketErrorType::Timeout;
}

}
}

#endif /* __MESYTEC_MCPD_UTIL_UDP_SOCKETS_H__ */
