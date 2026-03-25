import queue

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
# event_type  position        y_pos


##@nb.njit(nb.int32(nb.uint64))
# @nb.vectorize([nb.int32(nb.uint64)])
def get_event_type(raw_event) -> np.uint8:
    s = np.uint64(mc.event_type_shift)
    m = np.uint64(mc.event_type_mask)
    return np.uint8((raw_event >> s) & m)


# @nb.vectorize([nb.int32(nb.int32, nb.uint64)])
# @nb.njit
def decode_raw_events(buffer_type: int, raw_events: np.ndarray) -> ak.Array:
    return ak.Array(
        {
            "event_type": get_event_type(raw_events),
        }
    )

    # return get_event_type(raw_events)


def decode_events(aug_packet: mcpd.AugmentedDataPacket) -> ak.Array:
    return decode_raw_events(aug_packet.packet.buffer_type, aug_packet.packet.get_raw_events())


if __name__ == "__main__":
    logging.basicConfig(
        level="DEBUG",
        format="%(name)s %(message)s",
        datefmt="[%X]",
        handlers=[RichHandler()],
    )
    mcpd.set_log_level("info")

    doc_dir = platformdirs.user_documents_dir()
    input_filepath = f"{doc_dir}/mcpd-cli-two-mdllv2-pulsertest-00.mcpdlst"

    ripley = mcpd.Replay(filename=input_filepath, queue_size=1000)
    logging.info(f"input_filepath: {input_filepath}")

    ripley.start()
    logging.info("Started ellen ripley replay...")
    input_queue = ripley.get_queue()
    packet_count = 0
    event_count = 0
    try:
        while True:
            # print("Waiting for packets...")
            try:
                packet = input_queue.get()
                packet_count += 1
                raw_events = np.array(packet, copy=True)
                event_count += raw_events.size

            except queue.ShutDown:
                print(f"Replay finished. Got {packet_count} packets with {event_count} events.")
                print("Final Counters:", ripley.get_counters())
                break

    except KeyboardInterrupt:
        print("Stopping ellen ripley...")
        ripley.stop()
