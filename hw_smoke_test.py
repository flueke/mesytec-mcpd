#!/usr/bin/env python3
"""Minimal, ad-hoc smoke test against a real MCPD or MDLL unit.

Not a pytest suite on purpose - meant to be run by hand at the instrument,
against an installed/built mesytec_mcpd (not the raw build tree):

    python test/hw_smoke_test.py --address 192.168.168.121
    python test/hw_smoke_test.py --address mcpd-0012 --id 0 --mdll

Does: connect -> read version -> write/read back a scratch register
(round-trip) -> optionally exercise a couple of MDLL setters -> disconnect.
Exits non-zero and prints the failing step on any error.
"""

import argparse
import sys

import mesytec_mcpd as mcpd

# Vendor register space is unspecified in the public docs; this address is
# only used as a round-trip scratch value, not a documented/meaningful
# register.
SCRATCH_REGISTER_ADDRESS = 0x1000
SCRATCH_REGISTER_VALUE = 0xC0FFEE


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True, help="MCPD/MDLL ip address or hostname")
    parser.add_argument("--id", type=int, default=0, help="mcpd/mdll id (default: 0)")
    parser.add_argument("--port", type=int, default=54321, help="command port (default: 54321)")
    parser.add_argument(
        "--mdll", action="store_true", help="also exercise a couple of MDLL-specific setters"
    )
    args = parser.parse_args()

    print(f"connecting to {args.address}:{args.port} (id={args.id}) ...")
    conn = mcpd.McpdConnection(args.address, mcpd_id=args.id, port=args.port)

    try:
        vi = conn.get_version()
        print(f"  version: cpu={vi.cpu}, fpga={vi.fpga}")

        print(
            f"  write_register(0x{SCRATCH_REGISTER_ADDRESS:04X}, "
            f"0x{SCRATCH_REGISTER_VALUE:08X}) ..."
        )
        conn.write_register(SCRATCH_REGISTER_ADDRESS, SCRATCH_REGISTER_VALUE)

        readback = conn.read_register(SCRATCH_REGISTER_ADDRESS)
        print(f"  read_register(0x{SCRATCH_REGISTER_ADDRESS:04X}) -> 0x{readback:08X}")
        if readback != SCRATCH_REGISTER_VALUE:
            print("  FAIL: register readback does not match what was written", file=sys.stderr)
            return 1

        if args.mdll:
            print("  mdll_set_energy_window(lower_threshold=0, upper_threshold=255) ...")
            conn.mdll_set_energy_window(lower_threshold=0, upper_threshold=255)

        print("OK")
        return 0
    finally:
        conn.close()


if __name__ == "__main__":
    raise SystemExit(main())
