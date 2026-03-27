import logging
import queue
import time

import mesytec_mcpd as mcpd
import numpy as np
from rich.logging import RichHandler

mcpd.set_log_level("trace")
rdo = mcpd.Readout()
rdo.start()
time.sleep(1)
# For a clean shutdown either rdo.stop() is needed or the object has to be deleted.
del rdo
# rdo.stop()

if __name__ == "__main__":
    logging.basicConfig(
        level="DEBUG",
        format="%(name)s %(message)s",
        datefmt="[%X]",
        handlers=[RichHandler()],
    )
    mcpd.set_log_level("info")

    rdo = mcpd.Readout(queue_size=10000)
    rdo.start()
    input_queue = rdo.get_queue()
    packet_count = 0
    event_count = 0

    time_to_run = 0
    t_start = time.time()


    try:
        while True:
            try:
                augPacket = input_queue.get()
                packet_count += 1
                raw_events = np.array(augPacket, copy=True)
                event_count += raw_events.size
                print(f"{augPacket.packet.event_count()=}")

                if time_to_run > 0 and time.time() - t_start > time_to_run:
                    print(f"Time limit of {time_to_run} seconds reached.")
                    break

            except queue.ShutDown:
                break

    except KeyboardInterrupt:
        pass

    print("Stopping readout...")
    rdo.stop()
    print(f"Readout finished. Got {packet_count} packets with {event_count} events.")
    print("Final Counters:", rdo.get_counters())
