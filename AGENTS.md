# General

To store and retrieve memories use memory/NNN-<topic>.md style files.

Keep comments to a minimum in all languages. We assume readers know the language
and how to program. Only add comments for tricky parts/unexpected stuff. Keep
existing comments though unless our changes invalidate them.

# Recreate mcpd-cli in python

## Motivation

Have a real tool to test and use the python bindings.

## Response from copilot

Recreating `mcpd-cli` in Python is a good idea for exactly the reason you state — it exercises `McpdConnection` end-to-end (encode → UDP → fake/real device → decode) the same way `test_mcpd_connection.py` does, but through the same "verb" surface a user actually calls, so it catches argument-plumbing bugs the unit tests wouldn't (wrong `py::arg` name, wrong enum passed, wrong default, etc.). It can also run against `fake_mcpd_device.py` in CI without hardware, same as today's tests, and doubles as a real usable tool for people who don't want to build the C++ binary.

**What I'd change vs. a literal port of `mcpd-cli.cc`:**

1. **Subcommand groups instead of one flat list.** `mcpd-cli.cc` has ~35 top-level commands. In Python I'd nest them by target device: `mcpd-cli mcpd set-timing ...`, `mcpd-cli mpsd set-gain ...`, `mcpd-cli mstd set-gain ...`, `mcpd-cli mdll set-thresholds ...`, `mcpd-cli daq start|stop|continue|reset`. `argparse` subparsers (or `click`/`typer` groups) make this natural and it mirrors the binding's method namespacing (`mpsd_*`, `mdll_*`, etc.) already visible in `mesytec_mcpd_py.cc`.

2. **`argparse` from stdlib, not a new dependency.** `pyproject.toml` has no CLI-framework dependency today (`gui` extra pulls `rich` already). I'd avoid adding `click`/`typer` unless you want the nicer DX — stdlib `argparse` is enough for the verb/flag shapes lyra produces, and keeps the base install dependency-free. Register it as `[project.scripts] mcpd-cli = "mesytec_mcpd.cli.main:main"`.

3. **No manual "hack around lyra's --help subgroup issue"** or hand-rolled `parse_unsigned_value` — `argparse` handles `type=lambda s: int(s, 0)` for hex/octal/decimal directly, and `choices=` maps straight onto the `native_enum` values already exposed (`TimingRole`, `BusTermination`, `MpsdMode`, …), so commands like `timing` don't need the `"master"/"1"` string-juggling the C++ version does.

4. **Exceptions instead of `std::error_code` checks.** Every command in `mcpd-cli.cc` is `if (ec) { spdlog::error(...); return 1; }`. Python bindings already raise `McpdError` (`py::register_exception`), so each CLI verb becomes a 2-3 line function wrapped by one shared `try/except McpdError as e: print error, exit(1)` decorator instead of repeating the error-check boilerplate ~30 times.

5. **Reuse your existing `Readout`/`Replay` worker classes** for the `readout`/`replay` verbs instead of hand-rolling the receive loop — `cli-test2.py` already shows the pattern (`get_queue()`, `get_counters()`); the CLI's `readout` command would just be a thin loop over that queue plus the listfile-writing you already do at the C++ level (or that `Readout`/`Replay` already handle, need to check).

6. **Skip the Python-embedding (`--python-script`) support entirely** — pointless from Python, so the CLI is actually simpler than the C++ readout command, not more complex.

7. **Environment variable + logging setup mirrors `mesytec_mcpd_py.cc`** (`MCPD_ADDRESS`/`MCPD_ID` env fallback, `set_log_level`) rather than re-deriving it.

8. **Testing**: add `test/test_cli.py` using the existing `fake_device`/`FakeMcpdDevice` fixtures, invoking the CLI's `main(argv)` function directly (not subprocess) so it's fast and gets coverage, plus maybe one `subprocess` smoke test to check the installed console-script entry point works end-to-end.

Want me to go ahead and scaffold this — a `src/mesytec-mcpd/cli/` package with the subcommand groups, wired to a `mcpd-cli` console script, plus a starter `test_cli.py`?
