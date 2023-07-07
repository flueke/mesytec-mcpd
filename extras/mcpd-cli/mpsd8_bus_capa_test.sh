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
export MCPD_ADDRESS=mcpd-0002
export MCPD_ID=1
MPSD_BUS=5
MPSD_PULSER_ON="${MPSD_BUS} 1 2 100 on"
MPSD_PULSER_OFF="${MPSD_BUS} 1 2 100 off"
# MPSD mode: for formats P and TP the MPSD can send amplitude instead of
# position data. Note that the value is always transmitted in the 'position'
# field of DAQ packets for tx formats P and TP.
MPSD_MODE="0" # pos|0|amp|1
DAQ_DURATION=5
READOUT_ARGS=--print-event-data

set -e

trap "exit" INT

for txfmt in 1  2  4; do
    mcpd-cli version
    mcpd-cli scan_busses
    mcpd-cli mpsd_set_tx_format $MPSD_BUS $txfmt
    mcpd-cli set_bus_capabilities $txfmt
    mcpd-cli mpsd_set_pulser $MPSD_PULSER_ON

    # Note: after the mpsd_set_mode command the mcpd-8_v2 has to send something
    # to the mpsd for the setting to take effect. The mcpd-8_v1 does this
    # internally.
    mcpd-cli mpsd_set_mode $MPSD_BUS $MPSD_MODE

    # Send something to the mpsd to make the setting stick when using
    # mcpd-8_v2. In this case the gain value is set.
    mcpd-cli mpsd_set_gain $MPSD_BUS 1 20

    mcpd-cli daq start

    rootfile="mcpd-8_${MCPD_ADDRESS}_${MCPD_ID}-txfmt${txfmt}.root"
    mcpd-cli readout $READOUT_ARGS --no-listfile --root-histo-file "$rootfile" \
        --duration $DAQ_DURATION
    mcpd-cli daq stop
    mcpd-cli mpsd_set_pulser $MPSD_PULSER_OFF
    echo "run for txfmt=${txfmt} done, output file: $rootfile"
done
