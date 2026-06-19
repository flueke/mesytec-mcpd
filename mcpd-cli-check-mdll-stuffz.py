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
    prev_event_ts = None
    prev_packet_ts = None
    prev_packet_recv_time_ns = None

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
    ctx.csv_writer.writerow(("type", "packet#", "dt_packet_recv_time_s", "packet_ts", "prev_packet_ts",\
                             "dt_packet_ts", "rel_event#", "abs_event#", "event_ts", "prev_event_ts",\
                             "dt_event_ts"))

def stop():
    global ctx
    print(f"python stop! {ctx.n_packets=}, {ctx.n_events=}")
    del ctx.csv_writer
    del ctx.csv_out

    ctx.csv_writer = None
    ctx.csv_out = None

def process_packet(packet: mcpd.DataPacket):
    global ctx

    now_ns = time.perf_counter_ns()
    dt_packet_recv_time_ns = None

    if ctx.prev_packet_recv_time_ns is not None:
        dt_packet_recv_time_ns = now_ns - ctx.prev_packet_recv_time_ns
        dt_packet_recv_time_s = dt_packet_recv_time_ns / 1e9

    ctx.prev_packet_recv_time_ns = now_ns

    if ctx.prev_packet_ts is not None:
        dt_packet_ts = packet.header_timestamp - ctx.prev_packet_ts

        #print(f"packet#{ctx.n_packets}, dt_packet_recv_time_s={dt_packet_recv_time_s}, packet_ts={packet.header_timestamp}, prev_packet_ts={ctx.prev_packet_ts}, dt_packet_ts={dt_packet_ts}", flush=True)

        if ctx.csv_writer is not None:
            ctx.csv_writer.writerow(("packet", ctx.n_packets, dt_packet_recv_time_s, packet.header_timestamp, ctx.prev_packet_ts, dt_packet_ts, None, None, None, None, None))

    ctx.prev_packet_ts = packet.header_timestamp

    for event_idx, event in enumerate(packet.get_decoded_events()):
        if ctx.prev_event_ts is not None:
            dt_event_ts = event.timestamp - ctx.prev_event_ts
            #print(f"  packet#{ctx.n_packets}, event#{event_idx}: event_ts={event.timestamp}, prev_event_ts={ctx.prev_event_timestamp}, dt={dt_event_ts}", flush=True)
            if ctx.csv_writer is not None:
                ctx.csv_writer.writerow(("event", ctx.n_packets, None, None, None, None, event_idx, ctx.n_events, event.timestamp, ctx.prev_event_ts, dt_event_ts))

        ctx.prev_event_ts = event.timestamp
        ctx.n_events = ctx.n_events + 1

    ctx.n_packets = ctx.n_packets + 1
