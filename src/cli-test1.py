import awkward as ak
import numba as nb
import numpy as np
import mesytec_mcpd_py as mcpd
import time
import sys
import platformdirs
import logging
from rich.logging import RichHandler

from mesytec_mcpd_py import constants as mc

# All possible data members:
#
# All         Neutron         MdllNeutron     Trigger         outside
# timestamp   mpsd_id         amplitude       trigger_id      buffer_type
# device_id   channel         x_pos           data_id
# event_type  position

##@nb.njit(nb.int32(nb.uint64))
#@nb.vectorize([nb.int32(nb.uint64)])
def get_event_type(raw_event) -> np.uint8:
    s = np.uint64(mc.event_type_shift)
    m = np.uint64(mc.event_type_mask)
    return np.uint8((raw_event >> s) & m)

#@nb.vectorize([nb.int32(nb.int32, nb.uint64)])
#@nb.njit
def decode_raw_events(buffer_type: int, raw_events: np.ndarray) -> ak.Array:
    return ak.Array(
        {
            "event_type": get_event_type(raw_events),
        })

    #return get_event_type(raw_events)


def decode_events(aug_packet: mcpd.AugmentedDataPacket) -> ak.Array:
    return decode_raw_events(aug_packet.packet.buffer_type, aug_packet.packet.get_raw_events())


if __name__ == "__main__":
    logging.basicConfig(
        level="DEBUG",
        format="%(name)s %(message)s",
        datefmt="[%X]",
        handlers=[RichHandler()],
    )

    doc_dir = platformdirs.user_documents_dir()
    input_filepath = f"{doc_dir}/mcpd-cli-two-mdllv2-pulsertest-00.mcpdlst"

    ripley = mcpd.Replay(filename=input_filepath, packetBufferMaxPackets=10)
    logging.info(f"input_filepath: {input_filepath}")

    ripley.start()
    logging.info("Started ellen ripley replay...")
    try:
        while ripley.is_running():
            print(f"Waiting for packets...{ripley.get_packet_count()} packets in buffer")
            if (packet_count := ripley.get_packet_count()) > 0:
                print(f"Packet count: {packet_count}")
                packets = ripley.get_packets()
                print(f"Got {len(packets)} packets from replay")

                #for aug_packet in packets:
                #    packet = aug_packet.packet
                #    print(f"Packet with {packet.event_count()} events")
                    #print(f"{packet.get_raw_events()[:10]}")
                    #decoded = decode_events(aug_packet)
                    #print(decoded.layout, decoded.fields)
                    #print(decode_events(packet))
                    #raw_events = packet.get_raw_events()
                    #raw_events = raw_events_to_ak(raw_events=raw_events)
            else:
                time.sleep(0.1)

        print("Replay finished.")

    except KeyboardInterrupt:
        print("Stopping ellen ripley...")
        ripley.stop()
