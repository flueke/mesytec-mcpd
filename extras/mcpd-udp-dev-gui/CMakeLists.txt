set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

find_package(Qt5 COMPONENTS Widgets Gui)

if (Qt5Widgets_FOUND)

    if ("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
        add_definitions(-DQT_NO_DEBUG_OUTPUT)
    endif()

    add_executable(mcpd-udp-dev-gui
        code_editor.cc
        main.cc
        mainwindow.ui
        mcpd_udp_gui.cc
        resources.qrc
        util/qt_logview.cc
        util/qt_monospace_textedit.cc
        util/qt_util.cc
    )

    target_link_libraries(mcpd-udp-dev-gui
        PRIVATE Qt5::Widgets
        PRIVATE Qt5::Gui
        PRIVATE mesytec-mcpd)

    # For some reason linking against spdlog::spdlog in the
    # target_link_libraries() call above leads to "multiple definition" errors
    # for spdlog functions under windows. The workaround is to just add the
    # spdlog include directory to the include paths. This does not happen in
    # the mvme or mesytec-mvlc projects. Also the mcpd-cli links just fine.
    target_include_directories(mcpd-udp-dev-gui PRIVATE ${CMAKE_SOURCE_DIR}/external/spdlog/include)

    # install
    install(TARGETS mcpd-udp-dev-gui
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

endif()
