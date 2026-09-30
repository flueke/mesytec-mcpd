"""Builds MCPD/MDLL data packets (see DataPacket in mcpd_core.h) and sends them via UDP.

Senders can bind to distinct loopback addresses (127.0.0.2, 127.0.0.3, ...) to
emulate multiple devices sending to the same data port.
"""

import socket
import struct

MDLL_DATA_BUFFER_TYPE = 0x0002
MCPD_DATA_BUFFER_TYPE = 0x0001
HEADER_WORDS = 21
MAX_DATA_WORDS = 715
PACKET_SIZE = 1472  # sizeof(DataPacket), the listfile record size

_HEADER = struct.Struct("<HHHHHBB3H12H")


def mdll_neutron(amplitude, x, y, timestamp=0):
    return (amplitude & 0xFF) << 39 | (y & 0x3FF) << 29 | (x & 0x3FF) << 19 | (timestamp & 0x7FFFF)


def mpsd_neutron(mpsd_id, channel, amplitude, position, timestamp=0):
    return (
        (mpsd_id & 0x7) << 44
        | (channel & 0x1F) << 39
        | (amplitude & 0x3FF) << 29
        | (position & 0x3FF) << 19
        | (timestamp & 0x7FFFF)
    )


def trigger_event(trigger_id=1, data_id=0, value=0, timestamp=0):
    return (
        1 << 47
        | (trigger_id & 0x7) << 44
        | (data_id & 0xF) << 40
        | (value & 0x1FFFFF) << 19
        | (timestamp & 0x7FFFF)
    )


def build_packet(
    events,
    device_id=0,
    buffer_number=0,
    buffer_type=MDLL_DATA_BUFFER_TYPE,
    run_id=0,
    device_status=0,
    header_timestamp=0,
    params=(0, 0, 0, 0),
):
    """Returns the packet bytes, bufferLength * 2 bytes long like on the wire."""
    data = []
    for ev in events:
        data += [ev & 0xFFFF, (ev >> 16) & 0xFFFF, (ev >> 32) & 0xFFFF]
    if len(data) > MAX_DATA_WORDS:
        raise ValueError("too many events for one packet")

    def split48(v):
        return [v & 0xFFFF, (v >> 16) & 0xFFFF, (v >> 32) & 0xFFFF]

    param_words = []
    for p in params:
        param_words += split48(p)

    header = _HEADER.pack(
        HEADER_WORDS + len(data),
        buffer_type,
        HEADER_WORDS,
        buffer_number & 0xFFFF,
        run_id,
        device_status,
        device_id,
        *split48(header_timestamp),
        *param_words,
    )
    return header + struct.pack(f"<{len(data)}H", *data)


class DataSender:
    def __init__(self, dest_port, src_ip="127.0.0.1", dest_ip="127.0.0.1"):
        self.dest = (dest_ip, dest_port)
        self.src_ip = src_ip
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.bind((src_ip, 0))

    @property
    def src_addr(self):
        return struct.unpack("!I", socket.inet_aton(self.src_ip))[0]

    def send_raw(self, data: bytes):
        self._sock.sendto(data, self.dest)

    def send(self, events, **kwargs):
        self.send_raw(build_packet(events, **kwargs))

    def close(self):
        self._sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
