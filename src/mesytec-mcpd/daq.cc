#include "daq.h"

#include <stdexcept>

namespace mesytec::mcpd
{

Daq::Daq(u16 listenPort)
    : listenPort_(listenPort)
{
}

Daq::~Daq() { stop(); }

void Daq::startReadout(const std::string &listfile, bool overwriteListfile)
{
    if (isRunning())
        throw std::logic_error("Daq: already running");

    // Bind first so that a busy port does not leave an empty listfile behind.
    auto source = std::make_unique<UdpPacketSource>(listenPort_);

    std::unique_ptr<ListfileWriter> writer;
    if (!listfile.empty())
        writer = std::make_unique<ListfileWriter>(listfile, overwriteListfile);

    const auto localPort = source->localPort();
    receiveBufferSize_ = source->receiveBufferSize();
    startWorker(std::move(source), std::move(writer));
    localPort_ = localPort;
}

void Daq::startReplay(const std::string &listfile)
{
    if (isRunning())
        throw std::logic_error("Daq: already running");

    startWorker(std::make_unique<ListfilePacketSource>(listfile), {});
}

void Daq::startWorker(std::unique_ptr<PacketSource> source, std::unique_ptr<ListfileWriter> listfile)
{
    worker_.reset(); // joins a previous, already finished worker
    worker_ = std::make_unique<ReadoutWorker>(
        std::move(source), std::move(listfile),
        std::vector<std::shared_ptr<PacketConsumer>>{stats_, mdllHistos_, mcpdHistos_});
    worker_->start();
}

void Daq::stop()
{
    if (worker_)
        worker_->stop();
    localPort_ = 0;
}

void Daq::rethrowException()
{
    if (worker_)
        worker_->rethrowException();
}

ReadoutWorkerCounters Daq::getCounters() const
{
    return worker_ ? worker_->getCounters() : ReadoutWorkerCounters{};
}

void Daq::clearHistograms()
{
    mdllHistos_->clearHistograms();
    mcpdHistos_->clearHistograms();
}

void Daq::clearSources()
{
    stats_->clear();
    mdllHistos_->clear();
    mcpdHistos_->clear();
}

} // namespace mesytec::mcpd
