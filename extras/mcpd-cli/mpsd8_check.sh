#!/usr/bin/env bash

# Env vars used by mcpd-cli
export MCPD_ADDRESS=mcpd-0002
export MCPD_ID=4

MPSD_BUS=0  # Bus number the MPSD is connected to.

#set -x # verbose bash output

mcpd-cli version
mcpd-cli scan_busses
mcpd-cli get_bus_capabilities
mcpd-cli mpsd_get_parameters $MPSD_BUS
mcpd-cli read_peripheral_register $MPSD_BUS 0 # manual read of MPSD bus tx capabilities
mcpd-cli read_peripheral_register $MPSD_BUS 1 # manual read of MPSD bus tx format
mcpd-cli read_peripheral_register $MPSD_BUS 2 # manual read of MPSD firmware revision
