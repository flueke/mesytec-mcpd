"""pytest config: make the built _mesytec_mcpd extension importable.

Looks for the compiled module under $MESYTEC_MCPD_BUILD_DIR if set, otherwise
falls back to the conventional ../build next to this test/ directory.
"""

import os
import sys

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_DEFAULT_BUILD_DIR = os.path.join(_THIS_DIR, "..", "build")
_BUILD_DIR = os.environ.get("MESYTEC_MCPD_BUILD_DIR", _DEFAULT_BUILD_DIR)

if os.path.isdir(_BUILD_DIR):
    sys.path.insert(0, _BUILD_DIR)
