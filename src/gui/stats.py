"""Rates and sanity checks computed from mcpd.Daq.get_source_stats() snapshots."""

from __future__ import annotations

import socket
import struct
from dataclasses import dataclass, field
from typing import Iterable, Optional

import mesytec_mcpd as mcpd

from .config import DeviceConfig

DeviceKey = tuple[int, int]  # (src_addr, device_id), src_addr is 0 when replaying

BUFFER_TYPE_NAMES = {
    mcpd.constants.buffer_types.McpdDataBufferType: "MCPD",
    mcpd.constants.buffer_types.MdllDataBufferType: "MDLL",
}


def resolve_ipv4(address: str) -> Optional[int]:
    try:
        return struct.unpack("!I", socket.inet_aton(socket.gethostbyname(address)))[0]
    except OSError:
        return None


@dataclass
class DeviceRow:
    key: DeviceKey
    stats: mcpd.SourceStats
    config: Optional[DeviceConfig] = None
    packet_rate: float = 0.0
    event_rate: float = 0.0
    byte_rate: float = 0.0
    loss_rate: float = 0.0
    notes: list[str] = field(default_factory=list)

    @property
    def ip(self) -> str:
        return mcpd.format_ipv4(self.key[0]) if self.key[0] else "replay"

    @property
    def type_name(self) -> str:
        return BUFFER_TYPE_NAMES.get(self.stats.buffer_type, f"{self.stats.buffer_type:#06x}")

    @property
    def is_mdll(self) -> bool:
        return self.stats.buffer_type == mcpd.constants.buffer_types.MdllDataBufferType

    @property
    def device_id(self) -> int:
        return self.key[1]

    @property
    def label(self) -> str:
        name = self.config.name if self.config else "?"
        return f"{name} ({self.ip}, id={self.device_id})"


class StatsTracker:
    def __init__(self):
        self._prev: dict[DeviceKey, mcpd.SourceStats] = {}
        self._prev_time: Optional[float] = None
        self._reported: set[tuple[DeviceKey, str]] = set()
        self._resolved: dict[str, Optional[int]] = {}

    def reset(self):
        self._prev.clear()
        self._prev_time = None
        self._reported.clear()

    def _addr(self, cfg: DeviceConfig) -> Optional[int]:
        if cfg.address not in self._resolved:
            self._resolved[cfg.address] = resolve_ipv4(cfg.address)
        return self._resolved[cfg.address]

    def update(
        self, stats: dict[DeviceKey, mcpd.SourceStats], devices: Iterable[DeviceConfig], now: float
    ) -> tuple[list[DeviceRow], list[str]]:
        """Returns the table rows and notes that were not reported before."""
        devices = list(devices)
        dt = now - self._prev_time if self._prev_time is not None else 0.0
        rows = []

        by_id: dict[int, set[int]] = {}
        for addr, dev_id in stats:
            if addr:
                by_id.setdefault(dev_id, set()).add(addr)

        for key in sorted(stats):
            addr, dev_id = key
            st = stats[key]
            row = DeviceRow(key, st)

            if (prev := self._prev.get(key)) is not None and dt > 0 and st.packets >= prev.packets:
                row.packet_rate = (st.packets - prev.packets) / dt
                row.event_rate = (st.neutron_events - prev.neutron_events) / dt
                row.byte_rate = (st.bytes - prev.bytes) / dt
                row.loss_rate = (st.packets_lost - prev.packets_lost) / dt

            if addr == 0:
                matches = [d for d in devices if d.mcpd_id == dev_id]
                if len(matches) == 1:
                    row.config = matches[0]
            else:
                same_addr = [d for d in devices if self._addr(d) == addr]
                exact = [d for d in same_addr if d.mcpd_id == dev_id]
                if exact:
                    row.config = exact[0]
                elif same_addr:
                    ids = ", ".join(str(d.mcpd_id) for d in same_addr)
                    row.notes.append(f"configured with id {ids} but sends id {dev_id}")
                else:
                    row.notes.append("unknown source")

                if len(others := by_id.get(dev_id, set()) - {addr}) > 0:
                    ips = ", ".join(mcpd.format_ipv4(a) for a in sorted(others))
                    row.notes.append(f"id {dev_id} also sent by {ips}: ambiguous in listfile")

            rows.append(row)

        new_notes = []
        for row in rows:
            for note in row.notes:
                if (row.key, note) not in self._reported:
                    self._reported.add((row.key, note))
                    new_notes.append(f"{row.ip} id={row.device_id}: {note}")

        self._prev = dict(stats)
        self._prev_time = now
        return rows, new_notes
