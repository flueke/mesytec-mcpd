"""A minimal, pure-Python fake MCPD/MDLL command responder.

Implements just enough of the wire protocol (see mcpd_core.h/mcpd_functions.cc)
to exercise McpdConnection end-to-end over real UDP sockets on loopback,
without needing real hardware. Not a full device simulator: unrecognized/most
"setter" commands are answered with a generic ack (request data echoed back
unmodified, no error bit), which is sufficient because the real client code
only inspects response data for a handful of "getter" style commands - those
are special-cased below.

Keep the CommandType values in sync with mcpd_core.h if that enum changes.
"""

import socket
import struct
import threading

COMMAND_PACKET_BUFFER_TYPE = 0x8000
COMMAND_NUMBER_MASK = 0x00FF
COMMAND_ERROR_SHIFT = 8
COMMAND_HEADER_WORDS = 10
BUFFER_TERMINATOR = 0xFFFF
DATA_WORDS = 726

# Mirrors mcpd_core.h CommandError::IdMismatch.
ID_MISMATCH_ERROR = 128

# Mirrors mcpd_core.h CommandType (values that matter for the fake device).
CMD_RESET = 0
CMD_START_DAQ = 1
CMD_STOP_DAQ = 2
CMD_CONTINUE_DAQ = 3
CMD_SET_ID = 4
CMD_GET_PARAMS = 12
CMD_GET_BUS_CAPABILITIES = 22
CMD_SET_BUS_CAPABILITIES = 23
CMD_GET_VERSION = 51
CMD_WRITE_REGISTER = 80
CMD_READ_REGISTER = 81

# "<" (little-endian, no padding) + header fields + fixed-size data array.
_PACKET_STRUCT = struct.Struct("<HHHHHBBHHHH{}H".format(DATA_WORDS))
_HEADER_FIELDS = (
    "buffer_length",
    "buffer_type",
    "header_length",
    "buffer_number",
    "cmd",
    "device_status",
    "device_id",
    "time0",
    "time1",
    "time2",
    "header_checksum",
)


def _checksum(words):
    result = 0
    for w in words:
        result ^= w
    return result & 0xFFFF


def _decode(raw):
    values = _PACKET_STRUCT.unpack(raw.ljust(_PACKET_STRUCT.size, b"\0")[: _PACKET_STRUCT.size])
    header = dict(zip(_HEADER_FIELDS, values[: len(_HEADER_FIELDS)]))
    data = list(values[len(_HEADER_FIELDS) :])
    return header, data


def _encode(cmd, device_id, data_words, error=0):
    data = list(data_words) + [BUFFER_TERMINATOR]
    data = (data + [0] * DATA_WORDS)[:DATA_WORDS]
    header_length = COMMAND_HEADER_WORDS
    buffer_length = header_length + len(data_words) + 1
    words = [
        buffer_length,
        COMMAND_PACKET_BUFFER_TYPE,
        header_length,
        0,  # buffer_number
        cmd | (error << COMMAND_ERROR_SHIFT),
    ]
    checksum = _checksum(words + [0, 0, 0, 0])  # status/id (1 word) + time (3 words), all zero
    raw = _PACKET_STRUCT.pack(
        buffer_length,
        COMMAND_PACKET_BUFFER_TYPE,
        header_length,
        0,
        cmd | (error << COMMAND_ERROR_SHIFT),
        0,  # device_status
        device_id,
        0,
        0,
        0,
        checksum,
        *data,
    )
    # Only the leading buffer_length words are meaningful/sent on the wire.
    return raw[: buffer_length * 2]


class FakeMcpdDevice:
    """Runs a background thread answering MCPD/MDLL command packets."""

    def __init__(self, host="127.0.0.1", port=0, mcpd_id=0, enforce_id=False):
        self.mcpd_id = mcpd_id
        self.enforce_id = enforce_id
        self.cpu_version = (1, 0)
        self.fpga_version = (2, 3)
        self.bus_capabilities = (0x07, 0x01)
        self.registers = {}
        self.daq_state = "idle"

        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.bind((host, port))
        self._sock.settimeout(0.1)
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._serve, daemon=True)

    @property
    def port(self):
        return self._sock.getsockname()[1]

    def start(self):
        self._thread.start()

    def stop(self):
        self._stop.set()
        self._thread.join()
        self._sock.close()

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *exc_info):
        self.stop()

    def _serve(self):
        while not self._stop.is_set():
            try:
                raw, addr = self._sock.recvfrom(_PACKET_STRUCT.size)
            except (TimeoutError, socket.timeout):
                continue
            except OSError:
                break

            header, data = _decode(raw)
            response = self._handle(header, data)
            if response is not None:
                self._sock.sendto(response, addr)

    def _handle(self, header, data):
        cmd = header["cmd"] & COMMAND_NUMBER_MASK
        device_id = header["device_id"]

        if self.enforce_id and device_id != self.mcpd_id:
            return _encode(cmd, self.mcpd_id, [], error=ID_MISMATCH_ERROR)

        if cmd == CMD_GET_VERSION:
            cpu_major, cpu_minor = self.cpu_version
            fpga_major, fpga_minor = self.fpga_version
            return _encode(
                cmd, device_id, [cpu_major, cpu_minor, (fpga_major << 8) | fpga_minor]
            )

        if cmd == CMD_SET_ID:
            self.mcpd_id = data[0]
            return _encode(cmd, device_id, [])

        if cmd == CMD_WRITE_REGISTER:
            address, lo, hi = data[0], data[1], data[2]
            self.registers[address] = lo | (hi << 16)
            return _encode(cmd, device_id, [address, lo, hi])

        if cmd == CMD_READ_REGISTER:
            address = data[0]
            value = self.registers.get(address, 0)
            return _encode(cmd, device_id, [address, value & 0xFFFF, (value >> 16) & 0xFFFF])

        if cmd == CMD_GET_BUS_CAPABILITIES:
            available, selected = self.bus_capabilities
            return _encode(cmd, device_id, [available, selected])

        if cmd == CMD_SET_BUS_CAPABILITIES:
            self.bus_capabilities = (self.bus_capabilities[0], data[0])
            return _encode(cmd, device_id, [data[0]])

        if cmd == CMD_START_DAQ:
            self.daq_state = "running"
            return _encode(cmd, device_id, [])

        if cmd in (CMD_STOP_DAQ, CMD_RESET):
            self.daq_state = "idle"
            return _encode(cmd, device_id, [])

        if cmd == CMD_CONTINUE_DAQ:
            self.daq_state = "running"
            return _encode(cmd, device_id, [])

        if cmd == CMD_GET_PARAMS:
            return _encode(cmd, device_id, [])

        # Generic ack for everything else (mdll_set_*, mpsd_set_*, setup_cell,
        # set_param_source, set_dac_output_values, set_timing_options, ...):
        # echo the request data back unmodified, no error.
        return _encode(cmd, device_id, _request_data(data))


def _request_data(data):
    # Strip the trailing zero padding / terminator word added by the client;
    # only whatever precedes the first BUFFER_TERMINATOR is real request data.
    if BUFFER_TERMINATOR in data:
        return data[: data.index(BUFFER_TERMINATOR)]
    return data
