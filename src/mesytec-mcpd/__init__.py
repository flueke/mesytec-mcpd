# Re-export everything from the compiled extension module
from ._mesytec_mcpd import *

__version__ = _mesytec_mcpd.__version__


# Sets up the spdlog -> pybind11 -> logging bridge.
_mesytec_mcpd.init()

try:
    from . import gui
except ImportError:
    pass
