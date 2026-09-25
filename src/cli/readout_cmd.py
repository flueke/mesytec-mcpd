from __future__ import annotations

import pathlib
import time

import click

import mesytec_mcpd as mcpd

from .util import handle_mcpd_errors


@click.command()
@click.option(
    "--listfile", type=click.Path(dir_okay=False, path_type=pathlib.Path),
    help="Path to the output listfile",
)
@click.option(
    "--overwrite-listfile", is_flag=True, help="Overwrite the output listfile if it already exists"
)
@click.option("--no-listfile", is_flag=True, help="Do not write an output listfile")
@click.option(
    "--duration", "duration_s", type=float, default=0.0, show_default=True,
    help="DAQ run duration in seconds. Runs forever if 0.",
)
@click.option(
    "--dataport", type=int, default=54321, show_default=True,
    help="mcpd data port (also the local listening port)",
)
@click.option(
    "--report-interval", "report_interval_s", type=float, default=1.0, show_default=True,
    help="Time in seconds between logging readout stats",
)
@click.option(
    "--no-start-daq", is_flag=True, help="Do not send the DAQ start command prior to data taking"
)
@click.pass_obj
@handle_mcpd_errors
def readout(
    ctx, listfile, overwrite_listfile, no_listfile, duration_s, dataport, report_interval_s,
    no_start_daq,
):
    """DAQ readout to a listfile."""
    if listfile is None and not no_listfile:
        raise click.UsageError("no listfile name given (use --no-listfile to ignore)")

    listfile_path = ""

    if listfile is not None and not no_listfile:
        if listfile.exists() and not overwrite_listfile:
            raise click.UsageError(f"output listfile '{listfile}' already exists")
        listfile_path = str(listfile)

    daq = mcpd.Daq(listen_port=dataport)

    try:
        daq.start_readout(listfile_path, overwrite=overwrite_listfile)
    except (RuntimeError, OSError) as e:
        raise click.ClickException(str(e)) from e

    try:
        if not no_start_daq:
            ctx.connection.start_daq()

        t_start = time.monotonic()
        t_report = t_start

        click.echo("readout: entering readout loop, press ctrl-c to quit")

        while daq.is_running():
            time.sleep(0.1)
            now = time.monotonic()

            if duration_s > 0 and now - t_start >= duration_s:
                click.echo("readout: run duration reached, leaving readout loop")
                break

            if report_interval_s > 0 and now - t_report >= report_interval_s:
                _print_counters(daq, "readout")
                t_report = now
    except KeyboardInterrupt:
        pass
    finally:
        daq.stop()

    if daq.has_exception():
        try:
            daq.rethrow_exception()
        except Exception as e:
            raise click.ClickException(f"readout error: {e}") from e

    _print_counters(daq, "readout (full run)")


def _print_counters(daq: mcpd.Daq, title: str) -> None:
    c = daq.get_counters()
    stats = daq.get_source_stats().values()
    click.echo(
        f"{title}: packets={c.packets}, packetsLost={sum(s.packets_lost for s in stats)}, "
        f"events={sum(s.events for s in stats)}, bytes={c.bytes}, timeouts={c.timeouts}, "
        f"invalid={c.invalid_packets}, sources={len(stats)}"
    )
