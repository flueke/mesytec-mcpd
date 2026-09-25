"""Declarative description of the McpdConnection commands the GUI exposes.

Arg.name must match the py::arg names in mesytec_mcpd_py.cc. Values are kept
JSON friendly (enums as member names, hex values as strings) and converted
right before calling into the bindings.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Callable, Optional

import mesytec_mcpd as mcpd


@dataclass(frozen=True)
class Arg:
    name: str
    kind: str  # int | bool | enum | str | hex
    default: Any
    limits: Optional[tuple[int, int]] = None
    enum: Optional[type] = None
    title: Optional[str] = None

    def convert(self, value):
        if self.kind == "enum":
            return self.enum[value]
        if self.kind == "hex":
            return int(value, 0) if isinstance(value, str) else int(value)
        if self.kind == "int":
            return int(value)
        if self.kind == "bool":
            return bool(value)
        return value


@dataclass(frozen=True)
class Command:
    key: str
    title: str
    method: str
    args: tuple[Arg, ...] = ()
    # Called with (device_config, values) after the command succeeded.
    on_success: Optional[Callable[[Any, dict], None]] = field(default=None, compare=False)

    def defaults(self) -> dict:
        return {a.name: a.default for a in self.args}

    def call(self, conn: mcpd.McpdConnection, values: dict):
        kwargs = {a.name: a.convert(values.get(a.name, a.default)) for a in self.args}
        return getattr(conn, self.method)(**kwargs)

    def describe(self, values: dict) -> str:
        return f"{self.method}({', '.join(f'{a.name}={values.get(a.name, a.default)}' for a in self.args)})"


def u8(name, default=0, title=None):
    return Arg(name, "int", default, (0, 255), title=title)


def u16(name, default=0, title=None):
    return Arg(name, "int", default, (0, 0xFFFF), title=title)


def enum(name, enum_type, default, title=None):
    return Arg(name, "enum", default, enum=enum_type, title=title)


# Persistent per-device settings. Applied individually or all at once, in this order.
# NOTE: the default values are placeholders (zero where no better value was known)
# and must be revisited once proper MDLL defaults are established.
#
# The timing role determines which device receives the DAQ start/stop/continue
# commands: exactly one enabled device must be Master, it relays the commands to
# the Slaves via the sync bus.
SETTINGS: tuple[Command, ...] = (
    Command("data_dest_port", "Data Destination Port", "set_data_dest_port", (u16("port", 54321),)),
    Command(
        "timing",
        "Timing Options",
        "set_timing_options",
        (
            enum("role", mcpd.TimingRole, "Master"),
            enum("term", mcpd.BusTermination, "On"),
            Arg("ext_sync", "bool", False),
        ),
    ),
    Command(
        "thresholds",
        "Thresholds",
        "mdll_set_thresholds",
        (u8("threshold_x"), u8("threshold_y"), u8("threshold_anode")),
    ),
    Command(
        "spectrum",
        "Spectrum",
        "mdll_set_spectrum",
        (u8("shift_x"), u8("shift_y"), u8("scale_x"), u8("scale_y")),
    ),
    Command(
        "energy_window",
        "Energy Window",
        "mdll_set_energy_window",
        (u8("lower_threshold", 0), u8("upper_threshold", 255)),
    ),
    Command(
        "timing_window",
        "Timing Window",
        "mdll_set_timing_window",
        (
            u16("t_sum_limit_x_low", 0),
            u16("t_sum_limit_x_high", 1024),
            u16("t_sum_limit_y_low", 0),
            u16("t_sum_limit_y_high", 1024),
        ),
    ),
    Command(
        "pulser",
        "Pulser",
        "mdll_set_pulser",
        (
            Arg("enable", "bool", False),
            Arg("amplitude", "int", 0, (0, 3)),
            enum("position", mcpd.MdllChannelPosition, "Middle"),
        ),
    ),
)

SETTINGS_BY_KEY = {c.key: c for c in SETTINGS}


def _update_id(cfg, values):
    cfg.mcpd_id = int(values["new_id"])


def _update_address(cfg, values):
    cfg.address = values["address"]


# One-shot commands. Not persisted.
COMMANDS: tuple[Command, ...] = (
    Command("get_version", "Get Version", "get_version"),
    Command("get_all_parameters", "Get Parameters", "get_all_parameters"),
    Command("read_register", "Read Register", "read_register", (Arg("address", "hex", "0x0000"),)),
    Command(
        "write_register",
        "Write Register",
        "write_register",
        (Arg("address", "hex", "0x0000"), Arg("value", "hex", "0x0000")),
    ),
    Command("set_run_id", "Set Run Id", "set_run_id", (u16("run_id"),)),
    Command("set_master_clock", "Set Master Clock", "set_master_clock_value", (Arg("clock", "int", 0, (0, 2**48 - 1)),)),
    Command(
        "setup_cell",
        "Setup Cell",
        "setup_cell",
        (
            enum("cell", mcpd.CellName, "Monitor0"),
            enum("trigger_source", mcpd.TriggerSource, "NoTrigger"),
            u16("compare_register_bit_value"),
        ),
    ),
    Command(
        "setup_auxtimer",
        "Setup Aux Timer",
        "setup_auxtimer",
        (Arg("timer_id", "int", 0, (0, 3)), u16("compare_register_value")),
    ),
    Command(
        "set_param_source",
        "Set Param Source",
        "set_param_source",
        (Arg("param", "int", 0, (0, 3)), enum("source", mcpd.DataSource, "Monitor0")),
    ),
    Command("set_dac_output", "Set DAC Output", "set_dac_output_values", (u16("dac0_value"), u16("dac1_value"))),
    Command("set_tx_data_set", "Set TX Data Set", "mdll_set_tx_data_set", (enum("data_set", mcpd.MdllTxDataSet, "Default"),)),
    Command("set_id", "Set Id", "set_id", (u8("new_id"),), on_success=_update_id),
    Command("set_ip_address", "Set IP Address (MCPD/MDLL v0/v1 only)", "set_ip_address", (Arg("address", "str", "192.168.168.121"),), on_success=_update_address),
    Command(
        "set_ip_and_data_dest",
        "Set IP And Data Destination (MCPD/MDLL v0/v1 only)",
        "set_ip_address_and_data_dest",
        (
            Arg("address", "str", "192.168.168.121"),
            Arg("data_dest_address", "str", "0.0.0.0"),
            u16("data_dest_port", 54321),
        ),
        on_success=_update_address,
    ),
    Command("reset_daq", "Reset DAQ", "reset_daq"),
    Command("start_daq", "Start DAQ", "start_daq"),
    Command("stop_daq", "Stop DAQ", "stop_daq"),
    Command("continue_daq", "Continue DAQ", "continue_daq"),
)


def format_result(result) -> str:
    if result is None:
        return "ok"
    if isinstance(result, mcpd.McpdVersionInfo):
        return f"cpu={result.cpu[0]}.{result.cpu[1]}, fpga={result.fpga[0]}.{result.fpga[1]}"
    if isinstance(result, mcpd.McpdParams):
        return (
            f"adc={result.adc}, dac={result.dac}, ttl_out={result.ttl_out:#x}, "
            f"ttl_in={result.ttl_in:#x}, event_counters={result.event_counters}, params={result.params}"
        )
    if isinstance(result, int):
        return f"{result} ({result:#x})"
    return str(result)
