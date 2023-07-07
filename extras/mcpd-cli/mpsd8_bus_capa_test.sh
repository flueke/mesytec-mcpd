#!/usr/bin/env bash

# Script to test the bus tx format/capabilities settings using an MCPD-8
# pulser.
#
# Requires mcpd-cli to be built with ROOT support.
#
# The script sets the required parameters and starts a short DAQ run recording
# histograms to a ROOT file. All valid bus tx formats are run in order 1, 2, 4.

# Env vars used by mcpd-cli
export MCPD_ADDRESS=192.168.168.211
export MCPD_ID=1
MPSD_BUS=5
MPSD_PULSER_ON="${MPSD_BUS} 1 2 100 on"
MPSD_PULSER_OFF="${MPSD_BUS} 1 2 100 off"
DAQ_DURATION=10

trap "exit" INT

for txfmt in 1  2  4; do
    mcpd-cli version
    mcpd-cli scan_busses
    mcpd-cli mpsd_set_tx_format $MPSD_BUS $txfmt
    mcpd-cli set_bus_capabilities $txfmt
    rootfile="mcpd-8_${MCPD_ADDRESS}_${MCPD_ID}-txfmt${txfmt}.root"
    mcpd-cli mpsd_set_pulser $MPSD_PULSER_ON
    mcpd-cli daq start
    mcpd-cli readout --no-listfile --print-packet-summary --print-event-data --root-histo-file "$rootfile" \
        --duration $DAQ_DURATION
    mcpd-cli daq stop
    mcpd-cli mpsd_set_pulser $MPSD_PULSER_OFF
    echo "run for txfmt=${txfmt} done, output file: $rootfile"
done
