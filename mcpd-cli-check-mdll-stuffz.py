# Example script for python support in mcpd-cli. This script can be used with
# the '--python-script' option of mcpd-clis 'readout' and 'replay' commands.
#
# mcpd-cli does an implicit 'import _mesytec_mcpd as mcpd', so there's no need
# to manually import the module here.
#
# Only one of process_packet() and process_event() has to be defined. If both
# are defined, both will be called.
#
# In process_packet() you can get the contained events via
# packet.get_decoded_events().
#
# The binding code can be found in mesytec_mcpd_py.cc. To view the generated
# docstrings use 'import mesytec_mcpd as mcpd; help(mcpd)' in python.

import csv
import time
import sys

class Context:
    csv_out = None
    csv_writer = None
    n_packets = 0
    n_events = 0
    prev_packet_timestamp = None
    prev_full_timestamp = None

ctx = Context()

def start(listfilePath: str, args: list[str]):

    csv_output_name = "mdll_readout.csv"

    if len(listfilePath) > 0:
        # replace whatever extension there is with .csv
        # if there is no extension, just append .csv
        csv_output_name = listfilePath.rsplit('.', 1)[0] + ".csv"

    print(f"python start! {csv_output_name=}, {args=}")

    ctx.csv_out = open(csv_output_name, 'w', newline='', encoding='utf-8')
    ctx.csv_writer = csv.writer(ctx.csv_out)

    ctx.csv_writer.writerow(("type", "packet#", "event#", "rel_event#",
                              "packet_header_timestamp", "dt_packet_header_timestamp",
                              "event_timestamp", "dt_event_timestamp",
                              "full_timestamp", "dt_full_timestamp",
                              "raw_packet_words"
                              ))

def stop():
    global ctx
    print(f"python stop! {ctx.n_packets=}, {ctx.n_events=}")
    del ctx.csv_writer
    del ctx.csv_out

    ctx.csv_writer = None
    ctx.csv_out = None

def process_packet(packet: mcpd.DataPacket):
    global ctx

    dt_packet_timestamp = None
    if ctx.prev_packet_timestamp is not None:
        dt_packet_timestamp = packet.packet_timestamp - ctx.prev_packet_timestamp
    ctx.prev_packet_timestamp = packet.packet_timestamp

    raw_words = packet.get_raw_words()
    formatted_raw_words = " ".join(f"{w:#06x}" for w in raw_words)

    ctx.csv_writer.writerow(("packet", ctx.n_packets, None, None,
                              packet.packet_timestamp, dt_packet_timestamp,
                              None, None,
                              None, None,
                              formatted_raw_words))

    prev_event_timestamp = None

    for event_idx, event in enumerate(packet.get_decoded_events()):

        # packet relative event timestamp delta
        dt_event_timestamp = None
        if prev_event_timestamp is not None:
            dt_event_timestamp = event.event_timestamp - prev_event_timestamp
        prev_event_timestamp = event.event_timestamp

        # full event timestamp delta
        dt_full_timestamp = None
        if ctx.prev_full_timestamp is not None:
            dt_full_timestamp = event.timestamp - ctx.prev_full_timestamp
        ctx.prev_full_timestamp = event.timestamp

        ctx.csv_writer.writerow((str(event.type), ctx.n_packets, ctx.n_events, event_idx,
                                 #event.packet_timestamp, dt_packet_timestamp,
                                 None, None,
                                 event.event_timestamp, dt_event_timestamp,
                                 event.timestamp, dt_full_timestamp,
                                 None))


        ctx.n_events = ctx.n_events + 1

    ctx.n_packets = ctx.n_packets + 1
