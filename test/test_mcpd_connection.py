"""Pure-software tests for the McpdConnection python bindings.

These run against FakeMcpdDevice (a minimal UDP protocol responder, see
fake_mcpd_device.py) on loopback - no real hardware required. They exercise
the actual C++ socket/encode/decode code, just not real MCPD/MDLL firmware
behavior.

For a real-hardware counterpart see hw_smoke_test.py at the repo root.
"""

import pytest

import _mesytec_mcpd as mcpd
from fake_mcpd_device import FakeMcpdDevice


@pytest.fixture
def fake_device():
    with FakeMcpdDevice() as device:
        yield device


@pytest.fixture
def connection(fake_device):
    conn = mcpd.McpdConnection("127.0.0.1", mcpd_id=0, port=fake_device.port)
    yield conn
    conn.close()


def test_get_version(connection, fake_device):
    fake_device.cpu_version = (1, 2)
    fake_device.fpga_version = (3, 4)

    vi = connection.get_version()

    assert vi.cpu == (1, 2)
    assert vi.fpga == (3, 4)


def test_write_then_read_register_roundtrip(connection):
    # The only truly symmetric "set X, read X back" pair the wire protocol
    # offers - most mdll_set_*/mpsd_set_* commands are write-only, with no
    # corresponding "get" opcode on the real device.
    connection.write_register(0x10, 0xDEADBEEF)

    assert connection.read_register(0x10) == 0xDEADBEEF


def test_read_register_default_is_zero(connection):
    assert connection.read_register(0x99) == 0


def test_daq_lifecycle(connection, fake_device):
    connection.start_daq()
    assert fake_device.daq_state == "running"

    connection.stop_daq()
    assert fake_device.daq_state == "idle"

    connection.continue_daq()
    assert fake_device.daq_state == "running"

    connection.reset_daq()
    assert fake_device.daq_state == "idle"


def test_mdll_setters_ack_without_error(connection):
    # These have no readback opcode, so all we can assert in software is that
    # the transaction completes without McpdConnection raising.
    connection.mdll_set_thresholds(threshold_x=10, threshold_y=20, threshold_anode=30)
    connection.mdll_set_energy_window(lower_threshold=5, upper_threshold=250)
    connection.mdll_set_timing_window(
        t_sum_limit_x_low=0, t_sum_limit_x_high=1024, t_sum_limit_y_low=0,
        t_sum_limit_y_high=1024,
    )


def test_set_get_bus_capabilities(connection):
    result = connection.set_bus_capabilities(0x02)
    assert result == 0x02

    caps = connection.get_bus_capabilities()
    assert caps.selected == 0x02


def test_id_mismatch_raises_mcpd_error():
    with FakeMcpdDevice(mcpd_id=5, enforce_id=True) as device:
        conn = mcpd.McpdConnection("127.0.0.1", mcpd_id=0, port=device.port)
        try:
            with pytest.raises(mcpd.McpdError):
                conn.get_version()
        finally:
            conn.close()


def test_calling_after_close_raises(connection):
    connection.close()
    assert not connection.is_open()

    with pytest.raises(mcpd.McpdError):
        connection.get_version()
