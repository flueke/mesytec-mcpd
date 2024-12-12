# Environment variables read by mcpd-cli. Saves us from having to specify
# --address and --id for every command.

# For MCPD-8_v1 which uses fixed ip addresses
#export MCPD_ADDRESS=192.168.168.221
#export MCPD_ID=0

# For MCPD-8_v2 which can use DHCP
export MCPD_ADDRESS=mcpd-0029
export MCPD_ID=0

# Attempt to connect and read the MCPD firmware revisions, then scan the
# busses for attached (MPSD) devices.
mcpd-cli version
mcpd-cli scan_busses

# Assumes the MPSD is plugged into bus 2 of the MCPD.
# mpsd_set_pulser       <mpsdId> <mpsdChannel> <position> <amplitude> <state>
mcpd-cli mpsd_set_pulser 2        3             2          100         on

# Start the DAQ. The MCPD will start to send out event packets.
mcpd-cli daq start

# Start a readout process. It will receive data on port 54321. Do not write a
# listfile but print detailed information about incoming packets. Pulser
# generated neutron events and trigger events (given that there are rising
# edges at input 0) should be present in the datastream.
mcpd-cli readout --no-listfile --print-packet-summary --print-event-data --duration 10000

# Stop the DAQ. MCPD won't send out any more data packets.
mcpd-cli daq stop

