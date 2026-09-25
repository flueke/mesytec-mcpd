#ifndef B6E2A8F0_3C1D_4B7E_8F5A_2D9C4E7B1A03
#define B6E2A8F0_3C1D_4B7E_8F5A_2D9C4E7B1A03

// Sources of MCPD/MDLL data packets (UDP socket, listfile) and the listfile
// writer. A listfile is a plain sequence of sizeof(DataPacket) records, the
// same format mcpd-cli has always written.

#include <fstream>
#include <string>
#include <vector>

#include "mcpd_core.h"
#include "mesytec-mcpd_export.h"

namespace mesytec::mcpd
{

struct MESYTEC_MCPD_EXPORT ReceivedPacket
{
    DataPacket packet = {};
    size_t bytes = 0;  // number of bytes received/read
    u32 srcAddr = 0;   // IPv4 address in host byte order, 0 for listfile data
    u16 srcPort = 0;
};

// Checks the length fields against the number of bytes received. Only valid
// packets may be passed to get_event_count()/decode_event().
MESYTEC_MCPD_EXPORT bool is_valid_data_packet(const ReceivedPacket &rp);

class MESYTEC_MCPD_EXPORT PacketSource
{
  public:
    enum class Result
    {
        Packet,
        Timeout,
        EndOfData,
    };

    virtual ~PacketSource() = default;

    // Reads the next packet into dest. Throws on errors.
    virtual Result read(ReceivedPacket &dest) = 0;
};

class MESYTEC_MCPD_EXPORT UdpPacketSource: public PacketSource
{
  public:
    static constexpr int DefaultReadTimeout_ms = 100;
    static constexpr int DefaultReceiveBufferSize = 16 * 1024 * 1024;

    // Binds the socket immediately, throws std::system_error on failure. Pass
    // port 0 to let the OS choose a port.
    explicit UdpPacketSource(u16 listenPort, int readTimeout_ms = DefaultReadTimeout_ms,
                             int receiveBufferSize = DefaultReceiveBufferSize);
    ~UdpPacketSource() override;

    UdpPacketSource(const UdpPacketSource &) = delete;
    UdpPacketSource &operator=(const UdpPacketSource &) = delete;

    Result read(ReceivedPacket &dest) override;

    u16 localPort() const { return localPort_; }
    // Effective socket receive buffer size as reported by the OS.
    int receiveBufferSize() const { return receiveBufferSize_; }

  private:
    int sock_ = -1;
    int readTimeout_ms_ = DefaultReadTimeout_ms;
    u16 localPort_ = 0;
    int receiveBufferSize_ = 0;
};

class MESYTEC_MCPD_EXPORT ListfilePacketSource: public PacketSource
{
  public:
    // Throws std::runtime_error if the file cannot be opened.
    explicit ListfilePacketSource(const std::string &filename);

    Result read(ReceivedPacket &dest) override;

  private:
    std::string filename_;
    std::ifstream file_;
};

class MESYTEC_MCPD_EXPORT ListfileWriter
{
  public:
    // Throws std::runtime_error if the file exists and overwrite is false or
    // if it cannot be opened.
    ListfileWriter(const std::string &filename, bool overwrite = false);

    ListfileWriter(const ListfileWriter &) = delete;
    ListfileWriter &operator=(const ListfileWriter &) = delete;

    // Writes the full sizeof(DataPacket) record. Throws on write errors.
    void write(const DataPacket &packet);
    void close();

    const std::string &filename() const { return filename_; }
    size_t bytesWritten() const { return bytesWritten_; }

  private:
    std::string filename_;
    std::vector<char> buffer_;
    std::ofstream file_;
    size_t bytesWritten_ = 0;
};

} // namespace mesytec::mcpd

#endif /* B6E2A8F0_3C1D_4B7E_8F5A_2D9C4E7B1A03 */
