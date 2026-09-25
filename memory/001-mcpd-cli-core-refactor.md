# TODO: port mcpd-cli to the shared readout core

Requested by Florian on 2026-09-25: after the readout core refactor (packet
sources, listfile writer, DaqWorker, per-source stats, MDLL/MCPD histogrammers
in src/mesytec-mcpd) is done, remind him to switch the C++ mcpd-cli
(extras/mcpd-cli/mcpd-cli.cc) `readout`/`replay` commands over to it.

Things to replace there:
- the hand-rolled receive/listfile/replay loops
- calc_packet_loss(): single global lastBufferNumber (wrong with multiple
  sources) and a repeated buffer number counts as 65535 lost packets
- ReadoutCounters / report_counters()
- the --python-script hook could become a PacketConsumer
- mcpd_root_histos could become a consumer fed from the new histogrammers
