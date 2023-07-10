#!/usr/bin/env bash

# Script to test the bus tx format/capabilities settings using an MCPD-8
# pulser.
#
# Requires mcpd-cli to be built with ROOT support.
#
# The script sets the required parameters and starts a short DAQ run recording
# histograms to a ROOT file. All valid bus tx formats are run in order 1, 2, 4.

# Env vars used by mcpd-cli
#export MCPD_ADDRESS=192.168.168.211
export MCPD_ADDRESS=mcpd-0002
export MCPD_ID=1
MPSD_BUS=5  # Bus with connected mpsd module.

# Pulser setup. Together with the gain value of 20 (set in the script below)
# this results in position values of ~470 and amplitudes of ~720.
MPSD_PULSER_ON="${MPSD_BUS} 1 2 100 on"
MPSD_PULSER_OFF="${MPSD_BUS} 1 2 100 off"
DAQ_DURATION=1
READOUT_ARGS=--print-event-data

set -e
trap "exit" INT

# MPSD mode: for formats P and TP the MPSD can send amplitude instead of
# position data. Note that the value is always transmitted in the 'position'
# field of DAQ packets for tx formats P and TP.

for mpsd_mode in 0 1; do
    for txfmt in 1  2  4; do
        mcpd-cli version
        mcpd-cli scan_busses
        mcpd-cli mpsd_set_tx_format $MPSD_BUS $txfmt
        mcpd-cli set_bus_capabilities $txfmt
        mcpd-cli mpsd_set_pulser $MPSD_PULSER_ON

        # Note: after the mpsd_set_mode command the mcpd-8_v2 has to send something
        # to the mpsd for the setting to take effect. The mcpd-8_v1 does this
        # internally.
        mcpd-cli mpsd_set_mode $MPSD_BUS $mpsd_mode

        # Send something to the mpsd to make the setting active when using
        # mcpd-8_v2. In this case the gain value is set but any other command
        # directed to the mpsd should work.
        mcpd-cli mpsd_set_gain $MPSD_BUS 1 20

        mcpd-cli daq start

        rootfile="mcpd-8_${MCPD_ADDRESS}_${MCPD_ID}-txfmt${txfmt}-mpsd_mode${mpsd_mode}.root"
        mcpd-cli readout $READOUT_ARGS --no-listfile --root-histo-file "$rootfile" \
            --duration $DAQ_DURATION
        mcpd-cli daq stop
        mcpd-cli mpsd_set_pulser $MPSD_PULSER_OFF
        echo "run for txfmt=${txfmt} done, output file: $rootfile"
    done
done

# Results with different firmware versions:
#
# Data Packet 'Position' Values
#           Mode            Fmt=1 Fmt=2 Fmt=4
# * pre 12_0_MCPD_V50_FW0603_3.bin
# old_v1    Mode=0 (pos)    470   470   470
# old_v1    Mode=1 (amp)    720   720   470
# new_v2    Mode=0 (pos)    720   720   470 No correct!
# new_v2    Mode=1 (amp)    470   470   470 No correct!
#
# * 12_0_MCPD_V50_FW0603_3.bin
# new_v2    Mode=0 (pos)    470   470   470
# new_v2    Mode=1 (amp)    720   720   470
