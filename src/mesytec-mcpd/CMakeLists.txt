find_package(Threads)

configure_file("git_version.cc.in" "git_version.cc" @ONLY)

add_library(mesytec-mcpd SHARED
    "${CMAKE_CURRENT_BINARY_DIR}/git_version.cc"
    mcpd_functions.cc
    util/udp_sockets.cc
    )

target_include_directories(mesytec-mcpd
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>
    PUBLIC $<INSTALL_INTERFACE:include>
    )

target_link_libraries(mesytec-mcpd
    PUBLIC spdlog::spdlog
    PUBLIC Threads::Threads
    )

target_compile_options(mesytec-mcpd PRIVATE -Wall -Wextra)
target_compile_features(mesytec-mcpd PUBLIC cxx_std_14)

if (WIN32)
    target_link_libraries(mesytec-mcpd PRIVATE ws2_32 winmm)
    target_compile_options(mesytec-mcpd PRIVATE -Wno-format)
endif(WIN32)

include(GenerateExportHeader)
generate_export_header(mesytec-mcpd)

# Export and installation
include(CMakePackageConfigHelpers)
set(project_config ${PROJECT_BINARY_DIR}/mesytec-mcpd-config.cmake)
set(targets_export_name mesytec-mcpd-targets)

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/tools/cmake/mesytec-mcpd-config.cmake.in"
    ${project_config}
    INSTALL_DESTINATION lib/cmake/mesytec-mcpd)

set(INSTALL_TARGETS mesytec-mcpd spdlog)
export(TARGETS ${INSTALL_TARGETS} NAMESPACE mesytec-mcpd::
    FILE ${PROJECT_BINARY_DIR}/${targets_export_name}.cmake)

install(FILES ${project_config}
    DESTINATION lib/cmake/mesytec-mcpd)

install(EXPORT ${targets_export_name}
    DESTINATION lib/cmake/mesytec-mcpd
    NAMESPACE mesytec-mcpd::
    )

install(TARGETS ${INSTALL_TARGETS}
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

if (MCPD_BUILD_TESTS)
    function(add_gtest exe_name exe_source)
        add_executable(${exe_name} ${exe_source})

        target_link_libraries(${exe_name}
            PRIVATE mesytec-mcpd
            PRIVATE gtest
            PRIVATE gtest_main
            )

        add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
    endfunction(add_gtest)
endif(MCPD_BUILD_TESTS)
