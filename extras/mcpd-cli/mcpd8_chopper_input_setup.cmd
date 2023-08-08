:: Environment variables read by mcpd-cli. Saves us from having to specify
:: --address and --id for every command.
set MCPD_ADDRESS=192.168.168.211
set MCPD_ID=0

:: Attempt to connect and read the MCPD firmware revisions, then scan the
:: busses for attached (MPSD) devices.
mcpd-cli version
mcpd-cli scan_busses

:: Setup monitor/chopper input 0. "7" means "compare register", "22" sets the
:: trigger to "rising edge of input".
:: Now for every rising edge on input 0 a trigger event will be generated and
:: inserted into the data stream.
mcpd-cli cell 0 7 22

:: Setup MCPD to receive Time, Position and Amplitude from MPSDs.
mcpd-cli set_bus_capabilities 4

:: Assuming there is an mpsd present on bus 0. Enable its pulser for channel
:: 3, middle position (2) and set the amplitude to 100.
:: Set to "off" to only see triggers events.
mcpd-cli mpsd_set_pulser 0 3 2 100 off

:: Start the DAQ. The MCPD will start to send out event packets.
mcpd-cli daq start

:: Start a readout process. It will receive data on port 54321. Do not write a
:: listfile but print detailed information about incoming packets. Pulser
:: generated neutron events and trigger events (given that there are rising
:: edges at input 0) should be present in the datastream.
mcpd-cli readout --no-listfile --print-packet-summary --print-event-data --duration 10

:: Stop the DAQ. MCPD won't send out any more data packets.
mcpd-cli daq stop
