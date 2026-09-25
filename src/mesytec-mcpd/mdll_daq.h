#ifndef A3C5D2E1_7F4B_4E19_9B0C_6D2A8E1F4C77
#define A3C5D2E1_7F4B_4E19_9B0C_6D2A8E1F4C77

#include <array>
#include <atomic>
#include <exception>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <mesytec-mcpd/mesytec-mcpd.h>

namespace mesytec::mcpd::py_lib
{

// MDLL readout and replay without Python in the data path. A single worker
// thread receives packets from all MDLLs on one UDP port (or reads them from a
// listfile), optionally writes them unmodified to a listfile, keeps statistics
// and fills histograms per (source address, device id). Consumers poll copies
// of the stats and histograms.

struct MdllDeviceKey
{
    u32 srcAddr = 0; // IPv4 address in host byte order. 0 when replaying.
    u8 deviceId = 0;

    bool operator<(const MdllDeviceKey &o) const
    {
        return std::tie(srcAddr, deviceId) < std::tie(o.srcAddr, o.deviceId);
    }

    bool operator==(const MdllDeviceKey &o) const
    {
        return srcAddr == o.srcAddr && deviceId == o.deviceId;
    }
};

struct MdllDeviceStats
{
    u64 packets = 0;
    u64 bytes = 0;
    u64 events = 0;
    u64 neutronEvents = 0;
    u64 triggerEvents = 0;
    u64 packetsLost = 0;    // derived from gaps in the 16 bit bufferNumber sequence
    // bufferNumber repeated or went backwards (gap >= 0x8000), e.g. after a DAQ
    // reset issued elsewhere. Not counted as loss.
    u64 bufferNumberJumps = 0;
    u16 lastBufferNumber = 0;
    bool haveBufferNumber = false;
    u8 lastDeviceStatus = 0;
    u16 lastRunId = 0;
    u64 lastHeaderTimestamp = 0;
    std::array<u64, McpdParamCount> lastParams = {};
};

struct MdllHistograms
{
    static constexpr size_t AmplitudeBins = 1u << event_constants::mdll_neutron::AmplitudeBits;
    static constexpr size_t XBins = 1u << event_constants::mdll_neutron::xPosBits;
    static constexpr size_t YBins = 1u << event_constants::mdll_neutron::yPosBits;

    std::vector<u64> amplitude = std::vector<u64>(AmplitudeBins);
    std::vector<u64> x = std::vector<u64>(XBins);
    std::vector<u64> y = std::vector<u64>(YBins);
    std::vector<u64> xy = std::vector<u64>(XBins * YBins); // row-major: xy[y * XBins + x]

    void clear();
};

struct MdllDaqCounters
{
    u64 packets = 0;         // all received/replayed packets
    u64 bytes = 0;
    u64 timeouts = 0;
    u64 invalidPackets = 0;  // truncated or inconsistent length fields
    u64 nonMdllPackets = 0;  // valid packets with a buffer type other than MdllDataBufferType
    u64 listfileBytes = 0;
};

class MdllDaq
{
  public:
    explicit MdllDaq(u16 listenPort = McpdDefaultPort);
    ~MdllDaq();

    MdllDaq(const MdllDaq &) = delete;
    MdllDaq &operator=(const MdllDaq &) = delete;

    // Bind the data socket and start receiving. If listfilePath is non-empty
    // every received packet is written to it as a raw sizeof(DataPacket) record.
    void startReadout(const std::string &listfilePath = {}, bool overwriteListfile = false);

    // Read sizeof(DataPacket) records from a listfile as fast as possible.
    void startReplay(const std::string &listfilePath);

    void stop();

    // False once stopped or when a replay reached the end of the file.
    bool isRunning() const { return running_; }
    bool hasException() const;
    void rethrowException();

    // Local port of the data socket while a readout is active, 0 otherwise.
    u16 localPort() const { return localPort_; }
    u16 listenPort() const { return listenPort_; }
    // Effective socket receive buffer size in bytes as reported by the OS.
    int socketReceiveBufferSize() const { return rcvBufSize_; }

    MdllDaqCounters getCounters() const;
    std::map<MdllDeviceKey, MdllDeviceStats> getDeviceStats() const;
    std::optional<MdllHistograms> getHistograms(const MdllDeviceKey &key) const;

    void clearHistograms();
    // Zero device and global counters, including the bufferNumber baseline.
    // Keeps known devices and their histograms.
    void resetStats();
    // Forget all devices, their stats and histograms.
    void clearDevices();

  private:
    struct DeviceData
    {
        MdllDeviceStats stats;
        std::unique_ptr<MdllHistograms> histos = std::make_unique<MdllHistograms>();
    };

    void joinWorker();
    void readoutLoop();
    void replayLoop();
    void runLoop(void (MdllDaq::*loop)());
    void processPacket(DataPacket &packet, size_t bytes, u32 srcAddr);

    u16 listenPort_ = McpdDefaultPort;
    int dataSocket_ = -1;
    std::atomic<u16> localPort_ = 0;
    int rcvBufSize_ = 0;
    std::ofstream listfileOut_;
    std::ifstream listfileIn_;
    std::vector<char> listfileBuffer_;

    std::thread worker_;
    std::atomic<bool> quit_ = false;
    std::atomic<bool> running_ = false;

    mutable std::mutex mutex_; // guards everything below
    MdllDaqCounters counters_;
    std::map<MdllDeviceKey, DeviceData> devices_;
    std::exception_ptr exception_;
};

} // namespace mesytec::mcpd::py_lib

#endif /* A3C5D2E1_7F4B_4E19_9B0C_6D2A8E1F4C77 */
