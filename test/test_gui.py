"""Headless tests for the MCPD/MDLL GUI against FakeMcpdDevice and fake data senders."""

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
from fake_data import MCPD_DATA_BUFFER_TYPE, DataSender, mdll_neutron, mpsd_neutron  # noqa: E402
from mesytec_mcpd.gui.commands import (  # noqa: E402
    COMMANDS,
    MPSD_COMMANDS,
    MPSD_SETTINGS,
    SETTINGS,
    ScanBusses,
)
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
            DeviceConfig("a", "127.0.0.2", 0, a.port, device_type="mdll"),
            DeviceConfig("b", "127.0.0.3", 0, b.port, device_type="mcpd"),
        ],
    )
    setup.devices[1].settings["timing"]["role"] = "Slave"
    w = MainWindow(setup, tmp_path / "setup.json")
    w.show()
    yield w
    w.close()


def test_command_specs_match_bindings():
    for cmd in SETTINGS + COMMANDS + MPSD_SETTINGS + MPSD_COMMANDS:
        assert hasattr(mcpd.McpdConnection, cmd.method), cmd.method


def test_setup_roundtrip(tmp_path):
    setup = Setup(
        devices=[DeviceConfig("x", "10.0.0.1", 3, device_type="mdll"), DeviceConfig("y", "10.0.0.2")]
    )
    setup.devices[0].settings["thresholds"]["threshold_x"] = 42
    setup.devices[1].mpsds[2].present = True
    setup.devices[1].mpsds[2].settings["gain"]["gain5"] = 17
    setup.save(tmp_path / "s.json")
    loaded = Setup.load(tmp_path / "s.json")
    x, y = loaded.devices
    assert x.mcpd_id == 3 and x.device_type == "mdll" and x.mpsds == []
    assert x.settings["thresholds"]["threshold_x"] == 42
    assert x.settings["pulser"] == setup.devices[0].settings["pulser"]
    assert y.device_type == "mcpd" and "thresholds" not in y.settings
    assert [m.present for m in y.mpsds] == [False, False, True] + [False] * 5
    assert y.mpsds[2].settings["gain"]["gain5"] == 17


def test_device_type_change_keeps_common_settings():
    cfg = DeviceConfig("x", "10.0.0.1", device_type="mdll")
    cfg.settings["timing"]["role"] = "Slave"
    cfg.device_type = "mcpd"
    cfg.normalize()
    assert cfg.timing_role == "Slave"
    assert "thresholds" not in cfg.settings and len(cfg.mpsds) == 8


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
    assert panel.table.item(0, 4).text() == "5"


def test_scan_busses_and_apply_mpsd_settings(app, window, devices):
    _, b = devices
    b.bus_ids = [0, 0x11, 0, 0, 0x22, 0, 0, 0]
    panel = window.device_panel
    cfg = window.setup.devices[1]
    panel.table.selectRow(1)
    panel._run_command(cfg, ScanBusses, {})
    wait_until(app, lambda: cfg.mpsds[4].present)
    assert [m.present for m in cfg.mpsds] == [False, True, False, False, True, False, False, False]
    assert panel.table.item(1, 7).text() == "1 4"
    assert panel._tree_root.child("busses", "bus4", "present").value()

    cfg.mpsds[4].settings["gain"].update({f"gain{ch}": ch for ch in range(8)})
    b.requests.clear()
    panel._apply_selected()
    wait_until(app, lambda: [d[0] for c, d in b.requests if c == 16] == [1, 4])  # mpsd_set_mode
    gains = [data[:3] for cmd, data in b.requests if cmd == 13]
    assert gains[0] == [1, 8, 0]  # bus 1: all gains equal, channel 8 = all
    assert gains[1:] == [[4, ch, ch] for ch in range(8)]
    assert [data[0] for cmd, data in b.requests if cmd == 14] == [1, 4]


def test_histogram_view_log_and_types(app):
    from mesytec_mcpd.gui.histo_view import HistogramView

    view = HistogramView()
    view.restore_state(dict(type="amplitude"))
    view.set_devices([((1, 0), "dev", "mdll")])
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

    view = HistogramView()
    view.set_devices([((1, 0), "a", "mdll"), ((2, 0), "b", "mcpd")])
    view.select_device((2, 0))
    assert view.device_kind() == "mcpd"
    view.set_devices([((0, 5), "new", "mdll"), ((1, 0), "a", "mdll"), ((2, 0), "b", "mcpd")])
    assert view.device_key() == (2, 0)
    assert view.device_kind() == "mcpd"


def test_histogram_view_mcpd_modes(app):
    from mesytec_mcpd.gui.histo_view import MCPD_MODES, HistogramView

    view = HistogramView()
    view.set_devices([((1, 0), "dev", "mcpd")])
    position = np.zeros((8, 32, 1024), dtype=np.uint64)
    position[3, 2, 100] = 5
    position[3, 6, 200] = 7
    position[0, 0, 0] = 1
    histos = {"position": position, "amplitude": np.zeros_like(position)}
    view.restore_state(dict(value="position", bus=3, channels=[2]))
    expected = {"bus": "Entries: 12", "overlay": "Entries: 5", "overview": "Entries: 13"}
    for mode in MCPD_MODES:
        view.combo_mode.setCurrentIndex(view.combo_mode.findData(mode))
        view.update_histograms(histos)
        assert view.label_info.text() == expected[mode], mode
    view.cb_log.setChecked(True)
    view.update_histograms(histos)
    assert view.save_state()["channels"] == [2]


def test_mcpd_histograms_in_window(app, window):
    window.start_readout(False)
    port = window.daq.local_port
    with DataSender(port, "127.0.0.3") as sb:
        sb.send([mpsd_neutron(4, 1, 10, 300)] * 2, device_id=0, buffer_type=MCPD_DATA_BUFFER_TYPE)
        wait_until(app, lambda: window.daq.get_counters().packets == 1)
    window._update_stats()
    view = window.histo_views[0][1]
    view.select_device((sb.src_addr, 0))
    assert view.device_kind() == "mcpd"
    view.restore_state(dict(mode="bus", bus=4, value="position"))
    window._update_histograms()
    assert view._data.shape == (8, 8, 1024)
    assert view._data[4, 1, 300] == 2
    window.stop_readout()


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
    assert panel.table.item(len(window.setup.devices) - 1, 6).text() == "Slave"
