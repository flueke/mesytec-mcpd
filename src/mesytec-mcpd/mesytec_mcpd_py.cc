#include <pybind11/pybind11.h>
#include <pybind11/native_enum.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <mesytec-mcpd/mesytec-mcpd.h>

namespace py = pybind11;
using namespace mesytec::mcpd;

PYBIND11_MODULE(mesytec_mcpd_py, m)
{
    m.doc() = "driver library for the mesytec PSD system (MCPD, MPSD, MDLL) - python bindings";
    m.attr("__version__") = library_version();

    py::class_<DecodedEvent>(m, "DecodedEvent")
        .def(py::init<>())
        .def_readonly("deviceId", &DecodedEvent::deviceId)
        .def_readonly("type", &DecodedEvent::type)
        .def_readonly("timestamp", &DecodedEvent::timestamp)
        .def_readonly("neutron", &DecodedEvent::neutron)
        .def_readonly("trigger", &DecodedEvent::trigger)
        .def_readonly("mdllNeutron", &DecodedEvent::mdllNeutron)
        .def("__str__", [](const DecodedEvent &event) { return to_string(event); })
        ;

    py::class_<DecodedEvent::Neutron>(m, "Neutron")
        .def(py::init<>())
        .def_readonly("mpsdId", &DecodedEvent::Neutron::mpsdId)
        .def_readonly("channel", &DecodedEvent::Neutron::channel)
        .def_readonly("amplitude", &DecodedEvent::Neutron::amplitude)
        .def_readonly("position", &DecodedEvent::Neutron::position)
        ;

    py::class_<DecodedEvent::MdllNeutron>(m, "MdllNeutron")
        .def(py::init<>())
        .def_readonly("amplitude", &DecodedEvent::MdllNeutron::amplitude)
        .def_readonly("xPos", &DecodedEvent::MdllNeutron::xPos)
        .def_readonly("yPos", &DecodedEvent::MdllNeutron::yPos)
        ;

    py::native_enum<EventType>(m, "EventType", "enum.Enum")
        .value("NeutronEvent", EventType::Neutron)
        .value("TriggerEvent", EventType::Trigger)
        .value("MdllNeutronEvent", EventType::MdllNeutron)
        .export_values()
        .finalize()
        ;

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

        .def_property_readonly("param", [](const DataPacket &packet)
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

        ;
}
