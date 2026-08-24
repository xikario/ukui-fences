cmake_minimum_required(VERSION 3.16)
project(ukui-fences VERSION 0.4.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_SKIP_BUILD_RPATH ON)

set(UKUI_FENCES_QT_SDK_ROOT
    "${CMAKE_SOURCE_DIR}/.local-deps"
    CACHE PATH "Qt5 SDK root used when system development files are unavailable")

find_package(Qt5 QUIET COMPONENTS Core Gui Widgets DBus)
if(NOT Qt5_FOUND)
    list(PREPEND CMAKE_PREFIX_PATH
        "${UKUI_FENCES_QT_SDK_ROOT}/root/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake")
    find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets DBus)
endif()
find_package(ZLIB REQUIRED)

find_path(UKUI_FENCES_X11_INCLUDE_DIR X11/Xlib.h
    HINTS "${UKUI_FENCES_QT_SDK_ROOT}/root/usr/include")
find_library(UKUI_FENCES_X11_LIBRARY
    NAMES X11 libX11.so.6
    HINTS
        "/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
        "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}")
if(NOT UKUI_FENCES_X11_INCLUDE_DIR OR NOT UKUI_FENCES_X11_LIBRARY)
    message(FATAL_ERROR "X11 development headers or runtime library were not found")
endif()

add_executable(ukui-fences
    src/main.cpp
    src/DesktopCanvas.cpp
    src/FenceWidget.cpp
    src/DesktopIcon.cpp
    src/FileClipboard.cpp
    src/SystemMonitor.cpp
    src/SmartSpaceWidget.cpp
    src/MenuStyle.h
    resources.qrc
)

target_compile_definitions(ukui-fences PRIVATE
    UKUI_FENCES_SMART_INDEXER_PATH="${CMAKE_SOURCE_DIR}/scripts/smart_space_indexer.py"
    UKUI_FENCES_KNOWLEDGE_WORKER_PATH="${CMAKE_SOURCE_DIR}/scripts/smart_space_knowledge.py")

target_include_directories(ukui-fences PRIVATE
    src/
    ${UKUI_FENCES_X11_INCLUDE_DIR}
)

target_link_libraries(ukui-fences PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::DBus
    ZLIB::ZLIB
    ${UKUI_FENCES_X11_LIBRARY}
)

if(BUILD_TESTING)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME smart_space_indexer
            COMMAND ${Python3_EXECUTABLE} -m unittest -v
                    tests/test_smart_space_indexer.py)
        set_tests_properties(smart_space_indexer PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 180)
        add_test(NAME smart_space_knowledge
            COMMAND ${Python3_EXECUTABLE} -m unittest -v
                    tests/test_smart_space_knowledge.py)
        set_tests_properties(smart_space_knowledge PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 180)
    endif()

    find_program(XVFB_RUN_EXECUTABLE xvfb-run)
    find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)
    if(XVFB_RUN_EXECUTABLE AND DBUS_RUN_SESSION_EXECUTABLE)
        add_test(NAME smart_space_ui_smoke
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 1440x900x24"
                    ${DBUS_RUN_SESSION_EXECUTABLE} --
                    ${CMAKE_SOURCE_DIR}/tests/run_smart_space_ui_smoke.sh
                    $<TARGET_FILE:ukui-fences>)
        set_tests_properties(smart_space_ui_smoke PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 120
            LABELS "integration;ui")
        add_test(NAME smart_space_policy_smoke
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 1440x900x24"
                    ${DBUS_RUN_SESSION_EXECUTABLE} --
                    ${CMAKE_SOURCE_DIR}/tests/run_smart_space_policy_smoke.sh
                    $<TARGET_FILE:ukui-fences>)
        set_tests_properties(smart_space_policy_smoke PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 60
            LABELS "integration;ui;policy")
        add_test(NAME smart_space_new_features_smoke
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 1440x900x24"
                    ${DBUS_RUN_SESSION_EXECUTABLE} --
                    ${CMAKE_SOURCE_DIR}/tests/run_smart_space_new_features_smoke.sh
                    $<TARGET_FILE:ukui-fences>)
        set_tests_properties(smart_space_new_features_smoke PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 90
            LABELS "integration;ui;theme")
        add_test(NAME smart_space_resource_benchmark
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 1440x900x24"
                    ${DBUS_RUN_SESSION_EXECUTABLE} --
                    ${CMAKE_SOURCE_DIR}/tests/run_smart_space_resource_benchmark.sh
                    $<TARGET_FILE:ukui-fences>)
        set_tests_properties(smart_space_resource_benchmark PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 180
            LABELS "integration;resource")
    endif()
endif()

install(TARGETS ukui-fences RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS packaging/ukui-fences-launcher DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS scripts/smart_space_indexer.py
                 scripts/smart_space_knowledge.py
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/ukui-fences)
install(FILES packaging/ukui-fences.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/ukui-fences.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES packaging/ukui-fences.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(DIRECTORY assets/fence-icons/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/ukui-fences/fence-icons
        FILES_MATCHING PATTERN "*.svg")
install(FILES packaging/ukui-fences-autostart.desktop
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/xdg/autostart)
install(FILES README.md docs/SMART_SPACE.md docs/SMART_SPACE_TEST_REPORT.md
              docs/SMART_SPACE_KNOWLEDGE.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
