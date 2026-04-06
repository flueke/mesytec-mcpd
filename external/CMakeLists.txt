# spdlog
option(SPDLOG_FMT_EXTERNAL "Use external fmt library instead of bundled" OFF)
option(SPDLOG_INSTALL "Generate the install target" ON)
add_subdirectory(spdlog)
set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (WIN32)
    target_compile_definitions(spdlog PRIVATE _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
endif()

# Lyra command line parser
add_subdirectory(lyra-1.7.0)

set(PYBIND11_FINDPYTHON ON)
add_subdirectory(pybind11-3.0.1)
