#include "readout_worker.h"

#include <stdexcept>

#include "util/logging.h"

namespace mesytec::mcpd
{

ReadoutWorker::ReadoutWorker(std::unique_ptr<PacketSource> source,
                             std::unique_ptr<ListfileWriter> listfile,
                             std::vector<std::shared_ptr<PacketConsumer>> consumers)
    : source_(std::move(source))
    , listfile_(std::move(listfile))
    , consumers_(std::move(consumers))
{
    if (!source_)
        throw std::invalid_argument("ReadoutWorker: null packet source");
}

ReadoutWorker::~ReadoutWorker() { stop(); }

void ReadoutWorker::start()
{
    if (thread_.joinable() || !source_)
        throw std::logic_error("ReadoutWorker: already started");

    running_ = true;
    thread_ = std::thread(&ReadoutWorker::loop, this);
}

void ReadoutWorker::stop()
{
    quit_ = true;

    if (thread_.joinable())
        thread_.join();

    // Release the socket/file right away, not when the worker is destroyed.
    source_.reset();

    if (listfile_)
    {
        try
        {
            listfile_->close();
        }
        catch (const std::exception &e)
        {
            spdlog::error("ReadoutWorker: error closing listfile '{}': {}", listfile_->filename(),
                          e.what());
        }
    }
}

bool ReadoutWorker::hasException() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return exception_ != nullptr;
}

void ReadoutWorker::rethrowException()
{
    std::exception_ptr ex;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        std::swap(ex, exception_);
    }
    if (ex)
        std::rethrow_exception(ex);
}

ReadoutWorkerCounters ReadoutWorker::getCounters() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return counters_;
}

void ReadoutWorker::loop()
{
    ReceivedPacket rp;

    try
    {
        while (!quit_)
        {
            const auto result = source_->read(rp);

            if (result == PacketSource::Result::EndOfData)
                break;

            if (result == PacketSource::Result::Timeout)
            {
                std::lock_guard<std::mutex> guard(mutex_);
                ++counters_.timeouts;
                continue;
            }

            if (listfile_)
                listfile_->write(rp.packet);

            const bool valid = is_valid_data_packet(rp);

            {
                std::lock_guard<std::mutex> guard(mutex_);
                ++counters_.packets;
                counters_.bytes += rp.bytes;
                counters_.listfileBytes = listfile_ ? listfile_->bytesWritten() : 0;
                if (!valid)
                    ++counters_.invalidPackets;
            }

            if (valid)
            {
                for (auto &consumer: consumers_)
                    consumer->consume(rp);
            }
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("ReadoutWorker: loop exiting with exception: {}", e.what());
        std::lock_guard<std::mutex> guard(mutex_);
        exception_ = std::current_exception();
    }

    for (auto &consumer: consumers_)
    {
        try
        {
            consumer->finished();
        }
        catch (const std::exception &e)
        {
            spdlog::error("ReadoutWorker: exception from consumer finished(): {}", e.what());
        }
    }

    running_ = false;
}

} // namespace mesytec::mcpd
