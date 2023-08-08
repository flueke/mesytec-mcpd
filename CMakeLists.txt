cmake_minimum_required(VERSION 3.12)

# Check if mesytec-mcpd is being used directly or via add_subdirectory
set(MESYTEC_MCPD_MASTER_PROJECT OFF)
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(MESYTEC_MCPD_MASTER_PROJECT ON)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib")

# Add the local cmake directory module path.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake")

# Pull information from git:
# GIT_SHA1 contains the complete git hash
#
# GIT_VERSION contains the name of the latest git tag, the number of commits
# since that tag was created and the start of the git hash if there have been
# changes since the tag was created.
# Example GIT_VERSION: 0.7-371-g12d9678

# GIT_VERSION_SHORT contains the same as GIT_VERSION but without the start of
# the last commits hash.
# Example GIT_VERSION_SHORT: 0.7-371

# GIT_VERSION_TAG contains only the tag part of the version
# Example GIT_VERSION_TAG: 0.7
include(GetGitRevisionDescription)
get_git_head_revision(GIT_REFSPEC GIT_SHA1)
git_describe(GIT_VERSION "--always")

if(GIT_VERSION MATCHES v[^-]*-)
    string(REGEX REPLACE "v([^-]*)-([0-9]+)-.*" "\\1.\\2"  GIT_VERSION_SHORT ${GIT_VERSION})
else()
    string(REGEX REPLACE "v(.*)" "\\1" GIT_VERSION_SHORT ${GIT_VERSION})
endif()

execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE)

message("-- GIT_VERSION=${GIT_VERSION}")
message("-- GIT_VERSION_SHORT=${GIT_VERSION_SHORT}")
message("-- GIT_BRANCH=${GIT_BRANCH}")

project(mesytec-mcpd
    DESCRIPTION "User space driver library for the Mesytec PSD+ system" VERSION ${GIT_VERSION_SHORT})

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug")
endif()

message("-- CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} (mvme)")

## Optional tests
#option(MCPD_BUILD_TESTS "Build test binaries" ${MESYTEC_MCPD_MASTER_PROJECT})
#
#if (MCPD_BUILD_TESTS)
#    #  # googletest / GTest
#    #  # https://github.com/google/googletest/blob/master/googletest/README.md
#    #  # Download and unpack googletest at configure time
#    #  configure_file(tools/cmake/googletest_CMakeLists.txt.in googletest-download/CMakeLists.txt)
#    #  execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
#    #    RESULT_VARIABLE result
#    #    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download )
#    #  if(result)
#    #    message(FATAL_ERROR "CMake step for googletest failed: ${result}")
#    #  endif()
#    #  execute_process(COMMAND ${CMAKE_COMMAND} --build .
#    #    RESULT_VARIABLE result
#    #    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download )
#    #  if(result)
#    #    message(FATAL_ERROR "Build step for googletest failed: ${result}")
#    #  endif()
#
#    #  # Prevent overriding the parent project's compiler/linker
#    #  # settings on Windows
#    #  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
#
#    #  # Add googletest directly to our build. This defines
#    #  # the gtest and gtest_main targets.
#    #  add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/googletest-src
#    #                   ${CMAKE_CURRENT_BINARY_DIR}/googletest-build
#    #                   EXCLUDE_FROM_ALL)
#
#    #  # The gtest/gtest_main targets carry header search path
#    #  # dependencies automatically when using CMake 2.8.11 or
#    #  # later. Otherwise we have to add them here ourselves.
#    #  if (CMAKE_VERSION VERSION_LESS 2.8.11)
#    #    include_directories("${gtest_SOURCE_DIR}/include")
#    #  endif()
#
#    add_subdirectory(external/googletest)
#
#    message("-- Building tests")
#    include(CTest)
#    enable_testing()
#endif (MCPD_BUILD_TESTS)

set(not-msvc $<NOT:$<CXX_COMPILER_ID:MSVC>>)

add_subdirectory(external)

# Create our binaries in the root of the build directory. It's just more convenient.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_subdirectory(extras)
add_subdirectory(src)

if ("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
    set(PACKAGE_ARCH_STRING "x64")
else()
    set(PACKAGE_ARCH_STRING "x32")
endif()

# Note: To produce stand-alone packages additional files need to be deployed to
# the target system: shared objects, Qt plugins, Qt configuration, shell
# scripts, etc.
#
# Under windows These files are handled using an external deploy directory
# which is added via the install() command.
#
# Under linux the `linuxdeployqt` tool and a custom cmake script for the cpack
# `External` generator are used.

set(CPACK_PACKAGE_VERSION ${VERSION})
set(CPACK_PACKAGE_VENDOR "mesytec")
set(CPACK_PACKAGE_EXECUTABLES mcpd-cli;mcpd-cli)
set(CPACK_THREADS 0)

if (WIN32)
    # Note: CPack NSIS does automatically use the cmake/NSIS.template.in file!
    # This is important because currently not all standard settings are used in
    # the custom script, e.g. CPACK_NSIS_MUI_FINISHPAGE_RUN

    option(MVME_CPACK_INSTALL_DEPLOY_FILES
        "Install additional library files from a special deploy directory." OFF)

    set(MVME_CPACK_DEPLOY_DIR "c:/src/mvme2-deploy/windows/mvme-${PACKAGE_ARCH_STRING}-libusb-0.1-Release"
        CACHE PATH "Directory containing additional files to be deployed."
    )

    if (MVME_CPACK_INSTALL_DEPLOY_FILES)
        install(DIRECTORY "${MVME_CPACK_DEPLOY_DIR}/" DESTINATION . USE_SOURCE_PERMISSIONS)
    endif()

    set(CPACK_GENERATOR "ZIP")
    set(CPACK_SOURCE_GENERATOR "ZIP")
else()
    option(MVME_CPACK_USE_LINUXDEPLOYQT
        "Use 'linuxdeployqt' to create universal binary packages." OFF)

    set(CPACK_SOURCE_GENERATOR "TBZ2")

    if (MVME_CPACK_USE_LINUXDEPLOYQT)
        # Find linuxdeployqt here to error out early in case it's missing.
        find_program(LINUXDEPLOYQT_EXECUTABLE linuxdeployqt REQUIRED)
        set(CPACK_GENERATOR "External")
        set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_SOURCE_DIR}/tools/cmake/CPackExternal_linuxdeployqt.cmake")
        set(CPACK_EXTERNAL_ENABLE_STAGING ON)
    else()
        set(CPACK_GENERATOR "TBZ2")
    endif()
endif()

message("-- PROJECT_NAME=${PROJECT_NAME}, PROJECT_BINARY_DIR=${PROJECT_BINARY_DIR}, PROJECT_IS_TOP_LEVEL=${PROJECT_IS_TOP_LEVEL}")

# CPACK_PROJECT_CONFIG_FILE
# File included at cpack time, once per generator after setting CPACK_GENERATOR
# to the actual generator being used; allows per-generator setting of CPACK_*
# variables at cpack time.
# Default: ${PROJECT_BINARY_DIR}/CPackOptions.cmake
configure_file("CPackOptions.cmake.in" "${PROJECT_BINARY_DIR}/CPackOptions.cmake" @ONLY)
set(CPACK_PROJECT_CONFIG_FILE "${PROJECT_BINARY_DIR}/CPackOptions.cmake")

include(CPack)

# vim:tw=0
