from __future__ import annotations

import click

import mesytec_mcpd as mcpd

from .util import BASED_INT, enum_param, handle_mcpd_errors


@click.group("mcpd")
def mcpd_group():
    """Generic MCPD/MDLL base commands."""


@mcpd_group.command()
@click.pass_obj
@handle_mcpd_errors
def version(ctx):
    """Read cpu and fpga version info."""
    vi = ctx.connection.get_version()
    click.echo(f"MCPD cpu={vi.cpu[0]}.{vi.cpu[1]}, fpga={vi.fpga[0]}.{vi.fpga[1]}")


@mcpd_group.command("find-id")
@click.pass_obj
@handle_mcpd_errors
def find_id(ctx):
    """Find the 'id' value of MCPD-8_v1 (older) modules."""
    found_id = mcpd.find_mcpd_id(ctx.address, ctx.port)
    click.echo(f"Found mcpd_id={found_id}")


@mcpd_group.command()
@click.argument("new_address")
@click.argument("new_id", type=int)
@click.argument("data_dest_address", default="0.0.0.0")
@click.argument("data_port", type=int, default=54321)
@click.pass_obj
@handle_mcpd_errors
def setup(ctx, new_address, new_id, data_dest_address, data_port):
    """MCPD base setup (MCPD-8_v1 only, for v2 only the data dest port can be changed)."""
    # Setting the id is not part of the wire-level SetProtoParams command, added here for
    # convenience so 'setup' handles all MCPD-8_v1 settings in one call, like mcpd-cli.
    ctx.connection.set_id(new_id)
    ctx.connection.mcpd_id = new_id
    ctx.connection.set_ip_address_and_data_dest(new_address, data_dest_address, data_port)


@mcpd_group.command("setid")
@click.argument("new_id", type=int)
@click.pass_obj
@handle_mcpd_errors
def setid(ctx, new_id):
    """Set MCPD id (MCPD-8_v1 only, v2 mirrors the id given in command packets)."""
    ctx.connection.set_id(new_id)
    ctx.connection.mcpd_id = new_id


@mcpd_group.command("set-data-port")
@click.argument("data_port", type=int)
@click.pass_obj
@handle_mcpd_errors
def set_data_port(ctx, data_port):
    """Set the MCPD data destination port."""
    ctx.connection.set_data_dest_port(data_port)


@mcpd_group.command()
@click.argument("role", type=enum_param(mcpd.TimingRole))
@click.argument("termination", type=enum_param(mcpd.BusTermination))
@click.option("--ext-sync/--no-ext-sync", default=False, help="enable external sync")
@click.pass_obj
@handle_mcpd_errors
def timing(ctx, role, termination, ext_sync):
    """Bus master/slave setup."""
    ctx.connection.set_timing_options(
        mcpd.TimingRole[role], mcpd.BusTermination[termination], ext_sync
    )


@mcpd_group.command()
@click.argument("run_id", type=int)
@click.pass_obj
@handle_mcpd_errors
def runid(ctx, run_id):
    """Set the mcpd runId for the next DAQ run."""
    ctx.connection.set_run_id(run_id)


@mcpd_group.command()
@click.argument("cell_id", type=enum_param(mcpd.CellName))
@click.argument("trigger", type=enum_param(mcpd.TriggerSource))
@click.argument("compare_register", type=int, default=0)
@click.pass_obj
@handle_mcpd_errors
def cell(ctx, cell_id, trigger, compare_register):
    """Counter cell setup."""
    ctx.connection.setup_cell(mcpd.CellName[cell_id], mcpd.TriggerSource[trigger], compare_register)


@mcpd_group.command()
@click.argument("timer_id", type=int)
@click.argument("capture_value", type=int)
@click.pass_obj
@handle_mcpd_errors
def timer(ctx, timer_id, capture_value):
    """Timer setup."""
    ctx.connection.setup_auxtimer(timer_id, capture_value)


@mcpd_group.command("set-master-clock")
@click.argument("clock_value", type=int)
@click.pass_obj
@handle_mcpd_errors
def set_master_clock(ctx, clock_value):
    """Set master clock value (48 bit unsigned)."""
    ctx.connection.set_master_clock_value(clock_value)


@mcpd_group.command("param-source")
@click.argument("param", type=int)
@click.argument("source", type=enum_param(mcpd.DataSource))
@click.pass_obj
@handle_mcpd_errors
def param_source(ctx, param, source):
    """Set parameter source."""
    ctx.connection.set_param_source(param, mcpd.DataSource[source])


@mcpd_group.command("get-parameters")
@click.pass_obj
@handle_mcpd_errors
def get_parameters(ctx):
    """Read and print the current parameter values."""
    params = ctx.connection.get_all_parameters()
    click.echo(f"ADC1={params.adc[0]}, ADC2={params.adc[1]}")
    click.echo(f"DAC1={params.dac[0]}, DAC2={params.dac[1]}")
    click.echo(f"TTL out={params.ttl_out}, TTL in={params.ttl_in}")
    for i, value in enumerate(params.params):
        click.echo(f"Parameter{i}: {value}")


@mcpd_group.command("dac-setup")
@click.argument("dac0", type=int)
@click.argument("dac1", type=int)
@click.pass_obj
@handle_mcpd_errors
def dac_setup(ctx, dac0, dac1):
    """MCPD DAC unit setup (12 bit values)."""
    ctx.connection.set_dac_output_values(dac0, dac1)


@mcpd_group.command("scan-busses")
@click.pass_obj
@handle_mcpd_errors
def scan_busses(ctx):
    """Scan MCPD busses for connected MPSD modules."""
    result = ctx.connection.scan_busses()
    for bus, value in enumerate(result):
        click.echo(f"[{bus}]: {value}")


@mcpd_group.command("get-bus-capabilities")
@click.pass_obj
@handle_mcpd_errors
def get_bus_capabilities(ctx):
    """Get MCPD bus transmit capabilities."""
    caps = ctx.connection.get_bus_capabilities()
    click.echo(f"available=0x{caps.available:02X}, current=0x{caps.selected:02X}")


@mcpd_group.command("set-bus-capabilities")
@click.argument("value", type=BASED_INT)
@click.pass_obj
@handle_mcpd_errors
def set_bus_capabilities(ctx, value):
    """Set MCPD bus transmit capabilities."""
    result = ctx.connection.set_bus_capabilities(value)
    click.echo(f"wanted=0x{value:02X}, got=0x{result:02X}")


@mcpd_group.command("write-register")
@click.argument("address", type=BASED_INT)
@click.argument("value", type=BASED_INT)
@click.pass_obj
@handle_mcpd_errors
def write_register(ctx, address, value):
    """Write MCPD/MDLL internal register (modern versions only)."""
    ctx.connection.write_register(address, value)


@mcpd_group.command("read-register")
@click.argument("address", type=BASED_INT)
@click.pass_obj
@handle_mcpd_errors
def read_register(ctx, address):
    """Read MCPD/MDLL internal register (modern versions only)."""
    value = ctx.connection.read_register(address)
    click.echo(f"0x{address:04X} = 0x{value:08X} ({value} decimal)")


@mcpd_group.command("read-peripheral-register")
@click.argument("mpsd_id", type=int)
@click.argument("register_number", type=int)
@click.pass_obj
@handle_mcpd_errors
def read_peripheral_register(ctx, mpsd_id, register_number):
    """Read peripheral module (MPSD/MSTD) register."""
    value = ctx.connection.read_peripheral_register(mpsd_id, register_number)
    click.echo(
        f"mpsdId={mpsd_id}, register={register_number}, value=0x{value:04X} ({value} decimal)"
    )


@mcpd_group.command("write-peripheral-register")
@click.argument("mpsd_id", type=int)
@click.argument("register_number", type=int)
@click.argument("register_value", type=int)
@click.pass_obj
@handle_mcpd_errors
def write_peripheral_register(ctx, mpsd_id, register_number, register_value):
    """Write peripheral module (MPSD/MSTD) register."""
    ctx.connection.write_peripheral_register(mpsd_id, register_number, register_value)


@mcpd_group.command("decode-all-inputs-parameter")
@click.argument("value", type=BASED_INT)
def decode_all_inputs_parameter(value):
    """Decode a param_source=AllDigitalAndAdcInputs (48 bit) parameter value."""
    lo = value & 0xFFFF
    mid = (value >> 16) & 0xFFFF
    hi = (value >> 32) & 0xFFFF

    # parameter_Lo = xb00,xxxxxxxx,11,m/c[3:0] - see mcpd-cli.cc DecodeAllInputsParameter.
    lo_match_mask = 0b00_00000000_11_0000
    lo_extract_mask = 0b00_00000000_00_1111

    if (lo & lo_match_mask) != lo_match_mask:
        click.echo("warning: lo does not match expected bitmask", err=True)

    inputs_status = lo & lo_extract_mask
    click.echo(f"param=0x{value:012x}, lo=0x{lo:04x}, mid=0x{mid:04x}, hi=0x{hi:04x}")
    click.echo(f"inputsStatus={inputs_status:#04x} -> {inputs_status:#06b}")
