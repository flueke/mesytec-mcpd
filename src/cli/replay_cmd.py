from __future__ import annotations

import queue
import time

import click

import mesytec_mcpd as mcpd

from .util import handle_mcpd_errors


@click.command()
@click.option(
    "--listfile", required=True, type=click.Path(exists=True, dir_okay=False),
    help="Path to the input listfile",
)
@click.option(
    "--report-interval", "report_interval_s", type=float, default=1.0, show_default=True,
    help="Time in seconds between logging replay stats",
)
@handle_mcpd_errors
def replay(listfile, report_interval_s):
    """DAQ replay from a listfile."""
    rp = mcpd.Replay(filename=str(listfile))
    rp.start()

    t_report = time.monotonic()

    click.echo(f"Replaying from {listfile}")

    try:
        while rp.is_running() or not rp.get_queue().empty():
            try:
                rp.get_queue().get(timeout=0.5)
            except queue.Empty:
                continue

            now = time.monotonic()

            if report_interval_s > 0 and now - t_report >= report_interval_s:
                _print_counters(rp.get_counters(), "replay")
                t_report = now
    except KeyboardInterrupt:
        pass
    finally:
        rp.stop()

    _print_counters(rp.get_counters(), "replay (full run)")


def _print_counters(counters: mcpd.Counters, title: str) -> None:
    click.echo(
        f"{title}: packets={counters.packets}, events={counters.events}, bytes={counters.bytes}"
    )
