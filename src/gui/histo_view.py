"""Histogram display. Histograms are stored at native resolution by mcpd.Daq;
pyqtgraph downsamples to screen resolution on the fly."""

from __future__ import annotations

from typing import Optional

import numpy as np
import pyqtgraph as pg
from pyqtgraph.Qt import QtWidgets
from pyqtgraph.Qt.QtCore import Signal

HISTO_TYPES = {
    "xy": "XY Position",
    "amplitude": "Amplitude",
    "x": "X Position",
    "y": "Y Position",
}


class HistogramView(QtWidgets.QWidget):
    selection_changed = Signal()

    def __init__(self, histo_type: str = "xy", parent=None):
        super().__init__(parent)
        self._data: Optional[np.ndarray] = None
        # Device to select once it shows up in set_devices(), e.g. after restoring the ui state.
        self._wanted_device = None

        self.combo_device = QtWidgets.QComboBox()
        self.combo_device.setSizeAdjustPolicy(QtWidgets.QComboBox.SizeAdjustPolicy.AdjustToContents)
        self.combo_type = QtWidgets.QComboBox()
        for key, title in HISTO_TYPES.items():
            self.combo_type.addItem(title, key)
        self.combo_type.setCurrentIndex(list(HISTO_TYPES).index(histo_type))
        self.cb_log = QtWidgets.QCheckBox("Log")
        self.label_info = QtWidgets.QLabel()
        self.label_cursor = QtWidgets.QLabel()

        top = QtWidgets.QHBoxLayout()
        top.addWidget(self.combo_device)
        top.addWidget(self.combo_type)
        top.addWidget(self.cb_log)
        top.addStretch(1)
        top.addWidget(self.label_info)

        self.plot1d = pg.PlotWidget()
        self.plot1d.showGrid(x=True, y=True, alpha=0.3)
        self.curve = self.plot1d.plot(
            stepMode="center", fillLevel=0, brush=(60, 120, 200, 100), pen=pg.mkPen((60, 120, 200), width=1)
        )
        self.curve.setDownsampling(auto=True, method="peak")
        self.curve.setClipToView(True)

        self.plot2d = pg.PlotWidget()
        pi2d = self.plot2d.getPlotItem()
        pi2d.setLabel("bottom", "X")
        pi2d.setLabel("left", "Y")
        self.image = pg.ImageItem(autoDownsample=True)
        pi2d.addItem(self.image)
        self.colorbar = pg.ColorBarItem(colorMap="viridis", interactive=False)
        self.colorbar.setImageItem(self.image, insert_in=pi2d)

        self.stack = QtWidgets.QStackedWidget()
        self.stack.addWidget(self.plot1d)
        self.stack.addWidget(self.plot2d)

        layout = QtWidgets.QVBoxLayout(self)
        layout.setContentsMargins(2, 2, 2, 2)
        layout.addLayout(top)
        layout.addWidget(self.stack, 1)
        layout.addWidget(self.label_cursor)

        self.combo_device.currentIndexChanged.connect(self._on_device_changed)
        self.combo_type.currentIndexChanged.connect(self._on_selection_changed)
        self.cb_log.toggled.connect(self._on_log_toggled)
        self.plot1d.scene().sigMouseMoved.connect(self._on_mouse_moved_1d)
        self.plot2d.scene().sigMouseMoved.connect(self._on_mouse_moved_2d)
        self._on_selection_changed()

    def histo_type(self) -> str:
        return self.combo_type.currentData()

    def device_key(self):
        return self.combo_device.currentData()

    def find_device(self, key) -> int:
        # QComboBox.findData() does not match python tuples.
        for i in range(self.combo_device.count()):
            if self.combo_device.itemData(i) == key:
                return i
        return -1

    def select_device(self, key):
        self.combo_device.setCurrentIndex(max(self.find_device(key), 0))

    def save_state(self) -> dict:
        key = self._wanted_device or self.device_key()
        return dict(type=self.histo_type(), log=self.cb_log.isChecked(), device=list(key) if key else None)

    def restore_state(self, state: dict):
        if (t := state.get("type")) in HISTO_TYPES:
            self.combo_type.setCurrentIndex(list(HISTO_TYPES).index(t))
        self.cb_log.setChecked(bool(state.get("log", False)))
        if dev := state.get("device"):
            self._wanted_device = tuple(dev)

    def set_devices(self, devices: list[tuple[tuple[int, int], str]]):
        current = prev = self.device_key()
        if self._wanted_device is not None and any(k == self._wanted_device for k, _ in devices):
            current, self._wanted_device = self._wanted_device, None
        known = [self.combo_device.itemData(i) for i in range(self.combo_device.count())]
        labels = [self.combo_device.itemText(i) for i in range(self.combo_device.count())]
        if current == prev and known == [k for k, _ in devices] and labels == [lbl for _, lbl in devices]:
            return
        self.combo_device.blockSignals(True)
        self.combo_device.clear()
        for key, label in devices:
            self.combo_device.addItem(label, key)
        self.combo_device.setCurrentIndex(max(self.find_device(current), 0))
        self.combo_device.blockSignals(False)
        if self.device_key() != prev:
            self._on_selection_changed()

    def _on_device_changed(self):
        self._wanted_device = None
        self._on_selection_changed()

    def _on_selection_changed(self):
        self.stack.setCurrentWidget(self.plot2d if self.histo_type() == "xy" else self.plot1d)
        self._data = None
        self.curve.setData([], [])
        self.image.clear()
        self.label_info.setText("")
        self.selection_changed.emit()

    def _on_log_toggled(self, checked):
        self.plot1d.setLogMode(y=checked)
        self.curve.setFillLevel(None if checked else 0)
        self._redraw()

    def update_histograms(self, histos: Optional[dict]):
        self._data = None if histos is None else histos[self.histo_type()]
        self._redraw()

    def _redraw(self):
        data = self._data
        if data is None:
            return
        self.label_info.setText(f"Entries: {int(data.sum())}")

        if data.ndim == 1:
            y = data.astype(float)
            if self.cb_log.isChecked():
                y[y <= 0] = np.nan
            self.curve.setData(np.arange(len(y) + 1), y)
        else:
            values = data.astype(float)
            values[values <= 0] = np.nan
            if self.cb_log.isChecked():
                values = np.log10(values)
            if np.all(np.isnan(values)):
                self.image.clear()
                return
            levels = (np.nanmin(values), np.nanmax(values))
            if levels[0] == levels[1]:
                levels = (levels[0] - 0.5, levels[1] + 0.5)
            self.image.setImage(values, autoLevels=False)
            self.colorbar.setLevels(levels)
            self.colorbar.axis.setLabel("log10(Counts)" if self.cb_log.isChecked() else "Counts")

    def _on_mouse_moved_1d(self, pos):
        if self._data is None or self._data.ndim != 1:
            return
        p = self.plot1d.getPlotItem().vb.mapSceneToView(pos)
        x = int(np.floor(p.x()))
        if 0 <= x < len(self._data):
            self.label_cursor.setText(f"bin {x}: {int(self._data[x])}")

    def _on_mouse_moved_2d(self, pos):
        if self._data is None or self._data.ndim != 2:
            return
        p = self.plot2d.getPlotItem().vb.mapSceneToView(pos)
        x, y = int(np.floor(p.x())), int(np.floor(p.y()))
        if 0 <= y < self._data.shape[0] and 0 <= x < self._data.shape[1]:
            self.label_cursor.setText(f"x={x}, y={y}: {int(self._data[y, x])}")

