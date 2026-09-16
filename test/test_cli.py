"""Tests for the mcpd-cli console script, run against FakeMcpdDevice (see
fake_mcpd_device.py) - no real hardware required, same fixtures as
test_mcpd_connection.py.

Invokes the CLI's click group directly (not via subprocess) so it's fast and
covered by coverage tooling.
"""

import pytest

click = pytest.importorskip("click")
from click.testing import CliRunner

from fake_mcpd_device import FakeMcpdDevice
from mesytec_mcpd.cli.main import cli


@pytest.fixture
def fake_device():
    with FakeMcpdDevice() as device:
        yield device


@pytest.fixture
def runner():
    return CliRunner()


def _base_args(fake_device):
    return ["--address", "127.0.0.1", "--port", str(fake_device.port)]


def test_mcpd_version(runner, fake_device):
    fake_device.cpu_version = (1, 2)
    fake_device.fpga_version = (3, 4)

    result = runner.invoke(cli, [*_base_args(fake_device), "mcpd", "version"])

    assert result.exit_code == 0
    assert "cpu=1.2" in result.output
    assert "fpga=3.4" in result.output


def test_write_then_read_register_roundtrip(runner, fake_device):
    result = runner.invoke(
        cli, [*_base_args(fake_device), "mcpd", "write-register", "0x10", "0xDEADBEEF"]
    )
    assert result.exit_code == 0

    result = runner.invoke(cli, [*_base_args(fake_device), "mcpd", "read-register", "0x10"])
    assert result.exit_code == 0
    assert "0xDEADBEEF" in result.output


def test_daq_lifecycle(runner, fake_device):
    result = runner.invoke(cli, [*_base_args(fake_device), "daq", "start"])
    assert result.exit_code == 0
    assert fake_device.daq_state == "running"

    result = runner.invoke(cli, [*_base_args(fake_device), "daq", "stop"])
    assert result.exit_code == 0
    assert fake_device.daq_state == "idle"


def test_id_mismatch_reports_clean_error(runner, fake_device):
    fake_device.enforce_id = True
    fake_device.mcpd_id = 5

    result = runner.invoke(cli, [*_base_args(fake_device), "--id", "0", "mcpd", "version"])

    assert result.exit_code != 0
    assert "Traceback" not in (result.output + str(result.exception))
