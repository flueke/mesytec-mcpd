"""Per-device command execution.

Each device gets its own thread so blocking transactions (up to ~2.5s on
timeouts) neither freeze the GUI nor delay other devices. Jobs for one device
run in submission order.
"""

from __future__ import annotations

import logging
from typing import Any, Callable, Optional

import mesytec_mcpd as mcpd
from pyqtgraph.Qt import QtCore
from pyqtgraph.Qt.QtCore import Signal, Slot

from .config import DeviceConfig

log = logging.getLogger(__name__)

Job = Callable[[mcpd.McpdConnection], Any]


class DeviceWorker(QtCore.QObject):
    # device config, label, result, exception, on_success callback
    finished = Signal(object, str, object, object, object)
    _job = Signal(object)

    def __init__(self, cfg: DeviceConfig):
        super().__init__()
        self.cfg = cfg
        self._conn: Optional[mcpd.McpdConnection] = None
        self._conn_target = None
        self._job.connect(self._run)

    def submit(self, label: str, job: Job, on_success=None):
        self._job.emit((label, job, self.cfg.target(), on_success))

    def _connection(self, target) -> mcpd.McpdConnection:
        if self._conn is None or self._conn_target != target:
            self.close()
            address, mcpd_id, port = target
            self._conn = mcpd.McpdConnection(address, mcpd_id=mcpd_id, port=port)
            self._conn_target = target
        return self._conn

    @Slot(object)
    def _run(self, item):
        label, job, target, on_success = item
        try:
            result = job(self._connection(target))
            self.finished.emit(self.cfg, label, result, None, on_success)
        except Exception as e:
            self.finished.emit(self.cfg, label, None, e, None)

    @Slot()
    def close(self):
        if self._conn is not None:
            self._conn.close()
            self._conn = None


class DeviceWorkers(QtCore.QObject):
    """Owns one DeviceWorker thread per device. on_success callbacks are passed
    back through 'finished' so they run in the receiver's (GUI) thread."""

    finished = Signal(object, str, object, object, object)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._workers: dict[int, tuple[QtCore.QThread, DeviceWorker]] = {}

    def submit(self, cfg: DeviceConfig, label: str, job: Job, on_success=None):
        if id(cfg) not in self._workers:
            thread = QtCore.QThread()
            thread.setObjectName(f"DeviceWorker-{cfg.name}")
            worker = DeviceWorker(cfg)
            worker.moveToThread(thread)
            worker.finished.connect(self.finished)
            thread.finished.connect(worker.close, QtCore.Qt.ConnectionType.DirectConnection)
            thread.start()
            self._workers[id(cfg)] = (thread, worker)
        self._workers[id(cfg)][1].submit(label, job, on_success)

    def remove(self, cfg: DeviceConfig):
        if (entry := self._workers.pop(id(cfg), None)) is not None:
            thread, _ = entry
            thread.quit()
            thread.wait()

    def shutdown(self):
        for thread, _ in self._workers.values():
            thread.quit()
        for thread, _ in self._workers.values():
            thread.wait()
        self._workers.clear()
