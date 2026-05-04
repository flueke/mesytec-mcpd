option(MCPD_CLI_WITH_ROOT "Enable support for writing ROOT histograms in mcpd-cli" off)

if(MCPD_CLI_WITH_ROOT)
    list(APPEND CMAKE_PREFIX_PATH $ENV{ROOTSYS})
    find_package(ROOT COMPONENTS Core RIO Tree Hist)

    if (ROOT_FOUND)
        message("-- Using ROOT installation from ${ROOT_USE_FILE}")
        include(${ROOT_USE_FILE})
    else()
        message(WARNING "Could not find ROOT. Disabling ROOT support in mcpd-cli")
    endif()
endif()

if (MCPD_ENABLE_PYTHON)
    if (pybind11_FOUND)
        message("-- Found pybind11: ${pybind11_DIR}")
    else()
        message(WARNING "Could not find pybind11. Disabling Python support in mcpd-cli")
    endif()
endif()

add_executable(mcpd-cli mcpd-cli.cc)

target_link_libraries(mcpd-cli
    PRIVATE mesytec-mcpd
    PRIVATE bfg::lyra
    PRIVATE spdlog::spdlog
    #PRIVATE gnuplot_iostream
    )

if (MSYS OR NOT WIN32)
    target_compile_options(mcpd-cli PRIVATE -Wno-format)
endif()

if (WIN32)
    target_link_libraries(mcpd-cli PRIVATE ws2_32 winmm)
    target_compile_definitions(mcpd-cli PRIVATE -DNOMINMAX)
endif()

# LYRA_DEBUG enables very verbose output from the lyra cli parser
#target_compile_definitions(mcpd-cli PRIVATE -DLYRA_DEBUG)

if (MCPD_CLI_WITH_ROOT AND ROOT_FOUND)
    target_compile_definitions(mcpd-cli PRIVATE -DMESYTEC_MCPD_ENABLE_ROOT)
    target_sources(mcpd-cli PRIVATE mcpd_root_histos.cc)
    target_link_libraries(mcpd-cli PRIVATE ${ROOT_LIBRARIES})
endif()

if (MCPD_ENABLE_PYTHON AND pybind11_FOUND)
    message("-- Enabling Python support in mcpd-cli")
    target_compile_definitions(mcpd-cli PRIVATE -DMESYTEC_MCPD_ENABLE_PYTHON)
    target_sources(mcpd-cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src/mesytec-mcpd/mcpd_py_embed.cc)
    target_link_libraries(mcpd-cli PRIVATE pybind11::embed $<TARGET_OBJECTS:mesytec-mcpd-python-bindings>)
    set_target_properties(mcpd-cli PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden)
endif()

if (NOT DEFINED SKBUILD)
    install(TARGETS mcpd-cli LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
endif()

# converter from qmesydaq .mdat to our .mcpdlst format
add_executable(mdat2mcpdlst mdat2mcpdlst.cc)
target_link_libraries(mdat2mcpdlst
    PRIVATE mesytec-mcpd
    PRIVATE spdlog::spdlog
    )

if (WIN32)
    target_compile_definitions(mdat2mcpdlst PRIVATE -DNOMINMAX)
endif()
