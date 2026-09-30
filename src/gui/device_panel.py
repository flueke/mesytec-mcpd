from __future__ import annotations

from typing import Optional

from pyqtgraph.parametertree import Parameter, ParameterTree
from pyqtgraph.Qt import QtCore, QtWidgets
from pyqtgraph.Qt.QtCore import Signal

from .commands import COMMANDS, SETTINGS, Arg, Command
from .config import DeviceConfig, Setup
from .device_worker import DeviceWorkers

ActionName = "__action"


def _arg_param(arg: Arg, value) -> dict:
    p = dict(name=arg.name, title=arg.title or arg.name, value=value)
    if arg.kind == "int":
        p.update(type="int", limits=arg.limits)
    elif arg.kind == "bool":
        p.update(type="bool")
    elif arg.kind == "enum":
        p.update(type="list", limits=[m.name for m in arg.enum])
    else:
        p.update(type="str")
    return p


def _command_group(cmd: Command, values: dict, action_title: str) -> dict:
    children = [_arg_param(a, values[a.name]) for a in cmd.args]
    children.append(dict(name=ActionName, title=action_title, type="action"))
    return dict(name=cmd.key, title=cmd.title, type="group", children=children)


def submit_command(
    workers: DeviceWorkers, cfg: DeviceConfig, cmd: Command, values: dict, on_success=None
):
    values = dict(values)
    workers.submit(cfg, cmd.describe(values), lambda conn: cmd.call(conn, values), on_success)


def apply_settings(workers: DeviceWorkers, cfg: DeviceConfig):
    for cmd in SETTINGS:
        submit_command(workers, cfg, cmd, cfg.settings[cmd.key])


class DevicePanel(QtWidgets.QWidget):
    devices_changed = Signal()

    Columns = ("On", "Name", "Address", "Id", "Port", "Timing")

    def __init__(self, setup: Setup, workers: DeviceWorkers, parent=None):
        super().__init__(parent)
        self.setup = setup
        self.workers = workers
        self._command_values: dict[int, dict[str, dict]] = {}
        self._tree_root: Optional[Parameter] = None
        self._tree_cfg: Optional[DeviceConfig] = None

        self.table = QtWidgets.QTableWidget(0, len(self.Columns))
        self.table.setHorizontalHeaderLabels(self.Columns)
        self.table.setSelectionBehavior(QtWidgets.QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setSelectionMode(QtWidgets.QAbstractItemView.SelectionMode.SingleSelection)
        self.table.setEditTriggers(QtWidgets.QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.verticalHeader().hide()
        self.table.horizontalHeader().setStretchLastSection(True)

        self.pb_add = QtWidgets.QPushButton("Add")
        self.pb_remove = QtWidgets.QPushButton("Remove")
        self.pb_apply = QtWidgets.QPushButton("Apply Settings")
        self.pb_apply.setToolTip("Send all settings to the selected device")
        self.pb_apply_all = QtWidgets.QPushButton("Apply To All")
        self.pb_apply_all.setToolTip("Send all settings to all enabled devices")
        buttons = QtWidgets.QHBoxLayout()
        for b in (self.pb_add, self.pb_remove, self.pb_apply, self.pb_apply_all):
            buttons.addWidget(b)

        self.tree = ParameterTree(showHeader=False)
        self.le_filter = QtWidgets.QLineEdit()
        self.le_filter.setPlaceholderText("Filter...")
        self.le_filter.setClearButtonEnabled(True)

        splitter = QtWidgets.QSplitter(QtCore.Qt.Orientation.Vertical)
        top = QtWidgets.QWidget()
        top_layout = QtWidgets.QVBoxLayout(top)
        top_layout.setContentsMargins(0, 0, 0, 0)
        top_layout.addWidget(self.table)
        top_layout.addLayout(buttons)
        splitter.addWidget(top)
        bottom = QtWidgets.QWidget()
        bottom_layout = QtWidgets.QVBoxLayout(bottom)
        bottom_layout.setContentsMargins(0, 0, 0, 0)
        bottom_layout.addWidget(self.le_filter)
        bottom_layout.addWidget(self.tree)
        splitter.addWidget(bottom)
        splitter.setStretchFactor(1, 3)

        layout = QtWidgets.QVBoxLayout(self)
        layout.setContentsMargins(2, 2, 2, 2)
        layout.addWidget(splitter)

        self.pb_add.clicked.connect(self._add_device)
        self.pb_remove.clicked.connect(self._remove_device)
        self.pb_apply.clicked.connect(self._apply_selected)
        self.pb_apply_all.clicked.connect(self.apply_all)
        self.table.itemSelectionChanged.connect(self._on_selection_changed)
        self.le_filter.textChanged.connect(self._apply_filter)

        self.refresh()

    def set_setup(self, setup: Setup):
        for cfg in self.setup.devices:
            self.workers.remove(cfg)
        self.setup = setup
        self._command_values.clear()
        self.refresh()
        self.devices_changed.emit()

    def selected_device(self) -> Optional[DeviceConfig]:
        rows = self.table.selectionModel().selectedRows()
        if rows and rows[0].row() < len(self.setup.devices):
            return self.setup.devices[rows[0].row()]
        return None

    def refresh(self):
        selected = self.selected_device()
        self.table.blockSignals(True)
        self.table.setRowCount(len(self.setup.devices))
        for row, cfg in enumerate(self.setup.devices):
            values = (
                "✓" if cfg.enabled else "",
                cfg.name,
                cfg.address,
                str(cfg.mcpd_id),
                str(cfg.port),
                cfg.timing_role,
            )
            for col, text in enumerate(values):
                item = self.table.item(row, col)
                if item is None:
                    item = QtWidgets.QTableWidgetItem()
                    self.table.setItem(row, col, item)
                item.setText(text)
        self.table.resizeColumnsToContents()
        self.table.blockSignals(False)

        if selected in self.setup.devices:
            self.table.selectRow(self.setup.devices.index(selected))
        elif self.setup.devices:
            self.table.selectRow(0)
        self._on_selection_changed()

    def _add_device(self):
        n = len(self.setup.devices)
        cfg = DeviceConfig(name=f"mdll{n}", address=f"192.168.168.{121 + n}")
        if any(d.timing_role == "Master" for d in self.setup.devices):
            cfg.settings["timing"]["role"] = "Slave"
        self.setup.devices.append(cfg)
        self.refresh()
        self.table.selectRow(n)
        self.devices_changed.emit()

    def _remove_device(self):
        if (cfg := self.selected_device()) is None:
            return
        self.workers.remove(cfg)
        self.setup.devices.remove(cfg)
        self._command_values.pop(id(cfg), None)
        self.refresh()
        self.devices_changed.emit()

    def _apply_selected(self):
        if (cfg := self.selected_device()) is not None:
            apply_settings(self.workers, cfg)

    def apply_all(self):
        for cfg in self.setup.devices:
            if cfg.enabled:
                apply_settings(self.workers, cfg)

    def _on_selection_changed(self):
        cfg = self.selected_device()
        if cfg is self._tree_cfg and self._tree_root is not None:
            return
        self._tree_cfg = cfg
        if cfg is None:
            self._tree_root = None
            self.tree.clear()
            return
        self._tree_root = self._build_tree(cfg)
        self.tree.setParameters(self._tree_root, showTop=False)
        self._apply_filter()

    # Shows parameters whose name or title contains the filter text, plus their
    # ancestors and descendants. Groups containing matches are expanded.
    def _apply_filter(self):
        if self._tree_root is None:
            return
        text = self.le_filter.text().strip().lower()

        def matches(p: Parameter) -> bool:
            return p.name() != ActionName and (text in p.name().lower() or text in p.title().lower())

        def update(p: Parameter, parent_matched: bool) -> bool:
            matched = parent_matched or matches(p)
            child_visible = [update(c, matched) for c in p.children()]
            visible = matched or any(child_visible)
            p.show(visible)
            if visible and (action := p.names.get(ActionName)) is not None:
                action.show(True)
            if text and p.hasChildren() and any(child_visible):
                p.setOpts(expanded=True)
            return visible

        for c in self._tree_root.children():
            update(c, not text)

    def _build_tree(self, cfg: DeviceConfig) -> Parameter:
        cmd_values = self._command_values.setdefault(id(cfg), {c.key: c.defaults() for c in COMMANDS})

        connection = dict(
            name="connection",
            title="Connection",
            type="group",
            children=[
                dict(name="name", type="str", value=cfg.name),
                dict(name="address", type="str", value=cfg.address),
                dict(name="mcpd_id", title="id", type="int", value=cfg.mcpd_id, limits=(0, 255)),
                dict(name="port", type="int", value=cfg.port, limits=(1, 65535)),
                dict(name="enabled", type="bool", value=cfg.enabled),
            ],
        )
        settings = dict(
            name="settings",
            title="Settings",
            type="group",
            children=[_command_group(c, cfg.settings[c.key], "Apply") for c in SETTINGS],
        )
        commands = dict(
            name="commands",
            title="Commands",
            type="group",
            expanded=False,
            children=[_command_group(c, cmd_values[c.key], "Run") for c in COMMANDS],
        )
        root = Parameter.create(name="root", type="group", children=[connection, settings, commands])

        for section, cmds, store in (
            ("settings", SETTINGS, lambda c: cfg.settings[c.key]),
            ("commands", COMMANDS, lambda c: cmd_values[c.key]),
        ):
            for cmd in cmds:
                values = store(cmd)
                action = root.child(section, cmd.key, ActionName)
                action.sigActivated.connect(
                    lambda _p, cmd=cmd, values=values: self._run_command(cfg, cmd, values)
                )

        def on_changed(_root, changes):
            for param, change, data in changes:
                if change != "value":
                    continue
                path = root.childPath(param)
                if path[0] == "connection":
                    setattr(cfg, path[1], data)
                    self.refresh()
                    self.devices_changed.emit()
                elif path[0] == "settings":
                    cfg.settings[path[1]][path[2]] = data
                    if path[1] == "timing":
                        self.refresh()
                elif path[0] == "commands":
                    cmd_values[path[1]][path[2]] = data

        root.sigTreeStateChanged.connect(on_changed)
        return root

    def _run_command(self, cfg: DeviceConfig, cmd: Command, values: dict):
        on_success = None
        if cmd.on_success is not None:
            snapshot = dict(values)

            def on_success(_result):
                cmd.on_success(cfg, snapshot)
                self.refresh()
                self.rebuild_tree()
                self.devices_changed.emit()

        submit_command(self.workers, cfg, cmd, values, on_success)

    def rebuild_tree(self):
        self._tree_root = None
        self._tree_cfg = None
        self._on_selection_changed()
