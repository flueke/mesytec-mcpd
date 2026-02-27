#include <pybind11/pybind11.h>
#include <pybind11/native_enum.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <mesytec-mcpd/mesytec-mcpd.h>
#include "util/logging.h"
#include "util/pybind11_log.h"

// TODO:
// - maybe return a std::promise from start() and stop(). I've done this elsewhere (mvlc I think) and it works just fine.

namespace py = pybind11;
using namespace mesytec::mcpd;

struct ReadoutCounters
{
    size_t packets = 0u;
    size_t bytes = 0u;
    size_t timeouts = 0u;
    size_t events = 0u;
};

class Readout : public std::enable_shared_from_this<Readout>
{
public:
    explicit Readout(int listenPort = McpdDefaultPort)
        : listenPort_(listenPort)
    {
        spdlog::debug("{}: listenPort={}", __PRETTY_FUNCTION__, listenPort_);
        packetBuffer_.reserve(1024);
    }

    ~Readout()
    {
        //spdlog::debug("{}", __PRETTY_FUNCTION__);
        stop();
    }

    void start()
    {
        spdlog::debug("{}", __PRETTY_FUNCTION__);

        std::lock_guard<std::mutex> lock(mutex_);
        // Do not call isRunning() here as we do hold and need to hold the lock.
        if (readoutThread_.joinable())
        {
            spdlog::warn("{}: already running, not starting again", __PRETTY_FUNCTION__);
            return;
        }

        quit_ = false;
        counters_ = {};
        readoutException_ = nullptr;
        spdlog::debug("{}: starting readout thread", __PRETTY_FUNCTION__);
        readoutThread_ = std::thread(&Readout::readoutLoop, this);
        spdlog::debug("{}: readout thread started, returning", __PRETTY_FUNCTION__);
    }

    void stop()
    {
        spdlog::debug("{}", __PRETTY_FUNCTION__);

        std::lock_guard<std::mutex> lock(mutex_);
        if (!readoutThread_.joinable())
        {
            spdlog::warn("{}: already running, not starting again", __PRETTY_FUNCTION__);
            return;
        }

        spdlog::debug("{}: stopping readout", __PRETTY_FUNCTION__);
        quit_ = true;

        if (readoutThread_.joinable())
        {
            spdlog::debug("{}: joining readout thread", __PRETTY_FUNCTION__);
            readoutThread_.join();
        }
        else
        {
            spdlog::warn("{}: readout thread not joinable", __PRETTY_FUNCTION__);
        }
    }

    bool isRunning() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto ret = readoutThread_.joinable();
        spdlog::debug("{}, result={}", __PRETTY_FUNCTION__, ret);
        return ret;
    }

    std::vector<DataPacket> getPackets()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto ret = std::move(packetBuffer_);
        packetBuffer_ = {};
        packetBuffer_.reserve(1024);
        spdlog::debug("{}: returning {} packets", __PRETTY_FUNCTION__, ret.size());
        return ret;
    }

    ReadoutCounters getCounters()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        spdlog::debug("{}", __PRETTY_FUNCTION__);
        return counters_;
    }

    bool hasReadoutException() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto ret = readoutException_ != nullptr;
        spdlog::debug("{}, result={}", __PRETTY_FUNCTION__, ret);
        return ret;
    }

    std::exception_ptr getReadoutException()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        spdlog::debug("{}", __PRETTY_FUNCTION__);
        return readoutException_;
    }

private:
    int listenPort_ = McpdDefaultPort;

    std::atomic<bool> quit_ = false;
    std::thread readoutThread_;

    mutable std::mutex mutex_; // protects counters_, packetBuffer_ and readoutException_
    ReadoutCounters counters_;
    std::vector<DataPacket> packetBuffer_;
    std::exception_ptr readoutException_;

    Readout(const Readout &) = delete;
    Readout &operator=(const Readout &) = delete;
    Readout(Readout &&) = delete;
    Readout &operator=(Readout &&) = delete;

    void readoutLoop()
    {
        spdlog::debug("entering {}", __PRETTY_FUNCTION__);
        int dataSock = -1;

        try
        {
            std::error_code ec;
            dataSock = create_bound_udp_socket(listenPort_, &ec);

            if (ec)
            {
                spdlog::error("{}: failed to create and bind UDP socket to port {}, ec={}", __PRETTY_FUNCTION__, listenPort_, ec.message());
                throw std::system_error(ec);
            }
            else
            {
                spdlog::debug("{}: created and bound UDP socket to port {}, sockfd={}, ec={}", __PRETTY_FUNCTION__, listenPort_, dataSock, ec.message());
            }

            while (!quit_)
            {
                size_t bytesTransferred = 0u;
                sockaddr_in srcAddr = {};
                DataPacket dataPacket = {};

                auto ec = receive_one_packet(
                    dataSock,
                    reinterpret_cast<u8 *>(&dataPacket), sizeof(dataPacket),
                    bytesTransferred, DefaultReadTimeout_ms, &srcAddr);

                if (ec)
                {
                    if (ec != SocketErrorType::Timeout)
                        throw std::system_error(ec);

                    std::lock_guard<std::mutex> lock(mutex_);
                    ++counters_.timeouts;
                }
                else if (bytesTransferred)
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    packetBuffer_.push_back(dataPacket);
                    ++counters_.packets;
                    counters_.bytes += bytesTransferred;
                    counters_.events += get_event_count(dataPacket);
                }
            }
        }
        catch (const std::exception &e)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            readoutException_ = std::current_exception();
            spdlog::warn("readout thread exiting with exception: {}", e.what());
        }

        if (dataSock != -1)
        {
            close_socket(dataSock);
        }

        spdlog::debug("exiting {}", __PRETTY_FUNCTION__);
    }
};

void init_logging()
{
    // FIXME: mesytec-mcpd still links publicly to spdlog. We have two copies of
    // spdlog: this translation unit and the one in the shared object. Setting
    // levels and loggers in just our copy is not enough, we have to do it in
    // both.  TODO: make spdlog a private dependency of mesytec-mcpd and use
    // only the functions in logging.h
    auto logger = pybind11_log::init_mt("mcpd_py");
    spdlog::set_level(spdlog::level::trace);
    mesytec::mcpd::set_default_logger(logger);
    mesytec::mcpd::set_global_log_level(spdlog::level::trace);
}

PYBIND11_MODULE(mesytec_mcpd, m)
{
    m.doc() = "driver library for the mesytec PSD system (MCPD, MPSD, MDLL) - python bindings";
    m.attr("__version__") = library_version();

    m.def("init", []() { init_logging(); });
    m.def("set_log_level", [](const std::string &levelName)
          {
              auto level = log_level_from_string(levelName);
              spdlog::set_level(level.value_or(spdlog::level::info));
              mesytec::mcpd::set_global_log_level(level.value_or(spdlog::level::info));
          }, py::arg("levelName"));

    py::class_<DecodedEvent>(m, "DecodedEvent")
        .def(py::init<>())
        .def_readonly("deviceId", &DecodedEvent::deviceId)
        .def_readonly("type", &DecodedEvent::type)
        .def_readonly("timestamp", &DecodedEvent::timestamp)
        .def_readonly("neutron", &DecodedEvent::neutron)
        .def_readonly("trigger", &DecodedEvent::trigger)
        .def_readonly("mdllNeutron", &DecodedEvent::mdllNeutron)
        .def("__str__", [](const DecodedEvent &event)
             { return to_string(event); });

    py::class_<DecodedEvent::Neutron>(m, "Neutron")
        .def(py::init<>())
        .def_readonly("mpsdId", &DecodedEvent::Neutron::mpsdId)
        .def_readonly("channel", &DecodedEvent::Neutron::channel)
        .def_readonly("amplitude", &DecodedEvent::Neutron::amplitude)
        .def_readonly("position", &DecodedEvent::Neutron::position);

    py::class_<DecodedEvent::MdllNeutron>(m, "MdllNeutron")
        .def(py::init<>())
        .def_readonly("amplitude", &DecodedEvent::MdllNeutron::amplitude)
        .def_readonly("xPos", &DecodedEvent::MdllNeutron::xPos)
        .def_readonly("yPos", &DecodedEvent::MdllNeutron::yPos);

    py::native_enum<EventType>(m, "EventType", "enum.Enum")
        .value("NeutronEvent", EventType::Neutron)
        .value("TriggerEvent", EventType::Trigger)
        .value("MdllNeutronEvent", EventType::MdllNeutron)
        .export_values()
        .finalize();

    py::class_<DataPacket>(m, "DataPacket")
        .def(py::init<>())
        .def_readonly("runId", &DataPacket::runId)
        .def_readonly("deviceStatus", &DataPacket::deviceStatus)
        .def_readonly("deviceId", &DataPacket::deviceId)
        .def_property_readonly("time", [](const DataPacket &packet)
                               { return py::array_t<u16>(
                                     {3},             // shape
                                     {sizeof(u16)},   // stride
                                     packet.time,     // pointer to data
                                     py::cast(packet) // base object to keep alive
                                 ); })

        .def_property_readonly("params", [](const DataPacket &packet)
                               { return py::array_t<u16>(
                                     {McpdParamCount, McpdParamWords},            // shape
                                     {sizeof(u16) * McpdParamWords, sizeof(u16)}, // stride
                                     &packet.param[0][0],                         // pointer to data
                                     py::cast(packet)                             // base object to keep alive
                                 ); })

        .def_property_readonly("data", [](const DataPacket &packet)
                               { return py::array_t<u16>(
                                     {get_data_length(packet)}, // shape
                                     {sizeof(u16)},             // stride
                                     packet.data,               // pointer to data
                                     py::cast(packet)           // base object to keep alive
                                 ); })

        .def("__str__", [](const DataPacket &packet)
             { return to_string(packet); })

        .def("event_count", [](const DataPacket &packet)
             { return get_event_count(packet); })

        .def("decode_event", [](const DataPacket &packet, size_t eventNum)
             { return decode_event(packet, eventNum); })

        .def("get_events", [](const DataPacket &packet)
             {
            const auto eventCount = get_event_count(packet);
            std::vector<DecodedEvent> events;
            events.reserve(eventCount);

            for (size_t i=0; i<eventCount; ++i)
                events.push_back(decode_event(packet, i));

            return events; });

    py::class_<ReadoutCounters>(m, "ReadoutCounters")
        .def(py::init<>())
        .def_readonly("packets", &ReadoutCounters::packets)
        .def_readonly("bytes", &ReadoutCounters::bytes)
        .def_readonly("timeouts", &ReadoutCounters::timeouts)
        .def_readonly("events", &ReadoutCounters::events);

    py::class_<Readout, py::smart_holder>(m, "Readout")
        .def(py::init<int>(), py::arg("listenPort") = McpdDefaultPort)
        .def("start", &Readout::start)
        .def("stop", &Readout::stop)
        .def("is_running", &Readout::isRunning)
        .def("get_packets", &Readout::getPackets)
        .def("get_counters", &Readout::getCounters)
        .def("get_readout_exception", &Readout::getReadoutException)
        .def("has_readout_exception", &Readout::hasReadoutException);
}
