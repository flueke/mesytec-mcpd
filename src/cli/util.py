from __future__ import annotations

import functools

import click

import mesytec_mcpd as mcpd


class BasedIntParamType(click.ParamType):
    """Parses decimal, 0x-hex and 0-octal integers (like C's strtoul(s, 0, 0))."""

    name = "int"

    def convert(self, value, param, ctx):
        if isinstance(value, int):
            return value
        try:
            return int(value, 0)
        except ValueError:
            self.fail(f"{value!r} is not a valid integer", param, ctx)


BASED_INT = BasedIntParamType()


def enum_param(enum_cls) -> click.Choice:
    """click.Choice of an enum's member names. Convert back with enum_cls[name]."""
    return click.Choice(list(enum_cls.__members__))


def handle_mcpd_errors(func):
    """Turns McpdError into a clean CLI error message instead of a Python traceback."""

    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        try:
            return func(*args, **kwargs)
        except mcpd.McpdError as e:
            raise click.ClickException(str(e)) from e

    return wrapper
