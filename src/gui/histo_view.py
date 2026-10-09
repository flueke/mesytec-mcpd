"""Histogram display. Histograms are stored at native resolution by mcpd.Daq;
pyqtgraph downsamples 2D histograms to screen resolution on the fly.

MDLL sources show the xy/amplitude/x/y histograms. MCPD sources show the
per (bus, channel) position or amplitude histograms either as one plot per
channel of a bus, as an overlay of selected channels of a bus or as a 2D
overview of all channels."""

from __future__ import annotations

from typing import Optional

import numpy as np
import pyqtgraph as pg
from pyqtgraph.Qt import QtCore, QtWidgets
from pyqtgraph.Qt.QtCore import Signal

from .commands import McpdBusCount, MpsdChannelCount

HISTO_TYPES = {
    "xy": "XY Position",
    "amplitude": "Amplitude",
    "x": "X Position",
    "y": "Y Position",
}

MCPD_VALUES = {
    "position": "Position",
    "amplitude": "Amplitude",
}

MCPD_MODES = {
    "bus": "Bus Channels",
    "overlay": "Channel Overlay",
    "overview": "All Channels 2D",
}

KindRole = QtCore.Qt.ItemDataRole.UserRole + 1
FillBrush = (60, 120, 200, 100)
FillPen = (60, 120, 200)
BusSeparatorPen = (255, 255, 255, 120)


def _combo(items: dict, current: str) -> QtWidgets.QComboBox:
    combo = QtWidgets.QComboBox()
    for key, title in items.items():
        combo.addItem(title, key)
    combo.setCurrentIndex(max(list(items).index(current), 0) if current in items else 0)
    return combo


def _set_combo(combo: QtWidgets.QComboBox, items: dict, key):
    if key in items:
        combo.setCurrentIndex(list(items).index(key))


def _channel_color(ch: int):
    return pg.intColor(ch, MpsdChannelCount, maxValue=220)


# Overview y axis ticks: bus numbers at the center of each bus block, channel
# numbers at the center of each row.
def _overview_ticks() -> list[list[tuple[float, str]]]:
    major = [
        (bus * MpsdChannelCount + MpsdChannelCount / 2, f"bus {bus}") for bus in range(McpdBusCount)
    ]
    minor = [
        (bus * MpsdChannelCount + ch + 0.5, str(ch))
        for bus in range(McpdBusCount)
        for ch in range(MpsdChannelCount)
    ]
    return [major, minor]


class HistogramView(QtWidgets.QWidget):
    selection_changed = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._data: Optional[np.ndarray] = None
        # Device to select once it shows up in set_devices(), e.g. after restoring the ui state.
        self._wanted_device = None
        self._kind = "mcpd"

        self.combo_device = QtWidgets.QComboBox()
        self.combo_device.setSizeAdjustPolicy(QtWidgets.QComboBox.SizeAdjustPolicy.AdjustToContents)
        self.combo_type = _combo(HISTO_TYPES, "xy")
        self.combo_value = _combo(MCPD_VALUES, "position")
        self.combo_mode = _combo(MCPD_MODES, "bus")
        self.combo_bus = QtWidgets.QComboBox()
        for bus in range(McpdBusCount):
            self.combo_bus.addItem(f"Bus {bus}", bus)
        self.channel_box = QtWidgets.QWidget()
        channel_layout = QtWidgets.QHBoxLayout(self.channel_box)
        channel_layout.setContentsMargins(0, 0, 0, 0)
        self.cb_channels = []
        for ch in range(MpsdChannelCount):
            cb = QtWidgets.QCheckBox(str(ch))
            cb.setChecked(True)
            cb.setStyleSheet(f"color: {_channel_color(ch).name()}")
            channel_layout.addWidget(cb)
            self.cb_channels.append(cb)
        self.cb_log = QtWidgets.QCheckBox("Log")
        self.label_info = QtWidgets.QLabel()
        self.label_cursor = QtWidgets.QLabel()

        top = QtWidgets.QHBoxLayout()
        for w in (self.combo_device, self.combo_type, self.combo_value, self.combo_mode, self.combo_bus,
                  self.channel_box, self.cb_log):
            top.addWidget(w)
        top.addStretch(1)
        top.addWidget(self.label_info)

        # 1D plot: single MDLL curve or overlay of MCPD channels.
        self.plot1d = pg.PlotWidget()
        self.plot1d.showGrid(x=True, y=True, alpha=0.3)
        self.legend = self.plot1d.addLegend()
        self.curve = self.plot1d.plot(stepMode="center", fillLevel=0, brush=FillBrush, pen=pg.mkPen(FillPen, width=1))
        self.overlay_curves = [
            self.plot1d.plot(stepMode="center", pen=pg.mkPen(_channel_color(ch), width=1))
            for ch in range(MpsdChannelCount)
        ]
        # No clipToView/downsampling: PlotDataItem applies them to x and y alike,
        # breaking the len(x) == len(y) + 1 requirement of stepMode="center".

        # 2D plot: MDLL xy or MCPD overview of all (bus, channel) pairs.
        self.plot2d = pg.PlotWidget()
        pi2d = self.plot2d.getPlotItem()
        self.image = pg.ImageItem(autoDownsample=True)
        pi2d.addItem(self.image)
        self.colorbar = pg.ColorBarItem(colorMap="viridis", interactive=False)
        self.colorbar.setImageItem(self.image, insert_in=pi2d)
        self.bus_separators = [
            pg.InfiniteLine(pos=bus * MpsdChannelCount, angle=0, pen=pg.mkPen(BusSeparatorPen, width=1))
            for bus in range(1, McpdBusCount)
        ]
        for line in self.bus_separators:
            pi2d.addItem(line)

        # One plot per channel of a bus, stacked vertically with linked x axes.
        self.stacked = pg.GraphicsLayoutWidget()
        self.stacked_plots = []
        self.stacked_curves = []
        for ch in range(MpsdChannelCount):
            p = self.stacked.addPlot(row=ch, col=0)
            p.showGrid(x=True, y=True, alpha=0.3)
            p.setLabel("left", f"ch{ch}")
            if ch:
                p.setXLink(self.stacked_plots[0])
            if ch < MpsdChannelCount - 1:
                p.getAxis("bottom").setStyle(showValues=False)
            self.stacked_plots.append(p)
            self.stacked_curves.append(
                p.plot(stepMode="center", fillLevel=0, brush=FillBrush, pen=pg.mkPen(FillPen, width=1))
            )

        self.stack = QtWidgets.QStackedWidget()
        self.stack.addWidget(self.plot1d)
        self.stack.addWidget(self.plot2d)
        self.stack.addWidget(self.stacked)

        layout = QtWidgets.QVBoxLayout(self)
        layout.setContentsMargins(2, 2, 2, 2)
        layout.addLayout(top)
        layout.addWidget(self.stack, 1)
        layout.addWidget(self.label_cursor)

        self.combo_device.currentIndexChanged.connect(self._on_device_changed)
        for combo in (self.combo_type, self.combo_value, self.combo_mode, self.combo_bus):
            combo.currentIndexChanged.connect(self._on_selection_changed)
        for cb in self.cb_channels:
            cb.toggled.connect(self._on_channels_changed)
        self.cb_log.toggled.connect(self._on_log_toggled)
        self.plot1d.scene().sigMouseMoved.connect(self._on_mouse_moved_1d)
        self.plot2d.scene().sigMouseMoved.connect(self._on_mouse_moved_2d)
        self.stacked.scene().sigMouseMoved.connect(self._on_mouse_moved_stacked)
        self._on_selection_changed()

    def histo_type(self) -> str:
        return self.combo_type.currentData()

    def mcpd_value(self) -> str:
        return self.combo_value.currentData()

    def mcpd_mode(self) -> str:
        return self.combo_mode.currentData()

    def bus(self) -> int:
        return self.combo_bus.currentData()

    def channels(self) -> list[int]:
        return [ch for ch, cb in enumerate(self.cb_channels) if cb.isChecked()]

    def device_key(self):
        return self.combo_device.currentData()

    def device_kind(self) -> str:
        return self._kind

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
        return dict(
            type=self.histo_type(),
            value=self.mcpd_value(),
            mode=self.mcpd_mode(),
            bus=self.bus(),
            channels=self.channels(),
            log=self.cb_log.isChecked(),
            device=list(key) if key else None,
        )

    def restore_state(self, state: dict):
        _set_combo(self.combo_type, HISTO_TYPES, state.get("type"))
        _set_combo(self.combo_value, MCPD_VALUES, state.get("value"))
        _set_combo(self.combo_mode, MCPD_MODES, state.get("mode"))
        if (bus := state.get("bus")) in range(McpdBusCount):
            self.combo_bus.setCurrentIndex(bus)
        if (channels := state.get("channels")) is not None:
            for ch, cb in enumerate(self.cb_channels):
                cb.setChecked(ch in channels)
        self.cb_log.setChecked(bool(state.get("log", False)))
        if dev := state.get("device"):
            self._wanted_device = tuple(dev)

    # devices: (key, label, kind) with kind being "mdll" or "mcpd".
    def set_devices(self, devices: list[tuple[tuple[int, int], str, str]]):
        current = prev = self.device_key()
        if self._wanted_device is not None and any(k == self._wanted_device for k, _, _ in devices):
            current, self._wanted_device = self._wanted_device, None
        known = [
            (self.combo_device.itemData(i), self.combo_device.itemText(i), self.combo_device.itemData(i, KindRole))
            for i in range(self.combo_device.count())
        ]
        if current == prev and known == list(devices):
            return
        self.combo_device.blockSignals(True)
        self.combo_device.clear()
        for key, label, kind in devices:
            self.combo_device.addItem(label, key)
            self.combo_device.setItemData(self.combo_device.count() - 1, kind, KindRole)
        self.combo_device.setCurrentIndex(max(self.find_device(current), 0))
        self.combo_device.blockSignals(False)
        kind = self.combo_device.currentData(KindRole) or self._kind
        if self.device_key() != prev or kind != self._kind:
            self._kind = kind
            self._on_selection_changed()

    def _on_device_changed(self):
        self._wanted_device = None
        self._kind = self.combo_device.currentData(KindRole) or self._kind
        self._on_selection_changed()

    def _update_controls(self):
        mdll = self._kind == "mdll"
        mode = self.mcpd_mode()
        self.combo_type.setVisible(mdll)
        self.combo_value.setVisible(not mdll)
        self.combo_mode.setVisible(not mdll)
        self.combo_bus.setVisible(not mdll and mode != "overview")
        self.channel_box.setVisible(not mdll and mode == "overlay")

        pi2d = self.plot2d.getPlotItem()
        if mdll:
            pi2d.setLabel("bottom", "X")
            pi2d.setLabel("left", "Y")
            pi2d.getAxis("left").setTicks(None)
            for line in self.bus_separators:
                line.setVisible(False)
            page = self.plot2d if self.histo_type() == "xy" else self.plot1d
        else:
            pi2d.setLabel("bottom", "Bin")
            pi2d.setLabel("left", "Bus / Channel")
            pi2d.getAxis("left").setTicks(_overview_ticks())
            for line in self.bus_separators:
                line.setVisible(True)
            page = {"bus": self.stacked, "overlay": self.plot1d, "overview": self.plot2d}[mode]
        self.stack.setCurrentWidget(page)

        self.legend.clear()
        if not mdll and mode == "overlay":
            for ch in self.channels():
                self.legend.addItem(self.overlay_curves[ch], f"ch{ch}")

    def _clear_plots(self):
        self.curve.setData([], [])
        for c in self.overlay_curves + self.stacked_curves:
            c.setData([], [])
        self.image.clear()

    def _on_selection_changed(self):
        self._update_controls()
        self._data = None
        self._clear_plots()
        self.label_info.setText("")
        self.label_cursor.setText("")
        self.selection_changed.emit()

    def _on_channels_changed(self):
        self._update_controls()
        self._redraw()

    def _on_log_toggled(self, checked):
        self.plot1d.setLogMode(y=checked)
        self.curve.setFillLevel(None if checked else 0)
        for p, c in zip(self.stacked_plots, self.stacked_curves, strict=True):
            p.setLogMode(y=checked)
            c.setFillLevel(None if checked else 0)
        self._redraw()

    def update_histograms(self, histos: Optional[dict]):
        if histos is None:
            self._data = None
        elif self._kind == "mdll":
            self._data = histos.get(self.histo_type())
        else:
            self._data = histos.get(self.mcpd_value())
            if self._data is not None:
                self._data = self._data[:, :MpsdChannelCount, :]
        self._redraw()

    def _curve_values(self, data: np.ndarray) -> np.ndarray:
        y = data.astype(float)
        if self.cb_log.isChecked():
            y[y <= 0] = np.nan
        return y

    def _set_curve(self, curve, data: np.ndarray):
        curve.setData(np.arange(len(data) + 1), self._curve_values(data))

    def _set_image(self, data: np.ndarray):
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

    def _redraw(self):
        data = self._data
        if data is None:
            return

        if self._kind == "mdll":
            self.label_info.setText(f"Entries: {int(data.sum())}")
            if data.ndim == 1:
                self._set_curve(self.curve, data)
            else:
                self._set_image(data)
            return

        mode = self.mcpd_mode()
        if mode == "overview":
            self.label_info.setText(f"Entries: {int(data.sum())}")
            self._set_image(data.reshape(-1, data.shape[-1]))
        elif mode == "bus":
            bus_data = data[self.bus()]
            self.label_info.setText(f"Entries: {int(bus_data.sum())}")
            for curve, ch_data in zip(self.stacked_curves, bus_data, strict=True):
                self._set_curve(curve, ch_data)
        else:
            bus_data = data[self.bus()]
            channels = self.channels()
            self.label_info.setText(f"Entries: {int(bus_data[channels].sum())}")
            for ch, curve in enumerate(self.overlay_curves):
                if ch in channels:
                    self._set_curve(curve, bus_data[ch])
                else:
                    curve.setData([], [])

    def _on_mouse_moved_1d(self, pos):
        if self._data is None:
            return
        p = self.plot1d.getPlotItem().vb.mapSceneToView(pos)
        x = int(np.floor(p.x()))
        if self._kind == "mdll":
            if self._data.ndim == 1 and 0 <= x < len(self._data):
                self.label_cursor.setText(f"bin {x}: {int(self._data[x])}")
        elif 0 <= x < self._data.shape[-1]:
            bus_data = self._data[self.bus()]
            counts = ", ".join(f"ch{ch}={int(bus_data[ch, x])}" for ch in self.channels())
            self.label_cursor.setText(f"bin {x}: {counts}")

    def _on_mouse_moved_2d(self, pos):
        if self._data is None:
            return
        p = self.plot2d.getPlotItem().vb.mapSceneToView(pos)
        x, y = int(np.floor(p.x())), int(np.floor(p.y()))
        if self._kind == "mdll":
            if self._data.ndim == 2 and 0 <= y < self._data.shape[0] and 0 <= x < self._data.shape[1]:
                self.label_cursor.setText(f"x={x}, y={y}: {int(self._data[y, x])}")
            return
        buses, channels, bins = self._data.shape
        if 0 <= y < buses * channels and 0 <= x < bins:
            bus, ch = divmod(y, channels)
            self.label_cursor.setText(f"bus {bus}, ch{ch}, bin {x}: {int(self._data[bus, ch, x])}")

    def _on_mouse_moved_stacked(self, pos):
        if self._data is None or self._kind != "mcpd":
            return
        for ch, plot in enumerate(self.stacked_plots):
            if plot.vb.sceneBoundingRect().contains(pos):
                x = int(np.floor(plot.vb.mapSceneToView(pos).x()))
                if 0 <= x < self._data.shape[-1]:
                    count = int(self._data[self.bus(), ch, x])
                    self.label_cursor.setText(f"bus {self.bus()}, ch{ch}, bin {x}: {count}")
                return
