add_executable(mcpd-cli mcpd-cli.cc)
target_link_libraries(mcpd-cli
    PRIVATE mesytec-mcpd
    PRIVATE bfg::Lyra
    PRIVATE spdlog::spdlog)

add_executable(mcpd-setup mcpd-setup.cc)
target_link_libraries(mcpd-setup
    PRIVATE mesytec-mcpd
    PRIVATE bfg::Lyra
    PRIVATE spdlog::spdlog)

install(TARGETS
    mcpd-cli
    mcpd-setup
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    )
