from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from pathlib import Path

from .commands import SETTINGS

DefaultPort = 54321


def default_settings() -> dict[str, dict]:
    return {c.key: c.defaults() for c in SETTINGS}


@dataclass(eq=False)
class DeviceConfig:
    name: str
    address: str
    mcpd_id: int = 0
    port: int = DefaultPort
    enabled: bool = True
    settings: dict[str, dict] = field(default_factory=default_settings)

    @property
    def timing_role(self) -> str:
        return self.settings["timing"]["role"]

    def target(self) -> tuple[str, int, int]:
        return (self.address, self.mcpd_id, self.port)

    @classmethod
    def from_dict(cls, d: dict) -> DeviceConfig:
        settings = default_settings()
        for key, values in d.get("settings", {}).items():
            if key in settings:
                settings[key].update({k: v for k, v in values.items() if k in settings[key]})
        return cls(
            name=d["name"],
            address=d["address"],
            mcpd_id=int(d.get("mcpd_id", 0)),
            port=int(d.get("port", DefaultPort)),
            enabled=bool(d.get("enabled", True)),
            settings=settings,
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
