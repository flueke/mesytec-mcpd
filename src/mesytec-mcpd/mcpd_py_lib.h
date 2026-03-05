#ifndef E7B93B2B_DB43_49A2_A29F_480864086D05
#define E7B93B2B_DB43_49A2_A29F_480864086D05

#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <exception>
#include <vector>

#include <mesytec-mcpd/mesytec-mcpd.h>
#include <mesytec-mcpd/util/locked_ptr.h>

namespace mesytec::mcpd
{

struct ReadoutCounters
{
    size_t packets = 0u;
    size_t bytes = 0u;
    size_t timeouts = 0u;
    size_t events = 0u;
    size_t packetLoss = 0u;
};

// Helper class for the python bindings to read out data packets from MCPD/MDLL
// modules.
// Runs its own thread, reading packets in a loop and buffering them. Packets
// can be consumed using getPackets().
//
// Error handling is done via exceptions. The current implementation throws
// std::system_error() to transmit error codes to the outside.

class Readout
{
  public:
    explicit Readout(int listenPort = McpdDefaultPort, size_t packetBufferMaxSize = 1024 * 10);
    ~Readout();

    bool start();
    bool stop();

    bool isRunning() const;
    std::vector<DataPacket> getPackets();
    ReadoutCounters getCounters();
    bool hasReadoutException() const;
    std::exception_ptr getReadoutException();
    void rethrowReadoutException();

  private:
    int listenPort_ = McpdDefaultPort;

    std::atomic<bool> keepRunning_ = true;
    std::thread readoutThread_;

    locked_ptr<ReadoutCounters> counters_ = locked_ptr<ReadoutCounters>(std::in_place);
    locked_ptr<std::vector<DataPacket>> packetBuffer_ = locked_ptr<std::vector<DataPacket>>(std::in_place);
    mutable locked_ptr<std::exception_ptr> readoutException_ = locked_ptr<std::exception_ptr>(std::in_place);
    mutable std::mutex startStopMutex_;

    Readout(const Readout &) = delete;
    Readout &operator=(const Readout &) = delete;
    Readout(Readout &&) = delete;
    Readout &operator=(Readout &&) = delete;

    void readoutLoop(std::promise<bool> promise);
    bool isRunning_() const;
};

} // namespace mesytec::mcpd

#endif /* E7B93B2B_DB43_49A2_A29F_480864086D05 */
