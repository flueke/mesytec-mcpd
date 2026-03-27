import awkward as ak
import numba as nb
import numpy as np
import mesytec_mcpd as mcpd
import time
import sys

from mesytec_mcpd import constants as mc

# All possible data members:
#
# All         Neutron         MdllNeutron     Trigger         outside
# timestamp   mpsd_id         amplitude       trigger_id      buffer_type
# device_id   channel         x_pos           data_id
# event_type  position

#@nb.njit(nb.int32(nb.uint64))
@nb.vectorize([nb.int32(nb.uint64)])
def get_event_type(raw_event: np.uint64) -> np.uint8:
    s = np.uint64(mc.event_type_shift)
    m = np.uint64(mc.event_type_mask)
    return np.uint8((raw_event >> s) & m)

@nb.vectorize([nb.int32(nb.uint64)])

def decode_raw_events(buffer_type: int, raw_events: np.ndarray) -> ak.Array:
    return ak.Array(
        {
            "event_type": get_event_type(raw_events),
        })

    #return get_event_type(raw_events)


def decode_events(aug_packet: mcpd.AugmentedDataPacket) -> ak.Array:
    return decode_raw_events(aug_packet.buffer_type, aug_packet.packet.get_raw_events())


if __name__ == "__main__":

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
