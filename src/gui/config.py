from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from pathlib import Path

from .commands import MPSD_SETTINGS, McpdBusCount, Command, settings_for

DefaultPort = 54321


# Returns the defaults of the given commands updated with the known keys of 'values'.
def merge_settings(commands: tuple[Command, ...], values: dict) -> dict[str, dict]:
    result = {c.key: c.defaults() for c in commands}
    for key, v in values.items():
        if key in result:
            result[key].update({k: x for k, x in v.items() if k in result[key]})
    return result


@dataclass(eq=False)
class MpsdConfig:
    present: bool = False
    settings: dict[str, dict] = field(default_factory=lambda: merge_settings(MPSD_SETTINGS, {}))

    @classmethod
    def from_dict(cls, d: dict) -> MpsdConfig:
        return cls(present=bool(d.get("present", False)), settings=merge_settings(MPSD_SETTINGS, d.get("settings", {})))


@dataclass(eq=False)
class DeviceConfig:
    name: str
    address: str
    mcpd_id: int = 0
    port: int = DefaultPort
    enabled: bool = True
    device_type: str = "mcpd"  # mcpd | mdll
    settings: dict[str, dict] = field(default_factory=dict)
    # One entry per MCPD bus. Empty for MDLL devices.
    mpsds: list[MpsdConfig] = field(default_factory=list)

    def __post_init__(self):
        self.normalize()

    # Adapts settings and mpsds to the device type, keeping existing values.
    def normalize(self):
        self.settings = merge_settings(settings_for(self.device_type), self.settings)
        if self.device_type == "mcpd":
            self.mpsds = (self.mpsds + [MpsdConfig() for _ in range(McpdBusCount)])[:McpdBusCount]
        else:
            self.mpsds = []

    @property
    def timing_role(self) -> str:
        return self.settings["timing"]["role"]

    def target(self) -> tuple[str, int, int]:
        return (self.address, self.mcpd_id, self.port)

    @classmethod
    def from_dict(cls, d: dict) -> DeviceConfig:
        return cls(
            name=d["name"],
            address=d["address"],
            mcpd_id=int(d.get("mcpd_id", 0)),
            port=int(d.get("port", DefaultPort)),
            enabled=bool(d.get("enabled", True)),
            device_type=d.get("device_type", "mcpd"),
            settings=d.get("settings", {}),
            mpsds=[MpsdConfig.from_dict(m) for m in d.get("mpsds", [])],
        )


@dataclass
class Setup:
    data_port: int = DefaultPort
    write_listfile: bool = True
    listfile_dir: str = ""
    devices: list[DeviceConfig] = field(default_factory=list)

    def to_json(self) -> str:
        return json.dumps(asdict(self), indent=2)

    @classmethod
    def from_json(cls, text: str) -> Setup:
        d = json.loads(text)
        return cls(
            data_port=int(d.get("data_port", DefaultPort)),
            write_listfile=bool(d.get("write_listfile", True)),
            listfile_dir=d.get("listfile_dir", ""),
            devices=[DeviceConfig.from_dict(dd) for dd in d.get("devices", [])],
        )

    def save(self, path: Path):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(self.to_json())

    @classmethod
    def load(cls, path: Path) -> Setup:
        return cls.from_json(path.read_text())
