from __future__ import annotations

import click

from .util import handle_mcpd_errors


@click.group()
def daq():
    """DAQ control commands."""


@daq.command("start")
@click.pass_obj
@handle_mcpd_errors
def start(ctx):
    """Start the DAQ."""
    ctx.connection.start_daq()


@daq.command("stop")
@click.pass_obj
@handle_mcpd_errors
def stop(ctx):
    """Stop the DAQ."""
    ctx.connection.stop_daq()


@daq.command("continue")
@click.pass_obj
@handle_mcpd_errors
def daq_continue(ctx):
    """Continue the DAQ."""
    ctx.connection.continue_daq()


@daq.command("reset")
@click.pass_obj
@handle_mcpd_errors
def reset(ctx):
    """Reset the DAQ."""
    ctx.connection.reset_daq()
