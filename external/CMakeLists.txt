# fmt
option(FMT_INSTALL "Generate the fmt install target." ON)
add_subdirectory(fmt)
set_target_properties(fmt PROPERTIES POSITION_INDEPENDENT_CODE ON)

# spdlog
option(SPDLOG_FMT_EXTERNAL "Use external fmt library instead of bundled" ON)
option(SPDLOG_INSTALL "Generate the install target" ON)
add_subdirectory(spdlog)
set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON)
