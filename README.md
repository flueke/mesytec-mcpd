# mesytec-mcpd  {#mainpage}

*User space driver library for the Mesytec PSD+ system.*

## Introduction

## Installation

## mcpd-cli command line tool

### Initial Setup

* MCPD default IP Address is 192.168.168.121
* MCPD default ID is 0
* Reset to defaults by pressing the button on the CPU board inside the NIM case.
* MCPD data destination port by default is the same as the command port: 54321

  The library requires to use a distinct data port so that a dedicated socket
  can be used for the DAQ readout. The default data port is 54322.

    mcpd [--address] [--id=0] setup <newaddress> <newid> <dataDestAddress> <dataport>=54322


### MCPD and MPSD Configuration

    mcpd [--address] [--id] timing <master|slave> <term=on|off>
    mcpd [--address] [--id] runid <runid>
    mcpd [--address] [--id] cell <cellid> <triggervalue> [<comparevalue>]
    mcpd [--address] [--id] timer <timerid> <capturevalue>
    mcpd [--address] [--id] param_source <paramid> <sourceid>
    mcpd [--address] [--id] get_parameters
    mcpd [--address] [--id] version
    mcpd [--address] [--id] dac_setup <dac0> <dac1>
    mcpd [--address] [--id] scan_busses
    
    mcpd [--address] [--id] mpsd_set_gain <mpsd_id> <mpsd_channel> <gain>
    mcpd [--address] [--id] mpsd_set_threshold <mpsd_id> <threshold>
    mcpd [--address] [--id] mpsd_set_pulser <mpsd_id> <mpsd_channel> <pos> <amplitude> <on|off>
    mcpd [--address] [--id] mpsd_get_parameters <mpsd_id>

    Not implemented:
    mcpd [--address] [--id] mpsd_set_mode <mpsd_id> <mode>


### DAQ control

These commands are used to start and stop the MCPD based DAQ.

Note that these commands to not perform the actual data readout!

    mcpd [--address] [--id] daq start
    mcpd [--address] [--id] daq stop
    mcpd [--address] [--id] daq continue
    mcpd [--address] [--id] daq reset

### Readout process

Readout until canceled via ctrl-c. Output data rates to stdout. Use one socket
to receive datagrams from any source address on the specified port. Write
packets to the output listfile.

    mcpd readout [--dataport=54322] [--duration=<seconds>] --listfile=<listfile.mcpdlst> [--no-listfile]
        [--print-packet-summary] [--print-event-data] [--root-histo-file=<histos.root>]

### Replay

    mcpd replay --print-packet-summary --print-event-data --listfile=<listfile.mcpdlst>
        [--root-histo-file=<histos.root>]
