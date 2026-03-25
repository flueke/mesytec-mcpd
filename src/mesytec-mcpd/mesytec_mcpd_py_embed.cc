#include "mesytec_mcpd_py.h"
#include <pybind11/embed.h>

PYBIND11_EMBEDDED_MODULE(_mesytec_mcpd_py, m)
{
    init_py_module(m);
}
