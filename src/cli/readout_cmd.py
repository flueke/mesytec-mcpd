from __future__ import annotations

import pathlib
import queue
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

    listfile_handle = None

    if listfile is not None and not no_listfile:
        if listfile.exists() and not overwrite_listfile:
            raise click.UsageError(f"output listfile '{listfile}' already exists")
        listfile_handle = open(listfile, "wb")

    if not no_start_daq:
        ctx.connection.start_daq()

    rdo = mcpd.Readout(listenPort=dataport)
    rdo.start()

    t_start = time.monotonic()
    t_report = t_start

    click.echo("readout: entering readout loop, press ctrl-c to quit")

    try:
        while True:
            try:
                packet = rdo.get_queue().get(timeout=0.5).packet
            except queue.Empty:
                pass
            else:
                if listfile_handle is not None:
                    listfile_handle.write(packet.get_raw_words().tobytes())

            now = time.monotonic()

            if duration_s > 0 and now - t_start >= duration_s:
                click.echo("readout: run duration reached, leaving readout loop")
                break

            if report_interval_s > 0 and now - t_report >= report_interval_s:
                _print_counters(rdo.get_counters(), "readout")
                t_report = now
    except KeyboardInterrupt:
        pass
    finally:
        rdo.stop()
        if listfile_handle is not None:
            listfile_handle.close()

    _print_counters(rdo.get_counters(), "readout (full run)")


def _print_counters(counters: mcpd.Counters, title: str) -> None:
    click.echo(
        f"{title}: packets={counters.packets}, packetsLost={counters.packets_lost}, "
        f"events={counters.events}, bytes={counters.bytes}, timeouts={counters.timeouts}"
    )
