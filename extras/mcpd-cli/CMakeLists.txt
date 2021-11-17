list(APPEND CMAKE_PREFIX_PATH $ENV{ROOTSYS})
find_package(ROOT COMPONENTS Core RIO Tree Hist)

if (ROOT_FOUND)
    message("-- Using ROOT installation from ${ROOT_USE_FILE}")
    include(${ROOT_USE_FILE})
endif()


add_executable(mcpd-cli mcpd-cli.cc)

target_link_libraries(mcpd-cli
    PRIVATE mesytec-mcpd
    PRIVATE bfg::Lyra
    PRIVATE spdlog::spdlog
    #PRIVATE gnuplot_iostream
    )

if (ROOT_FOUND)
    target_compile_definitions(mcpd-cli PRIVATE -DMESYTEC_MCPD_ENABLE_ROOT)
    target_sources(mcpd-cli PRIVATE mcpd_root_histos.cc)
    target_link_libraries(mcpd-cli
        PRIVATE ${ROOT_LIBRARIES}
        )
endif()

install(TARGETS
    mcpd-cli
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    )
