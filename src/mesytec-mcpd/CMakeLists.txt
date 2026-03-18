option(MCPD_ENABLE_PYTHON "Build mesytec-mcpd with python bindings" ON)

find_package(Threads)

configure_file("git_version.cc.in" "git_version.cc" @ONLY)

add_library(mesytec-mcpd SHARED
    "${CMAKE_CURRENT_BINARY_DIR}/git_version.cc"
    mcpd_core.cc
    mcpd_functions.cc
    mdll_functions.cc
    util/logging.cc
    util/udp_sockets.cc
    )

target_include_directories(mesytec-mcpd
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>
    PUBLIC $<INSTALL_INTERFACE:include>
    )

target_link_libraries(mesytec-mcpd
    PRIVATE spdlog::spdlog
    PUBLIC Threads::Threads
    )

target_compile_features(mesytec-mcpd PUBLIC cxx_std_17)

if (MSYS OR NOT WIN32)
    target_compile_options(mesytec-mcpd PRIVATE -Wall -Wextra)
    target_compile_options(mesytec-mcpd PRIVATE -Wno-format)
endif()

if (WIN32)
    target_link_libraries(mesytec-mcpd PRIVATE ws2_32 winmm)
    target_compile_definitions(mesytec-mcpd PRIVATE -DNOMINMAX)
endif(WIN32)

include(GenerateExportHeader)
generate_export_header(mesytec-mcpd)

if (MCPD_ENABLE_PYTHON)
    if (pybind11_FOUND)
        message("-- Found pybind11: ${pybind11_DIR}")
    else()
        message(WARNING "Could not find pybind11. Disabling Python support in mesytec-mcpd")
    endif()
endif()

if (MCPD_ENABLE_PYTHON AND pybind11_FOUND)
    # Note: py.typed is required by PEP 561. It's just an empty file.
    if (DEFINED SKBUILD)
        pybind11_add_module(_mesytec_mcpd_py MODULE mesytec_mcpd_py.cc mcpd_py_lib.cc)
        target_link_libraries(_mesytec_mcpd_py PRIVATE mesytec-mcpd spdlog::spdlog)
        install(TARGETS _mesytec_mcpd_py mesytec-mcpd DESTINATION ${SKBUILD_PROJECT_NAME})
        install(FILES __init__.py py.typed DESTINATION ${SKBUILD_PROJECT_NAME})
        # Install gui submodule
        install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../gui
                DESTINATION ${SKBUILD_PROJECT_NAME}
                FILES_MATCHING PATTERN "*.py")
    else()
        pybind11_add_module(_mesytec_mcpd_py MODULE mesytec_mcpd_py.cc mcpd_py_lib.cc)
        target_link_libraries(_mesytec_mcpd_py PRIVATE mesytec-mcpd spdlog::spdlog)
        install(TARGETS _mesytec_mcpd_py
            LIBRARY DESTINATION lib/mesytec_mcpd_py
            RUNTIME DESTINATION bin
            ARCHIVE DESTINATION lib/mesytec_mcpd_py
            )
        install(FILES __init__.py py.typed DESTINATION lib/mesytec_mcpd_py)
        # Install gui submodule
        install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../gui
                DESTINATION lib/mesytec_mcpd_py
                FILES_MATCHING PATTERN "*.py")
    endif()

    find_program(PYBIND11_STUBGEN pybind11-stubgen)

    if (PYBIND11_STUBGEN)
        add_custom_command(TARGET _mesytec_mcpd_py POST_BUILD
            COMMAND PYTHONPATH=${CMAKE_BINARY_DIR}/python:$ENV{PYTHONPATH} pybind11-stubgen -o ${CMAKE_BINARY_DIR}/stubs mesytec_mcpd_py
            BYPRODUCTS ${CMAKE_BINARY_DIR}/stubs
            USES_TERMINAL
            COMMENT "Generating python type stubs")

        if (DEFINED SKBUILD)
            install(DIRECTORY ${CMAKE_BINARY_DIR}/stubs DESTINATION ${SKBUILD_PROJECT_NAME})
        else()
            install(DIRECTORY ${CMAKE_BINARY_DIR}/stubs DESTINATION lib/mesytec_mcpd_py)
        endif()
    endif()

endif()

set(INSTALL_TARGETS mesytec-mcpd)

if (NOT DEFINED SKBUILD)
    # Export and installation
    include(CMakePackageConfigHelpers)
    set(project_config ${PROJECT_BINARY_DIR}/mesytec-mcpd-config.cmake)
    set(targets_export_name mesytec-mcpd-targets)

    configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/tools/cmake/mesytec-mcpd-config.cmake.in"
        ${project_config}
        INSTALL_DESTINATION lib/cmake/mesytec-mcpd)

    export(TARGETS mesytec-mcpd NAMESPACE mesytec-mcpd::
        FILE ${PROJECT_BINARY_DIR}/${targets_export_name}.cmake)

    install(FILES ${project_config}
        DESTINATION lib/cmake/mesytec-mcpd)

    install(EXPORT ${targets_export_name}
        DESTINATION lib/cmake/mesytec-mcpd
        NAMESPACE mesytec-mcpd::
        )

    install(TARGETS mesytec-mcpd
        EXPORT ${targets_export_name}
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        )

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
        DESTINATION include/mesytec-mcpd
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mesytec-mcpd_export.h
        DESTINATION include/mesytec-mcpd)
endif()

if (MCPD_BUILD_TESTS)
    function(add_gtest exe_name exe_source)
        add_executable(${exe_name} ${exe_source})

        target_link_libraries(${exe_name}
            PRIVATE mesytec-mcpd
            PRIVATE gtest
            PRIVATE gtest_main
            PRIVATE spdlog::spdlog
            )

        add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
    endfunction(add_gtest)

    if (MCPD_ENABLE_PYTHON AND pybind11_FOUND)
        add_gtest(test_mcpd_py_lib mcpd_py_lib.test.cc)
        target_sources(test_mcpd_py_lib PRIVATE mcpd_py_lib.cc)
        target_link_libraries(test_mcpd_py_lib PRIVATE pybind11::embed)
    endif()
endif(MCPD_BUILD_TESTS)
