# Re-export everything from the compiled extension module
from ._mesytec_mcpd_py import *

__version__ = _mesytec_mcpd_py.__version__


# Sets up the spdlog -> pybind11 -> logging bridge.
_mesytec_mcpd_py.init()

try:
    from . import gui
except ImportError:
    pass
