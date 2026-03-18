#include <pybind11/native_enum.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "mcpd_py_lib.h"
#include "util/logging.h"
#include "util/pybind11_log.h"
#include <mesytec-mcpd/mesytec-mcpd.h>

namespace py = pybind11;
using namespace mesytec::mcpd;

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

PYBIND11_MODULE(_mesytec_mcpd_py, m)
{
    m.doc() = "driver library for the mesytec PSD system (MCPD, MPSD, MDLL) - python bindings";
    m.attr("__version__") = library_version();

    m.def("init", []() { init_logging(); });
    m.def(
        "set_log_level",
        [](const std::string &levelName)
        {
            auto level = log_level_from_string(levelName);
            spdlog::set_level(level.value_or(spdlog::level::info));
            mesytec::mcpd::set_global_log_level(level.value_or(spdlog::level::info));
        },
        py::arg("levelName"));

    py::class_<DecodedEvent::Neutron>(m, "Neutron")
        .def(py::init<>())
        .def_readonly("mdpsd_id", &DecodedEvent::Neutron::mpsdId)
        .def_readonly("channel", &DecodedEvent::Neutron::channel)
        .def_readonly("amplitude", &DecodedEvent::Neutron::amplitude)
        .def_readonly("position", &DecodedEvent::Neutron::position);

    py::class_<DecodedEvent::MdllNeutron>(m, "MdllNeutron")
        .def(py::init<>())
        .def_readonly("amplitude", &DecodedEvent::MdllNeutron::amplitude)
        .def_readonly("x_pos", &DecodedEvent::MdllNeutron::xPos)
        .def_readonly("y_pos", &DecodedEvent::MdllNeutron::yPos);

    py::class_<DecodedEvent::Trigger>(m, "Trigger")
        .def(py::init<>())
        .def_readonly("trigger_id", &DecodedEvent::Trigger::triggerId)
        .def_readonly("data_id", &DecodedEvent::Trigger::dataId)
        .def_readonly("value", &DecodedEvent::Trigger::value);

    py::native_enum<EventType>(m, "EventType", "enum.Enum")
        .value("NeutronEvent", EventType::Neutron)
        .value("TriggerEvent", EventType::Trigger)
        .value("MdllNeutronEvent", EventType::MdllNeutron)
        .export_values()
        .finalize();

    py::class_<DecodedEvent>(m, "DecodedEvent")
        .def(py::init<>())
        .def_readonly("deviceId", &DecodedEvent::deviceId)
        .def_readonly("type", &DecodedEvent::type)
        .def_readonly("timestamp", &DecodedEvent::timestamp)
        .def("neutron",
                      [](const DecodedEvent &event) -> py::object
                      {
                        if (event.type == EventType::Neutron)
                            return py::cast(event.neutron);
                        else
                            return py::none();
                      })
        .def("trigger",
                      [](const DecodedEvent &event) -> py::object
                      {
                        if (event.type == EventType::Trigger)
                            return py::cast(event.trigger);
                        else
                            return py::none();
                      })
        .def("mdll_neutron",
                      [](const DecodedEvent &event) -> py::object
                      {
                        if (event.type == EventType::MdllNeutron)
                            return py::cast(event.mdllNeutron);
                        else
                            return py::none();
                      })
        .def("__str__", [](const DecodedEvent &event) { return to_string(event); });

    py::class_<DataPacket>(m, "DataPacket")
        .def(py::init<>())
        .def_readonly("runId", &DataPacket::runId)
        .def_readonly("device_status", &DataPacket::deviceStatus)
        .def_readonly("device_id", &DataPacket::deviceId)
        .def_readonly("buffer_type", &DataPacket::bufferType)
        .def_readonly("buffer_length", &DataPacket::bufferLength)
        .def_readonly("buffer_number", &DataPacket::bufferNumber)
        .def_property_readonly("time",
                               [](const DataPacket &packet)
                               {
                                   return py::array_t<u16>(
                                       {3},             // shape
                                       {sizeof(u16)},   // stride
                                       packet.time,     // pointer to data
                                       py::cast(packet) // base object to keep alive
                                   );
                               })

        .def_property_readonly("params",
                               [](const DataPacket &packet)
                               {
                                   return py::array_t<u16>(
                                       {McpdParamCount, McpdParamWords},            // shape
                                       {sizeof(u16) * McpdParamWords, sizeof(u16)}, // stride
                                       &packet.param[0][0], // pointer to data
                                       py::cast(packet)     // base object to keep alive
                                   );
                               })

        .def_property_readonly("data",
                               [](const DataPacket &packet)
                               {
                                   return py::array_t<u16>(
                                       {get_data_length(packet)}, // shape
                                       {sizeof(u16)},             // stride
                                       packet.data,               // pointer to data
                                       py::cast(packet)           // base object to keep alive
                                   );
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

    py::class_<ReadoutCounters>(m, "ReadoutCounters")
        .def(py::init<>())
        .def_readonly("packets", &ReadoutCounters::packets)
        .def_readonly("bytes", &ReadoutCounters::bytes)
        .def_readonly("timeouts", &ReadoutCounters::timeouts)
        .def_readonly("events", &ReadoutCounters::events);

    py::class_<Readout>(m, "Readout")
        .def(py::init<int>(), py::arg("listenPort") = McpdDefaultPort)
        .def("start", &Readout::start)
        .def("stop", &Readout::stop)
        .def("is_running", &Readout::isRunning)
        .def("get_packets", &Readout::getPackets)
        .def("get_counters", &Readout::getCounters)
        .def("has_readout_exception", &Readout::hasReadoutException)
        .def("get_readout_exception", &Readout::getReadoutException)
        .def("rethrow_readout_exception", &Readout::rethrowReadoutException);

    // Event field constants (maximum values)
    namespace ec = event_constants;

    py::module_ constants = m.def_submodule("constants", "Event field ranges");

    py::module_ mdll_neutron =
        constants.def_submodule("mdll_neutron", "MDLL neutron event field ranges");
    mdll_neutron.attr("amplitude_max") = (1u << ec::mdll_neutron::AmplitudeBits) - 1;
    mdll_neutron.attr("x_pos_max") = (1u << ec::mdll_neutron::xPosBits) - 1;
    mdll_neutron.attr("y_pos_max") = (1u << ec::mdll_neutron::yPosBits) - 1;

    py::module_ neutron = constants.def_submodule("neutron", "MPSD neutron event field ranges");
    neutron.attr("mpsd_id_max") = (1u << ec::neutron::MpsdIdBits) - 1;
    neutron.attr("channel_max") = (1u << ec::neutron::ChannelBits) - 1;
    neutron.attr("amplitude_max") = (1u << ec::neutron::AmplitudeBits) - 1;
    neutron.attr("position_max") = (1u << ec::neutron::PositionBits) - 1;

    py::module_ trigger = constants.def_submodule("trigger", "Trigger event field ranges");
    trigger.attr("trigger_id_max") = (1u << ec::trigger::TriggerIdBits) - 1;
    trigger.attr("data_id_max") = (1u << ec::trigger::DataIdBits) - 1;
    trigger.attr("data_max") = (1u << ec::trigger::DataBits) - 1;

    m.attr("timestamp_max") = (1u << ec::TimestampBits) - 1;

    py::module_ buffer_types = constants.def_submodule("buffer_types", "Data buffer types");
    buffer_types.attr("CommandPacketBufferType") = CommandPacketBufferType;
    buffer_types.attr("McpdDataBufferType") = McpdDataBufferType;
    buffer_types.attr("MdllDataBufferType") = MdllDataBufferType;
}
