mesytec-mcpd  {#mainpage}
=========================

*User space driver library for the Mesytec PSD+ system.*


Initial Setup
-------------

* MCPD default IP Address is 192.168.168.121
* MCPD default ID is 0
* Reset to defaults by pressing the button on the CPU board inside the NIM case
* MCPD data destination port by default is the same as the command port: 54321
  The library requires to use a distinct data port so that a dedicated socket
  can be used for the DAQ readout.

    mcpd setup [--current-address=192.168.168.121] [--current-id=0] <newaddress> [<newid>=0] [<dataport>=54322]


MCPD and MPSD Configuration
---------------------------
    mcpd [--address] [--id] timing <master|slave> <term=on|off>
    mcpd [--address] [--id] runid <runid>
    mcpd [--address] [--id] cell <cellid> <triggervalue> [<comparevalue>]
    mcpd [--address] [--id] timer <timerid <capturevalue>
    mcpd [--address] [--id] param_source <paramid> <sourceid>
    mcpd [--address] [--id] get_parameters
    mcpd [--address] [--id] version
    mcpd [--address] [--id] dac_setup --dac0=<12bit> --dac1=<12bit>

    mcpd [--address] [--id] mpsd set_gain <mpsd_id> <mpsd_channel> <gain>
    mcpd [--address] [--id] mpsd set_threshold <mpsd_id> <threshold>
    mcpd [--address] [--id] mpsd set_pulser <mpsd_id> <mpsd_channel> <pos> <amplitude> <on|off>
    mcpd [--address] [--id] mpsd set_mode <mpsd_id> <mode>
    mcpd [--address] [--id] mpsd get_parameters <mpsd_id>


DAQ control
-----------

    mcpd [--address] [--id] daq start
    mcpd [--address] [--id] daq stop
    mcpd [--address] [--id] daq continue
    mcpd [--address] [--id] daq reset

Readout process
---------------

TODO: make this multi MCPD capable!

Readout until canceled via ctrl-c. Output data rates to stdout. Use one socket
to receive datagrams from any source address on the specified port. Write
packets to the output listfile.

    mcpd readout [--dataport=54322] <listfile.mcpdlst>
