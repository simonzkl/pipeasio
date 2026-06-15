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

option(BUILD_WOW64_32 "Build the experimental WoW64 32-bit PE front end" OFF)
if(BUILD_WOW64_32)
    find_program(I686_W64_MINGW_GCC i686-w64-mingw32-gcc)
    find_program(I686_W64_MINGW_RANLIB i686-w64-mingw32-ranlib)
    set(WINE_LIB_ROOT "" CACHE PATH "Wine lib/wine directory containing *-windows and *-unix import libraries")
    if(I686_W64_MINGW_GCC AND I686_W64_MINGW_RANLIB AND WINE_LIB_ROOT)
        set(PIPEASIO32_DLL "${CMAKE_BINARY_DIR}/pipeasio32.dll")
        set(PIPEASIO32_UNIX "${CMAKE_BINARY_DIR}/pipeasio32.so")
        set(PIPEASIO32_UNIX_SPEC "${CMAKE_SOURCE_DIR}/src/wow64/pipeasio32_unixlib.spec")
        set(WINECRT0_I386_SRC "${WINE_LIB_ROOT}/i386-windows/libwinecrt0.a")
        set(WINECRT0_I386 "${CMAKE_BINARY_DIR}/libwinecrt0-i386.a")
        set(WINENTDLL_I386_SRC "${WINE_LIB_ROOT}/i386-windows/libntdll.a")
        set(WINENTDLL_I386 "${CMAKE_BINARY_DIR}/libntdll-i386.a")
        set(WINEOLE32_I386_SRC "${WINE_LIB_ROOT}/i386-windows/libole32.a")
        set(WINEOLE32_I386 "${CMAKE_BINARY_DIR}/libole32-i386.a")
        set(WINEUUID_I386_SRC "${WINE_LIB_ROOT}/i386-windows/libuuid.a")
        set(WINEUUID_I386 "${CMAKE_BINARY_DIR}/libuuid-i386.a")
        set(PIPEASIO32_INCLUDE_FLAGS
            -I "${CMAKE_SOURCE_DIR}/include"
            -I "${CMAKE_SOURCE_DIR}/src/wow64")
        foreach(_wine_inc ${WINE_INCLUDE_DIRS})
            list(APPEND PIPEASIO32_INCLUDE_FLAGS -I "${_wine_inc}")
        endforeach()
        set(PIPEASIO32_MINGW_CFLAGS
            -Wall -Wextra
            -Werror=implicit-function-declaration
            $<$<CONFIG:Release>:-O2>
            $<$<CONFIG:Release>:-DNDEBUG>
            $<$<CONFIG:RelWithDebInfo>:-O2>
            $<$<CONFIG:RelWithDebInfo>:-g>
            $<$<CONFIG:RelWithDebInfo>:-DNDEBUG>
            $<$<CONFIG:Debug>:-O0>
            $<$<CONFIG:Debug>:-g3>
            $<$<CONFIG:Debug>:-DDEBUG>
            $<$<CONFIG:Debug>:-fno-omit-frame-pointer>)

        add_custom_command(
            OUTPUT  "${WINECRT0_I386}" "${WINENTDLL_I386}" "${WINEOLE32_I386}" "${WINEUUID_I386}"
            COMMAND ${CMAKE_COMMAND} -E copy "${WINECRT0_I386_SRC}" "${WINECRT0_I386}"
            COMMAND chmod u+w "${WINECRT0_I386}"
            COMMAND "${I686_W64_MINGW_RANLIB}" "${WINECRT0_I386}"
            COMMAND ${CMAKE_COMMAND} -E copy "${WINENTDLL_I386_SRC}" "${WINENTDLL_I386}"
            COMMAND chmod u+w "${WINENTDLL_I386}"
            COMMAND "${I686_W64_MINGW_RANLIB}" "${WINENTDLL_I386}"
            COMMAND ${CMAKE_COMMAND} -E copy "${WINEOLE32_I386_SRC}" "${WINEOLE32_I386}"
            COMMAND chmod u+w "${WINEOLE32_I386}"
            COMMAND "${I686_W64_MINGW_RANLIB}" "${WINEOLE32_I386}"
            COMMAND ${CMAKE_COMMAND} -E copy "${WINEUUID_I386_SRC}" "${WINEUUID_I386}"
            COMMAND chmod u+w "${WINEUUID_I386}"
            COMMAND "${I686_W64_MINGW_RANLIB}" "${WINEUUID_I386}"
            DEPENDS "${WINECRT0_I386_SRC}" "${WINENTDLL_I386_SRC}"
                    "${WINEOLE32_I386_SRC}" "${WINEUUID_I386_SRC}"
            VERBATIM
            COMMENT "index i386 Wine import libraries")

        add_custom_command(
            OUTPUT  "${PIPEASIO32_DLL}"
            COMMAND "${I686_W64_MINGW_GCC}" -shared
                    "${CMAKE_SOURCE_DIR}/src/wow64/pipeasio32_thunk.c"
                    "${CMAKE_SOURCE_DIR}/src/wow64/pipeasio32.def"
                    "${WINECRT0_I386}"
                    ${PIPEASIO32_INCLUDE_FLAGS}
                    ${PIPEASIO32_MINGW_CFLAGS}
                    -o "${PIPEASIO32_DLL}"
                    "${WINENTDLL_I386}" "${WINEOLE32_I386}" "${WINEUUID_I386}"
            COMMAND "${WINEBUILD}" -m32 --builtin "${PIPEASIO32_DLL}"
            DEPENDS "${CMAKE_SOURCE_DIR}/src/wow64/pipeasio32_thunk.c"
                    "${CMAKE_SOURCE_DIR}/src/wow64/pipeasio32.def"
                    "${CMAKE_SOURCE_DIR}/include/pipeasio_offsets.h"
                    "${CMAKE_SOURCE_DIR}/src/wow64/pipeasio_wow64_unix.h"
                    "${WINECRT0_I386}" "${WINENTDLL_I386}"
                    "${WINEOLE32_I386}" "${WINEUUID_I386}"
            VERBATIM
            COMMAND_EXPAND_LISTS
            COMMENT "mingw pipeasio32.dll (WoW64 Wine builtin unixlib front end)")

        add_library(pipeasio32_unix_objs OBJECT
            src/wow64/pipeasio32_unixlib.c
            src/audio.c
            src/config.c)
        set_target_properties(pipeasio32_unix_objs PROPERTIES
            POSITION_INDEPENDENT_CODE ON)
        target_include_directories(pipeasio32_unix_objs PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src/wow64
            ${PIPEWIRE_INCLUDE_DIRS}
            ${WINE_INCLUDE_DIRS})
        target_compile_options(pipeasio32_unix_objs PRIVATE
            -DPIPEASIO_AUDIO_UNIXLIB
            -D_REENTRANT
            -Wall -pipe
            -fno-strict-aliasing
            -Wwrite-strings
            -Wpointer-arith
            -Werror=implicit-function-declaration
            $<$<CONFIG:Release>:-O2 -DNDEBUG -fvisibility=hidden>
            $<$<CONFIG:RelWithDebInfo>:-O2 -g -DNDEBUG -fvisibility=hidden>
            $<$<CONFIG:Debug>:-O0 -g3 -DDEBUG -fno-omit-frame-pointer>)

        add_custom_command(
            OUTPUT  "${PIPEASIO32_UNIX}"
            COMMAND "${WINEGCC}" -shared
                    "${PIPEASIO32_UNIX_SPEC}"
                    $<TARGET_OBJECTS:pipeasio32_unix_objs>
                    -lpthread -ldl
                    ${PIPEWIRE_LDFLAGS}
                    -o "${PIPEASIO32_UNIX}"
            DEPENDS pipeasio32_unix_objs "${PIPEASIO32_UNIX_SPEC}"
                    $<TARGET_OBJECTS:pipeasio32_unix_objs>
            VERBATIM
            COMMAND_EXPAND_LISTS
            COMMENT "winegcc pipeasio32.so (WoW64 unixlib)")
        add_custom_target(pipeasio32_unix ALL DEPENDS "${PIPEASIO32_UNIX}")

        add_custom_target(pipeasio32 ALL DEPENDS "${PIPEASIO32_DLL}")
        add_dependencies(pipeasio32 pipeasio32_unix)

        install(FILES "${PIPEASIO32_DLL}"
                DESTINATION lib/wine/i386-windows)
        install(FILES "${PIPEASIO32_UNIX}"
                DESTINATION lib/wine/x86_64-unix
                PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                            GROUP_READ GROUP_EXECUTE
                            WORLD_READ WORLD_EXECUTE)
    else()
        message(FATAL_ERROR "BUILD_WOW64_32=ON requires i686-w64-mingw32-gcc, "
                            "i686-w64-mingw32-ranlib, and -DWINE_LIB_ROOT=<lib/wine>.")
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
