from __future__ import annotations

import click

import mesytec_mcpd as mcpd

from .util import enum_param, handle_mcpd_errors


@click.group()
def mdll():
    """MDLL module commands."""


@mdll.command("set-thresholds")
@click.argument("threshold_x", type=int)
@click.argument("threshold_y", type=int)
@click.argument("threshold_anode", type=int)
@click.pass_obj
@handle_mcpd_errors
def set_thresholds(ctx, threshold_x, threshold_y, threshold_anode):
    """Set MDLL thresholds."""
    ctx.connection.mdll_set_thresholds(threshold_x, threshold_y, threshold_anode)


@mdll.command("set-spectrum")
@click.argument("shift_x", type=int)
@click.argument("shift_y", type=int)
@click.argument("scale_x", type=int)
@click.argument("scale_y", type=int)
@click.pass_obj
@handle_mcpd_errors
def set_spectrum(ctx, shift_x, shift_y, scale_x, scale_y):
    """Set MDLL spectrum."""
    ctx.connection.mdll_set_spectrum(shift_x, shift_y, scale_x, scale_y)


@mdll.command("set-pulser")
@click.argument("enable", type=bool)
@click.argument("amplitude", type=int)
@click.argument("position", type=enum_param(mcpd.MdllChannelPosition))
@click.pass_obj
@handle_mcpd_errors
def set_pulser(ctx, enable, amplitude, position):
    """Set MDLL pulser (amplitude: 0-3)."""
    ctx.connection.mdll_set_pulser(enable, amplitude, mcpd.MdllChannelPosition[position])


@mdll.command("set-tx-data-set")
@click.argument("dataset", type=enum_param(mcpd.MdllTxDataSet))
@click.pass_obj
@handle_mcpd_errors
def set_tx_data_set(ctx, dataset):
    """Set MDLL TX data set (Default|Timings)."""
    ctx.connection.mdll_set_tx_data_set(mcpd.MdllTxDataSet[dataset])


@mdll.command("set-timing-window")
@click.argument("x_low", type=int)
@click.argument("x_high", type=int)
@click.argument("y_low", type=int)
@click.argument("y_high", type=int)
@click.pass_obj
@handle_mcpd_errors
def set_timing_window(ctx, x_low, x_high, y_low, y_high):
    """Set MDLL timing window."""
    ctx.connection.mdll_set_timing_window(x_low, x_high, y_low, y_high)


@mdll.command("set-energy-window")
@click.argument("lower_threshold", type=int)
@click.argument("upper_threshold", type=int)
@click.pass_obj
@handle_mcpd_errors
def set_energy_window(ctx, lower_threshold, upper_threshold):
    """Set MDLL energy window."""
    ctx.connection.mdll_set_energy_window(lower_threshold, upper_threshold)
