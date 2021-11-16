add_executable(mcpd-cli mcpd-cli.cc)
target_link_libraries(mcpd-cli
    PRIVATE mesytec-mcpd
    PRIVATE bfg::Lyra
    PRIVATE spdlog::spdlog)

install(TARGETS
    mcpd-cli
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    )
