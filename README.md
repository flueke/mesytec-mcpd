# mesytec-mcpd  {#mainpage}

*User space driver library for the Mesytec PSD+ system.*

## mcpd-cli command line tool


### Initial Setup

* MCPD default IP Address is 192.168.168.121
* MCPD default ID is 0
* Reset to defaults by pressing the button on the CPU board inside the NIM case.
* MCPD data destination port by default is the same as the command port: 54321

  The library requires to use a distinct data port so that a dedicated socket
  can be used for the DAQ readout. The default data port is 54322.

    mcpd [--address] [--id=0] setup <newaddress> <newid> <dataport>=54322


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
    mcpd [--address] [--id] mpsd_set_mode <mpsd_id> <mode>
    mcpd [--address] [--id] mpsd_get_parameters <mpsd_id>


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

    mcpd readout [--dataport=54322] [--duration=<seconds>] --listfile=<listfile.mcpdlst> [--no-listfile] [--print-packet-summary] [--print-event-data]

### Replay

    mcpd replay --print-packet-summary --print-event-data --listfile=<listfile.mcpdlst>

## mcpd commands


| Command Name      | Command Id    | Notes     |
| ---               | ---           | ---       |
| [Reset](#cmd_reset)             | 0 |
| StartDAQ          | 1 |
| StopDAQ           | 2 |
| ContinueDAQ       | 3 |
| SetId             | 4 |
| SetProtoParams    | 5 |
| SetTiming         | 6 |
| SetClock          | 7 |
| SetRunId          | 8 |
| SetCell           | 9 |
| SetAuxTimer       | 10 |
| SetParam          | 11 |
| GetParams         | 12 |
| SetGain           | 13 |
| SetThreshold      | 14 |
| SetPulser         | 15 |
| SetMpsdMode       | 16 |
| SetDAC            | 17 |
| SendSerial        | 18 | // Note: not implemented in the firmware
| ReadSerial        | 19 | // Note: not implemented in the firmware
| ScanPeriphery     | 20 | // FIXME: not in docs!
| SetTTLOutputs     | 21 |
| GetBusCapabilities | 22 |
| SetBusCapabilities | 23 |
| GetMpsdParams     | 24 |
| SetFastTxMode     | 25 |
| ReadId            | 36 | // FIXME: not in docs, scans the busses for MPSD-8 modules 
| GetVersion        | 51 |


### cmd_reset
