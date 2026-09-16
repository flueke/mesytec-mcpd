from __future__ import annotations

import click

import mesytec_mcpd as mcpd

from .context import CliContext, McpdDefaultAddress, McpdDefaultPort
from .daq_cmds import daq
from .mcpd_cmds import mcpd_group
from .mdll_cmds import mdll
from .mpsd_cmds import mpsd
from .mstd_cmds import mstd
from .readout_cmd import readout
from .replay_cmd import replay


@click.group()
@click.option(
    "--address", envvar="MCPD_ADDRESS", default=McpdDefaultAddress, show_default=True,
    help="mcpd ip-address/hostname",
)
@click.option("--id", "mcpd_id", envvar="MCPD_ID", default=0, show_default=True, help="mcpd id")
@click.option("--port", default=McpdDefaultPort, show_default=True, help="mcpd command port")
@click.option("--debug", is_flag=True, help="set log level to debug")
@click.option("--trace", is_flag=True, help="set log level to trace")
@click.version_option(mcpd.__version__, prog_name="mcpd-cli")
@click.pass_context
def cli(ctx, address, mcpd_id, port, debug, trace):
    """Command-line tool for driving mesytec MCPD/MPSD/MSTD/MDLL devices.

    The mcpd address and id can also be set via the MCPD_ADDRESS/MCPD_ID
    environment variables.
    """
    if trace:
        mcpd.set_log_level("trace")
    elif debug:
        mcpd.set_log_level("debug")

    ctx.obj = CliContext(address=address, mcpd_id=mcpd_id, port=port)
    ctx.call_on_close(ctx.obj.close)


cli.add_command(mcpd_group, name="mcpd")
cli.add_command(mpsd, name="mpsd")
cli.add_command(mstd, name="mstd")
cli.add_command(mdll, name="mdll")
cli.add_command(daq, name="daq")
cli.add_command(readout, name="readout")
cli.add_command(replay, name="replay")


def main():
    cli()


if __name__ == "__main__":
    main()
