import awkward as ak
import numba as nb
import numpy as np
import mesytec_mcpd_py as mcpd
import time

from mesytec_mcpd_py import constants as mc

# All possible data members:
#
# All         Neutron         MdllNeutron     Trigger         outside
# timestamp   mpsd_id         amplitude       trigger_id      buffer_type
# device_id   channel         x_pos           data_id
# event_type  position

#@nb.njit(nb.int32(nb.uint64))
@nb.vectorize([nb.int32(nb.uint64)])
def get_event_type(raw_event):
    return (raw_event >> mc.event_type_shift) & mc.event_type_mask

@nb.vectorize([nb.int32(nb.uint64)])

def decode_raw_events(buffer_type: int, raw_events: np.ndarray) -> ak.Array:
    return ak.Array(
        {
            "event_type": get_event_type(raw_events),
        })

    #return get_event_type(raw_events)


def decode_events(packet: mcpd.DataPacket) -> ak.Array:
    return decode_raw_events(packet.buffer_type, packet.get_raw_events())


rdo = mcpd.Readout()
rdo.start()
try:
    while rdo.is_running():
        if (packet_count := rdo.get_packet_count()) > 0:
            print(f"Packet count: {packet_count}")
            packets = rdo.get_packets()

            for packet in packets:
                print(f"Packet with {packet.event_count()} events")
                decoded = decode_events(packet)
                print(decoded.layout, decoded.fields)
                #print(decode_events(packet))
                #raw_events = packet.get_raw_events()
                #raw_events = raw_events_to_ak(raw_events=raw_events)
        else:
            time.sleep(0.1)

except KeyboardInterrupt:
    print("Stopping readout...")
    rdo.stop()
