option(MCPD_BUILD_MCPD_CLI "Build the MCPD command line interface" ON)
if (MCPD_BUILD_MCPD_CLI)
    add_subdirectory(mcpd-cli)
endif()
