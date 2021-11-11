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
static const size_t MaxOutgoingPayloadSize = 1500 - 20 - 8;

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

}
}

#endif /* __MESYTEC_MCPD_UTIL_UDP_SOCKETS_H__ */
