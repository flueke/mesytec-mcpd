# spdlog
option(SPDLOG_FMT_EXTERNAL "Use external fmt library instead of bundled" OFF)
option(SPDLOG_INSTALL "Generate the install target" ON)
add_subdirectory(spdlog)
set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Lyra command line parser
add_subdirectory(lyra-1.7.0)

# nanobind
if (Python_FOUND)
    message(STATUS "Python found: ${Python_EXECUTABLE}")
    add_subdirectory(nanobind-2.12.0)
else()
    message(WARNING "Python not found, not building Python bindings for mesytec-mcpd")
endif()
