#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include "mcpd_py_lib.h"
#include "util/logging.h"
#include <mesytec-mcpd/mesytec-mcpd.h>

namespace nb = nanobind;
using namespace mesytec::mcpd;

#if 0
void init_logging()
{
    // mesytec-mcpd links privately against spdlog and so do we. This means we
    // have two copies of spdlog around. The code below sets logger and log
    // level for both instances.
    auto logger = pybind11_log::init_mt("mcpd_py");

    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::trace);

    mesytec::mcpd::set_default_logger(logger);
    mesytec::mcpd::set_global_log_level(spdlog::level::trace);
}
#endif

NB_MODULE(_mesytec_mcpd_py, m)
{
    m.doc() = "driver library for the mesytec PSD system (MCPD, MPSD, MDLL) - python bindings";
    m.attr("__version__") = library_version();

    m.def("init", []() { /*init_logging();*/ });
    m.def(
        "set_log_level",
        [](const std::string &levelName)
        {
            auto level = log_level_from_string(levelName);
            spdlog::set_level(level.value_or(spdlog::level::info));
            mesytec::mcpd::set_global_log_level(level.value_or(spdlog::level::info));
        },
        nb::arg("levelName"));

    nb::class_<DecodedEvent::Neutron>(m, "Neutron")
        .def(nb::init<>())
        .def_ro("mdpsd_id", &DecodedEvent::Neutron::mpsdId)
        .def_ro("channel", &DecodedEvent::Neutron::channel)
        .def_ro("amplitude", &DecodedEvent::Neutron::amplitude)
        .def_ro("position", &DecodedEvent::Neutron::position);

    nb::class_<DecodedEvent::MdllNeutron>(m, "MdllNeutron")
        .def(nb::init<>())
        .def_ro("amplitude", &DecodedEvent::MdllNeutron::amplitude)
        .def_ro("x_pos", &DecodedEvent::MdllNeutron::xPos)
        .def_ro("y_pos", &DecodedEvent::MdllNeutron::yPos);

    nb::class_<DecodedEvent::Trigger>(m, "Trigger")
        .def(nb::init<>())
        .def_ro("trigger_id", &DecodedEvent::Trigger::triggerId)
        .def_ro("data_id", &DecodedEvent::Trigger::dataId)
        .def_ro("value", &DecodedEvent::Trigger::value);

    nb::enum_<EventType>(m, "EventType", "enum.Enum")
        .value("NeutronEvent", EventType::Neutron)
        .value("TriggerEvent", EventType::Trigger)
        .value("MdllNeutronEvent", EventType::MdllNeutron)
        .export_values();

    nb::class_<DecodedEvent>(m, "DecodedEvent")
        .def(nb::init<>())
        .def_ro("deviceId", &DecodedEvent::deviceId)
        .def_ro("type", &DecodedEvent::type)
        .def_ro("timestamp", &DecodedEvent::timestamp)
        .def_ro("neutron", &DecodedEvent::neutron)
        .def_ro("trigger", &DecodedEvent::trigger)
        .def_ro("mdll_neutron", &DecodedEvent::mdllNeutron)
        .def("__str__", [](const DecodedEvent &event) { return to_string(event); });

    nb::class_<DataPacket>(m, "DataPacket")
        .def(nb::init<>())
        .def_ro("runId", &DataPacket::runId)
        .def_ro("device_status", &DataPacket::deviceStatus)
        .def_ro("device_id", &DataPacket::deviceId)
        .def_ro("buffer_type", &DataPacket::bufferType)
        .def_ro("buffer_length", &DataPacket::bufferLength)
        .def_ro("buffer_number", &DataPacket::bufferNumber)
        .def_prop_ro("time",
                     [](const DataPacket &packet)
                     {
                         using Array = nb::ndarray<nb::numpy, nb::ro, uint16_t, nb::shape<3>>;
                         return Array(packet.time);
                     })

        .def_prop_ro("params",
                     [](const DataPacket &packet)
                     {
                         using Array = nb::ndarray<nb::numpy, nb::ro, uint16_t,
                                                   nb::shape<McpdParamCount, McpdParamWords>>;
                         return Array(&packet.param[0][0]);
                     })

        .def_prop_ro("data",
                     [](const DataPacket &packet)
                     {
                         using Array = nb::ndarray<nb::numpy, nb::ro, uint16_t, nb::shape<-1>>;
                         return Array(packet.data, {static_cast<size_t>(get_data_length(packet))});
                     })

        .def("__str__", [](const DataPacket &packet) { return to_string(packet); })

        .def("event_count", [](const DataPacket &packet) { return get_event_count(packet); })

        .def("decode_event", [](const DataPacket &packet, size_t eventNum)
             { return decode_event(packet, eventNum); })

        .def("get_events",
             [](const DataPacket &packet)
             {
                 const auto eventCount = get_event_count(packet);
                 std::vector<DecodedEvent> events;
                 events.reserve(eventCount);

                 for (size_t i = 0; i < eventCount; ++i)
                     events.push_back(decode_event(packet, i));

                 return events;
             });

    nb::class_<ReadoutCounters>(m, "ReadoutCounters")
        .def(nb::init<>())
        .def_ro("packets", &ReadoutCounters::packets)
        .def_ro("bytes", &ReadoutCounters::bytes)
        .def_ro("timeouts", &ReadoutCounters::timeouts)
        .def_ro("events", &ReadoutCounters::events);

    nb::class_<Readout>(m, "Readout")
        .def(nb::init<int>(), nb::arg("listenPort") = McpdDefaultPort)
        .def("start", &Readout::start)
        .def("stop", &Readout::stop)
        .def("is_running", &Readout::isRunning)
        .def("get_packets", &Readout::getPackets)
        .def("get_counters", &Readout::getCounters)
        .def("has_readout_exception", &Readout::hasReadoutException)
        .def("rethrow_readout_exception", &Readout::rethrowReadoutException);

    // Event field constants (maximum values)
    namespace ec = event_constants;

    nb::module_ constants = m.def_submodule("constants", "Event field ranges");

    nb::module_ mdll_neutron =
        constants.def_submodule("mdll_neutron", "MDLL neutron event field ranges");
    mdll_neutron.attr("amplitude_max") = (1u << ec::mdll_neutron::AmplitudeBits) - 1;
    mdll_neutron.attr("x_pos_max") = (1u << ec::mdll_neutron::xPosBits) - 1;
    mdll_neutron.attr("y_pos_max") = (1u << ec::mdll_neutron::yPosBits) - 1;

    nb::module_ neutron = constants.def_submodule("neutron", "MPSD neutron event field ranges");
    neutron.attr("mpsd_id_max") = (1u << ec::neutron::MpsdIdBits) - 1;
    neutron.attr("channel_max") = (1u << ec::neutron::ChannelBits) - 1;
    neutron.attr("amplitude_max") = (1u << ec::neutron::AmplitudeBits) - 1;
    neutron.attr("position_max") = (1u << ec::neutron::PositionBits) - 1;

    nb::module_ trigger = constants.def_submodule("trigger", "Trigger event field ranges");
    trigger.attr("trigger_id_max") = (1u << ec::trigger::TriggerIdBits) - 1;
    trigger.attr("data_id_max") = (1u << ec::trigger::DataIdBits) - 1;
    trigger.attr("data_max") = (1u << ec::trigger::DataBits) - 1;

    m.attr("timestamp_max") = (1u << ec::TimestampBits) - 1;

    nb::module_ buffer_types = constants.def_submodule("buffer_types", "Data buffer types");
    buffer_types.attr("CommandPacketBufferType") = CommandPacketBufferType;
    buffer_types.attr("McpdDataBufferType") = McpdDataBufferType;
    buffer_types.attr("MdllDataBufferType") = MdllDataBufferType;
}
