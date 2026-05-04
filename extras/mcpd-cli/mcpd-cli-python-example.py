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

class Context:
    n_packets = 0
    n_events = 0

ctx = Context()


def process_packet(packet: mcpd.DataPacket):
    global ctx
    print(f"hello from process_packet: {packet}")
    ctx.n_packets = ctx.n_packets + 1

def process_event(event: mcpd.DecodedEvent):
    global ctx
    print(f"hello from process_event: {event.type}")
    ctx.n_events = ctx.n_events + 1

def start():
    print("python start!")

def stop():
    global ctx
    print(f"python stop! {ctx.n_packets=}, {ctx.n_events=}")
