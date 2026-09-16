from __future__ import annotations

import click

import mesytec_mcpd as mcpd

from .util import enum_param, handle_mcpd_errors


@click.group()
def mpsd():
    """MPSD module commands."""


@mpsd.command("set-mode")
@click.argument("mpsd_id", type=int)
@click.argument("mode", type=enum_param(mcpd.MpsdMode))
@click.pass_obj
@handle_mcpd_errors
def set_mode(ctx, mpsd_id, mode):
    """Set mpsd mode (Position|Amplitude)."""
    ctx.connection.mpsd_set_mode(mpsd_id, mcpd.MpsdMode[mode])


@mpsd.command("set-tx-format")
@click.argument("mpsd_id", type=int)
@click.argument("tx_format", type=int)
@click.pass_obj
@handle_mcpd_errors
def set_tx_format(ctx, mpsd_id, tx_format):
    """Set mpsd bus tx format (1|2|4)."""
    ctx.connection.mpsd_set_tx_format(mpsd_id, tx_format)


@mpsd.command("set-gain")
@click.argument("mpsd_id", type=int)
@click.argument("channel", type=int)
@click.argument("gain", type=int)
@click.pass_obj
@handle_mcpd_errors
def set_gain(ctx, mpsd_id, channel, gain):
    """Set per-channel mpsd gain."""
    ctx.connection.mpsd_set_gain(mpsd_id, channel, gain)


@mpsd.command("set-threshold")
@click.argument("mpsd_id", type=int)
@click.argument("threshold", type=int)
@click.pass_obj
@handle_mcpd_errors
def set_threshold(ctx, mpsd_id, threshold):
    """Set mpsd threshold."""
    ctx.connection.mpsd_set_threshold(mpsd_id, threshold)


@mpsd.command("set-pulser")
@click.argument("mpsd_id", type=int)
@click.argument("channel", type=int)
@click.argument("position", type=enum_param(mcpd.ChannelPosition))
@click.argument("amplitude", type=int)
@click.argument("state", type=enum_param(mcpd.PulserState))
@click.pass_obj
@handle_mcpd_errors
def set_pulser(ctx, mpsd_id, channel, position, amplitude, state):
    """Set per-channel mpsd pulser settings."""
    ctx.connection.mpsd_set_pulser(
        mpsd_id, channel, mcpd.ChannelPosition[position], amplitude, mcpd.PulserState[state]
    )


@mpsd.command("get-parameters")
@click.argument("mpsd_id", type=int)
@click.pass_obj
@handle_mcpd_errors
def get_parameters(ctx, mpsd_id):
    """Get mpsd parameters."""
    params = ctx.connection.mpsd_get_params(mpsd_id)
    click.echo(f"MPSD{params.mpsd_id} parameters:")
    click.echo(f"  busTxCapabilities={params.bus_tx_caps}")
    click.echo(f"  txFormat={params.tx_format}")
    click.echo(f"  firmwareRevision=0x{params.firmware_revision:04x}")
