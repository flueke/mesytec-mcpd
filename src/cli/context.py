from __future__ import annotations

from dataclasses import dataclass, field

import mesytec_mcpd as mcpd

# Not exposed by the bindings (mcpd_core.h McpdDefaultAddress/McpdDefaultPort), mirrored here.
McpdDefaultAddress = "192.168.168.121"
McpdDefaultPort = 54321


@dataclass
class CliContext:
    address: str = McpdDefaultAddress
    mcpd_id: int = 0
    port: int = McpdDefaultPort
    _connection: mcpd.McpdConnection | None = field(default=None, repr=False, compare=False)

    @property
    def connection(self) -> mcpd.McpdConnection:
        # Lazy: commands that don't talk to a device (find-id, replay, ...) never open a socket.
        if self._connection is None:
            self._connection = mcpd.McpdConnection(self.address, mcpd_id=self.mcpd_id, port=self.port)
        return self._connection

    def close(self) -> None:
        if self._connection is not None:
            self._connection.close()
            self._connection = None
