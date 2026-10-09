cmake_minimum_required(VERSION 3.21)

# Derive version from the most recent git tag (e.g. v1.2.3 → 1.2.3).
# Falls back to "dev" when no tag exists or git is unavailable.
execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE _GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
string(REGEX REPLACE "^v" "" _APP_VERSION "${_GIT_TAG}")
if(NOT _APP_VERSION OR _APP_VERSION STREQUAL "")
    set(_APP_VERSION "dev")
endif()

project(QuickRecord
        DESCRIPTION "Shutter-style screen to GIF/MP4 recorder"
        LANGUAGES CXX)

set(PROJECT_VERSION "${_APP_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb xcb-keysyms)
find_package(spdlog REQUIRED)
find_package(GTest REQUIRED)

configure_file(src/BuildInfo.h.in ${CMAKE_BINARY_DIR}/generated/BuildInfo.h @ONLY)

add_subdirectory(src)

include(GNUInstallDirs)
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
install(TARGETS quickrecord RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
