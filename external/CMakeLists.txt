# spdlog
option(SPDLOG_FMT_EXTERNAL "Use external fmt library instead of bundled" OFF)
option(SPDLOG_INSTALL "Generate the install target" ON)
add_subdirectory(spdlog)
set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (WIN32)
    target_compile_definitions(spdlog PRIVATE _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
endif()

# Lyra command line parser
if (NOT TARGET lyra)
    add_subdirectory(lyra-1.7.0)
endif()

find_package(Python 3.12 COMPONENTS Interpreter Development GLOBAL)

message(STATUS "${PROJECT_NAME}: pre: Python_FOUND=${Python_FOUND}, pybind11_FOUND=${pybind11_FOUND}")

if (Python_FOUND AND NOT pybind11_FOUND)
    add_subdirectory(pybind11-3.0.1)
endif()

message(STATUS "${PROJECT_NAME}: post: Python_FOUND=${Python_FOUND}, pybind11_FOUND=${pybind11_FOUND}")
