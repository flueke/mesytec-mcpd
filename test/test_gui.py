"""Headless tests for the MDLL GUI against FakeMcpdDevice and fake data senders."""

import os
import time

import pytest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
pytest.importorskip("PySide6")
pytest.importorskip("pyqtgraph")

import numpy as np  # noqa: E402
from pyqtgraph.Qt import QtWidgets  # noqa: E402

import mesytec_mcpd as mcpd  # noqa: E402
from fake_mcpd_device import FakeMcpdDevice  # noqa: E402
from fake_data import DataSender, mdll_neutron  # noqa: E402
from mesytec_mcpd.gui.commands import COMMANDS, SETTINGS  # noqa: E402
from mesytec_mcpd.gui.config import DeviceConfig, Setup  # noqa: E402
from mesytec_mcpd.gui.main_window import MainWindow  # noqa: E402


@pytest.fixture(scope="module")
def app():
    return QtWidgets.QApplication.instance() or QtWidgets.QApplication([])


def wait_until(app, cond, timeout=3.0):
    deadline = time.monotonic() + timeout
    while not cond():
        if time.monotonic() > deadline:
            raise TimeoutError
        app.processEvents()
        time.sleep(0.01)


@pytest.fixture
def devices():
    with FakeMcpdDevice(host="127.0.0.2") as a, FakeMcpdDevice(host="127.0.0.3") as b:
        yield a, b


@pytest.fixture
def window(app, devices, tmp_path):
    a, b = devices
    setup = Setup(
        data_port=0,
        listfile_dir=str(tmp_path),
        devices=[
            DeviceConfig("a", "127.0.0.2", 0, a.port),
            DeviceConfig("b", "127.0.0.3", 0, b.port),
        ],
    )
    setup.devices[1].settings["timing"]["role"] = "Slave"
    w = MainWindow(setup, tmp_path / "setup.json")
    w.show()
    yield w
    w.close()


def test_command_specs_match_bindings():
    for cmd in SETTINGS + COMMANDS:
        assert hasattr(mcpd.McpdConnection, cmd.method), cmd.method


def test_setup_roundtrip(tmp_path):
    setup = Setup(devices=[DeviceConfig("x", "10.0.0.1", 3)])
    setup.devices[0].settings["thresholds"]["threshold_x"] = 42
    setup.save(tmp_path / "s.json")
    loaded = Setup.load(tmp_path / "s.json")
    assert loaded.devices[0].mcpd_id == 3
    assert loaded.devices[0].settings["thresholds"]["threshold_x"] == 42
    assert loaded.devices[0].settings["pulser"] == setup.devices[0].settings["pulser"]


def test_run_cycle(app, window, devices, tmp_path):
    a, b = devices
    window.start_run()
    assert window.state == "readout"
    port = window.daq.local_port
    wait_until(app, lambda: a.daq_state == "running")
    assert b.daq_state == "idle"  # started by the master via the sync bus

    with (
        DataSender(port, "127.0.0.2") as sa,
        DataSender(port, "127.0.0.3") as sb,
        DataSender(port, "127.0.0.4") as sc,
    ):
        sa.send([mdll_neutron(5, 10, 20)] * 3, device_id=0)
        sb.send([mdll_neutron(6, 11, 21)], device_id=0)
        sc.send([mdll_neutron(7, 12, 22)], device_id=9)
        wait_until(app, lambda: window.daq.get_counters().packets == 3)

    window._update_stats()
    rows = {row.key[0]: row for row in window.rows}
    assert rows[sa.src_addr].config.name == "a"
    assert rows[sb.src_addr].config.name == "b"
    assert rows[sc.src_addr].config is None
    assert "unknown source" in rows[sc.src_addr].notes
    assert any("ambiguous" in n for n in rows[sa.src_addr].notes)

    view = window.histo_views[0][1]
    view.select_device((sa.src_addr, 0))
    assert view.device_key() == (sa.src_addr, 0)
    window._update_histograms()
    assert view._data.shape == (1024, 1024)
    assert view._data[20, 10] == 3

    window.stop_run()
    wait_until(app, lambda: window.state == "idle")
    assert a.daq_state == "idle"
    listfiles = list(tmp_path.glob("*.mcpdlst"))
    assert len(listfiles) == 1 and listfiles[0].stat().st_size == 3 * 1472


def test_apply_settings_and_set_id(app, window, devices):
    panel = window.device_panel
    panel.apply_all()
    cfg = window.setup.devices[0]
    cmd = next(c for c in COMMANDS if c.key == "set_id")
    panel._run_command(cfg, cmd, {"new_id": 5})
    wait_until(app, lambda: cfg.mcpd_id == 5)
    assert panel.table.item(0, 3).text() == "5"


def test_histogram_view_log_and_types(app):
    from mesytec_mcpd.gui.histo_view import HistogramView

    view = HistogramView("amplitude")
    view.set_devices([((1, 0), "dev")])
    histos = {
        "amplitude": np.arange(256, dtype=np.uint64),
        "x": np.zeros(1024, dtype=np.uint64),
        "y": np.zeros(1024, dtype=np.uint64),
        "xy": np.zeros((1024, 1024), dtype=np.uint64),
    }
    view.update_histograms(histos)
    view.cb_log.setChecked(True)
    view.combo_type.setCurrentIndex(view.combo_type.findData("xy"))
    view.update_histograms(histos)  # all zero 2d
    histos["xy"][3, 4] = 7
    view.update_histograms(histos)
    assert view.label_info.text() == "Entries: 7"


def test_histogram_view_keeps_selection(app):
    from mesytec_mcpd.gui.histo_view import HistogramView

    view = HistogramView("xy")
    view.set_devices([((1, 0), "a"), ((2, 0), "b")])
    view.select_device((2, 0))
    view.set_devices([((0, 5), "new"), ((1, 0), "a"), ((2, 0), "b")])
    assert view.device_key() == (2, 0)


def test_stats_wrong_id_note():
    from mesytec_mcpd.gui.stats import StatsTracker

    cfg = DeviceConfig("a", "127.0.0.2", mcpd_id=1)
    key = (0x7F000002, 0)
    rows, notes = StatsTracker().update({key: mcpd.SourceStats()}, [cfg], 0.0)
    assert rows[0].config is None
    assert rows[0].notes == ["configured with id 1 but sends id 0"]
    assert len(notes) == 1


def test_start_requires_single_master(app, window, devices):
    a, b = devices
    window.setup.devices[1].settings["timing"]["role"] = "Master"
    window.start_run()
    assert window.state == "idle"
    for cfg in window.setup.devices:
        cfg.settings["timing"]["role"] = "Slave"
    window.start_run()
    assert window.state == "idle"
    assert a.daq_state == "idle" and b.daq_state == "idle"


def test_added_device_defaults_to_slave(app, window):
    panel = window.device_panel
    panel._add_device()
    assert window.setup.devices[-1].timing_role == "Slave"
    assert panel.table.item(len(window.setup.devices) - 1, 5).text() == "Slave"
