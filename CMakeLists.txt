cmake_minimum_required(VERSION 3.20)
project(pipeasio C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                 Debug Release RelWithDebInfo)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# libpipewire-0.3 backs src/audio.c — its headers feed the OBJECT lib
# compile step via INCLUDES, and -lpipewire-0.3 reaches the winegcc link
# step via LIBS.
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
message(STATUS "Found libpipewire-0.3 ${PIPEWIRE_VERSION}")

include(WineDLL)

add_wine_dll(
    NAME      pipeasio64
    SPEC      ${CMAKE_SOURCE_DIR}/pipeasio.dll.spec
    SOURCES   src/asio.c
              src/audio.c
              src/config.c
              src/main.c
              src/regsvr.c
    INCLUDES  ${CMAKE_SOURCE_DIR}/include
              ${PIPEWIRE_INCLUDE_DIRS}
    LIBS      odbc32 ole32 uuid winmm user32 pipewire-0.3
)

include(GNUInstallDirs)

option(BUILD_WOW64_32 "Build the experimental WoW64 32-bit PE front end" ON)
if(BUILD_WOW64_32)
    find_program(I686_W64_MINGW_GCC i686-w64-mingw32-gcc)
    find_program(X86_64_W64_MINGW_GCC x86_64-w64-mingw32-gcc)
    if(I686_W64_MINGW_GCC AND X86_64_W64_MINGW_GCC)
        set(PIPEASIO32_DLL "${CMAKE_BINARY_DIR}/pipeasio32.dll")
        set(PIPEASIO64_HELPER "${CMAKE_BINARY_DIR}/pipeasio64-helper.exe")

        add_custom_command(
            OUTPUT  "${PIPEASIO32_DLL}"
            COMMAND "${I686_W64_MINGW_GCC}" -shared
                    "${CMAKE_SOURCE_DIR}/src/wow64/pipeasio32_bridge.c"
                    "${CMAKE_SOURCE_DIR}/src/wow64/pipeasio32.def"
                    -O2 -Wall -Wextra
                    -o "${PIPEASIO32_DLL}"
                    -lole32 -luuid
            DEPENDS "${CMAKE_SOURCE_DIR}/src/wow64/pipeasio32_bridge.c"
                    "${CMAKE_SOURCE_DIR}/src/wow64/pipeasio32.def"
                    "${CMAKE_SOURCE_DIR}/src/wow64/pipeasio_wow64_ipc.h"
            VERBATIM
            COMMENT "mingw pipeasio32.dll (WoW64 PE front end)")

        add_custom_command(
            OUTPUT  "${PIPEASIO64_HELPER}"
            COMMAND "${X86_64_W64_MINGW_GCC}" -static -static-libgcc
                    "${CMAKE_SOURCE_DIR}/src/wow64/pipeasio64_metadata_helper.c"
                    -O2 -Wall -Wextra
                    -mwindows
                    -o "${PIPEASIO64_HELPER}"
                    -lole32 -luuid
            DEPENDS "${CMAKE_SOURCE_DIR}/src/wow64/pipeasio64_metadata_helper.c"
                    "${CMAKE_SOURCE_DIR}/src/wow64/pipeasio_wow64_ipc.h"
            VERBATIM
            COMMENT "mingw pipeasio64-helper.exe (WoW64 metadata helper)")

        add_custom_target(pipeasio32 ALL DEPENDS "${PIPEASIO32_DLL}")
        add_custom_target(pipeasio64-helper ALL DEPENDS "${PIPEASIO64_HELPER}")

        install(FILES "${PIPEASIO32_DLL}"
                DESTINATION lib/wine/i386-windows)
        install(FILES "${PIPEASIO64_HELPER}"
                DESTINATION lib/wine/x86_64-windows)
    else()
        message(WARNING "mingw-w64 i686/x86_64 compilers not found - skipping "
                        "experimental WoW64 32-bit front end.")
    endif()
endif()

install(PROGRAMS ${CMAKE_SOURCE_DIR}/pipeasio-register
        DESTINATION ${CMAKE_INSTALL_BINDIR})

# Native Qt6 settings panel (pipeasio-settings), run from the user's Linux host
# terminal.  The in-Wine ControlPanel button only shows a message pointing here,
# since the Qt panel can't run inside the Wine/Proton container.  Optional and
# self-contained: the core Wine driver builds fine without it.  Skips gracefully
# if no C++ compiler or Qt6 Widgets is present.
option(BUILD_SETTINGS_PANEL "Build the Qt6 settings panel" ON)
if(BUILD_SETTINGS_PANEL)
    include(CheckLanguage)
    check_language(CXX)
    if(CMAKE_CXX_COMPILER)
        enable_language(CXX)
        set(CMAKE_CXX_STANDARD 17)
        find_package(Qt6 QUIET COMPONENTS Widgets)
        if(Qt6_FOUND)
            add_subdirectory(gui)
        else()
            message(WARNING "Qt6 Widgets not found — skipping the settings panel "
                            "(set -DBUILD_SETTINGS_PANEL=OFF to silence).")
        endif()
    else()
        message(WARNING "No C++ compiler — skipping the settings panel.")
    endif()
endif()

# Test rig: standalone Wine probe host + Linux-native unit tests.
option(BUILD_TESTS "Build the asio_probe Wine host and unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/asio_probe)
    add_subdirectory(tests/asio_loopback)
    add_subdirectory(tests/unit)
    add_subdirectory(tests/pw_probe)
    add_subdirectory(tests/wow64)
    if(BUILD_SETTINGS_PANEL AND Qt6_FOUND)
        add_subdirectory(tests/gui)
    endif()
endif()
