from __future__ import annotations

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
    daq = mcpd.Daq()

    try:
        daq.start_replay(str(listfile))
    except RuntimeError as e:
        raise click.ClickException(str(e)) from e

    t_report = time.monotonic()

    click.echo(f"Replaying from {listfile}")

    try:
        while daq.is_running():
            time.sleep(0.1)
            now = time.monotonic()

            if report_interval_s > 0 and now - t_report >= report_interval_s:
                _print_counters(daq, "replay")
                t_report = now
    except KeyboardInterrupt:
        pass
    finally:
        daq.stop()

    _print_counters(daq, "replay (full run)")


def _print_counters(daq: mcpd.Daq, title: str) -> None:
    c = daq.get_counters()
    stats = daq.get_source_stats().values()
    click.echo(
        f"{title}: packets={c.packets}, packetsLost={sum(s.packets_lost for s in stats)}, "
        f"events={sum(s.events for s in stats)}, bytes={c.bytes}, sources={len(stats)}"
    )
