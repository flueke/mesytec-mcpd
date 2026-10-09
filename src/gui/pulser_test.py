"""MPSD pulser test: cycles the pulser of every present MPSD on the enabled MCPDs
through all amplitudes, positions and channels so that all channel histograms
receive counts. Each MPSD has a single pulser, a new setting replaces the
previous one. All MPSDs are stepped in parallel. The DAQ state is not touched.
"""

from __future__ import annotations

import itertools
import logging
from typing import Callable, Optional

import mesytec_mcpd as mcpd
from pyqtgraph.parametertree import Parameter, ParameterTree
from pyqtgraph.Qt import QtCore, QtWidgets
from pyqtgraph.Qt.QtCore import Slot

from .commands import MpsdChannelCount
from .config import DeviceConfig
from .device_worker import DeviceWorkers

log = logging.getLogger(__name__)

# Command labels with this prefix are logged at debug level by the main window.
LabelPrefix = "pulser_test:"
OffLabel = LabelPrefix + "off"

# Shorter dwell times make MCPD-8_v1 stop working until it is rebooted.
MinDwell_ms = 250

Positions = (mcpd.ChannelPosition.Left, mcpd.ChannelPosition.Right, mcpd.ChannelPosition.Center)


class PulserTest(QtCore.QObject):
    def __init__(
        self, workers: DeviceWorkers, devices: Callable[[], list[DeviceConfig]], parent=None
    ):
        super().__init__(parent)
        self.workers = workers
        self.devices = devices
        self.amplitudes: tuple[int, ...] = (50, 90)
        self.dwell_ms = MinDwell_ms
        self.on_status: Optional[Callable[[str], None]] = None
        self._steps: list[tuple[int, mcpd.ChannelPosition, int]] = []
        self._step_label = ""
        self._generation = 0
        self._step = 0
        self._cycle = 0
        self._pending = 0
        self._running = False
        self._touched: dict[tuple[int, int], tuple[DeviceConfig, int]] = {}
        self._timer = QtCore.QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self._next_step)
        workers.finished.connect(self._on_finished)

    def is_running(self) -> bool:
        return self._running

    def _targets(self) -> list[tuple[DeviceConfig, int]]:
        return [
            (cfg, bus)
            for cfg in self.devices()
            if cfg.enabled and cfg.device_type == "mcpd"
            for bus, mpsd in enumerate(cfg.mpsds)
            if mpsd.present
        ]

    def start(self) -> bool:
        if self._running:
            return True
        if not self._targets():
            log.error("Pulser test: no present MPSDs on enabled MCPDs. Run 'Scan Busses' first.")
            return False
        # (channel, position, amplitude index), amplitude changes innermost
        self._steps = list(
            itertools.product(range(MpsdChannelCount), Positions, range(len(self.amplitudes)))
        )
        self._generation += 1
        self._step_label = f"{LabelPrefix}step#{self._generation}"
        self._step = 0
        self._cycle = 0
        self._pending = 0
        self._touched.clear()
        self._running = True
        log.info(f"Pulser test started, amplitudes={self.amplitudes}, dwell={self.dwell_ms} ms")
        self._next_step()
        return True

    def stop(self):
        if not self._running:
            return
        self._running = False
        self._timer.stop()
        for cfg, bus in self._touched.values():
            self._submit_off(cfg, bus, OffLabel)
        self._touched.clear()
        log.info("Pulser test stopped")
        self._status("stopped")

    def all_off(self):
        for cfg, bus in self._targets():
            self._submit_off(cfg, bus, "pulser_off")

    def _submit_off(self, cfg: DeviceConfig, bus: int, label: str):
        self.workers.submit(
            cfg,
            f"{label}(bus={bus})",
            lambda conn, bus=bus: conn.mpsd_set_pulser(
                bus, 0, mcpd.ChannelPosition.Center, 0, mcpd.PulserState.Off
            ),
        )

    @Slot()
    def _next_step(self):
        if not self._running:
            return
        targets = self._targets()
        if not targets:
            log.error("Pulser test: no present MPSDs left")
            self.stop()
            return
        channel, position, amp_index = self._steps[self._step]
        amplitude = self.amplitudes[amp_index]
        self._pending = len(targets)
        for cfg, bus in targets:
            self._touched[(id(cfg), bus)] = (cfg, bus)
            self.workers.submit(
                cfg,
                self._step_label,
                lambda conn, bus=bus: conn.mpsd_set_pulser(
                    bus, channel, position, amplitude, mcpd.PulserState.On
                ),
            )
        self._status(
            f"cycle {self._cycle}, step {self._step + 1}/{len(self._steps)}: "
            f"channel={channel}, position={position.name}, amplitude={amplitude}, mpsds={len(targets)}"
        )
        self._step += 1
        if self._step == len(self._steps):
            self._step = 0
            self._cycle += 1

    @Slot(object, str, object, object, object)
    def _on_finished(self, _cfg, label: str, _result, _error, _on_success):
        if label != self._step_label or self._pending == 0:
            return
        self._pending -= 1
        if self._pending == 0 and self._running:
            self._timer.start(max(self.dwell_ms, MinDwell_ms))

    def _status(self, text: str):
        if self.on_status is not None:
            self.on_status(text)


class PulserTestPanel(QtWidgets.QWidget):
    def __init__(self, test: PulserTest, parent=None):
        super().__init__(parent)
        self.test = test
        self.params = Parameter.create(
            name="pulser_test",
            type="group",
            children=[
                dict(
                    name="amplitude0",
                    title="amplitude 1",
                    type="int",
                    value=test.amplitudes[0],
                    limits=(0, 255),
                ),
                dict(
                    name="amplitude1",
                    title="amplitude 2",
                    type="int",
                    value=test.amplitudes[1],
                    limits=(0, 255),
                ),
                dict(
                    name="dwell_ms",
                    title="dwell [ms]",
                    type="int",
                    value=test.dwell_ms,
                    limits=(MinDwell_ms, 600000),
                ),
            ],
        )
        self.params.sigTreeStateChanged.connect(self._apply_params)
        tree = ParameterTree(showHeader=False)
        tree.setParameters(self.params, showTop=False)

        self.pb_start = QtWidgets.QPushButton("Start")
        self.pb_stop = QtWidgets.QPushButton("Stop")
        self.pb_all_off = QtWidgets.QPushButton("All Pulsers Off")
        self.pb_all_off.setToolTip("Turn off the pulser of all present MPSDs on enabled MCPDs")
        self.label_status = QtWidgets.QLabel()
        self.label_status.setWordWrap(True)

        buttons = QtWidgets.QHBoxLayout()
        for b in (self.pb_start, self.pb_stop, self.pb_all_off):
            buttons.addWidget(b)

        layout = QtWidgets.QVBoxLayout(self)
        layout.setContentsMargins(2, 2, 2, 2)
        layout.addWidget(tree)
        layout.addLayout(buttons)
        layout.addWidget(self.label_status)

        self.pb_start.clicked.connect(self._start)
        self.pb_stop.clicked.connect(self.stop)
        self.pb_all_off.clicked.connect(test.all_off)
        test.on_status = self._on_status
        self._update_buttons()

    def _apply_params(self, *_):
        self.test.amplitudes = (self.params["amplitude0"], self.params["amplitude1"])
        self.test.dwell_ms = self.params["dwell_ms"]

    def _on_status(self, text: str):
        self.label_status.setText(text)
        self._update_buttons()

    def _start(self):
        self._apply_params()
        self.test.start()
        self._update_buttons()

    def stop(self):
        self.test.stop()
        self._update_buttons()

    def _update_buttons(self):
        running = self.test.is_running()
        self.pb_start.setEnabled(not running)
        self.pb_stop.setEnabled(running)
        self.pb_all_off.setEnabled(not running)
