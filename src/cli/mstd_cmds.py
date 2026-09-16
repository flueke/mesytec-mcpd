from __future__ import annotations

import click

from .util import handle_mcpd_errors


@click.group()
def mstd():
    """MSTD module commands."""


@mstd.command("set-gain")
@click.argument("mstd_id", type=int)
@click.argument("channel", type=int, metavar="CHANNEL")
@click.argument("gain", type=int)
@click.pass_obj
@handle_mcpd_errors
def set_gain(ctx, mstd_id, channel, gain):
    """Set per-channel mstd gain (channel 0..15, 16=all channels; gain 0..255)."""
    ctx.connection.mstd_set_gain(mstd_id, channel, gain)
