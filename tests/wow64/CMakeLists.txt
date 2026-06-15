# Build-contract checks for the experimental WoW64 front end.  Runtime
# PE32 execution stays in the external GitLab gate because macOS Docker is
# not reliable enough for that path.

if(NOT TARGET pipeasio32 OR NOT TARGET pipeasio32_unix)
    message(STATUS "WoW64 artifacts not available - skipping tests/wow64")
    return()
endif()

find_program(FILE_EXECUTABLE file)
find_program(BASH_EXECUTABLE bash)
find_program(I686_W64_MINGW_GCC i686-w64-mingw32-gcc)
find_program(I686_W64_MINGW_OBJDUMP i686-w64-mingw32-objdump)
find_program(NM_EXECUTABLE nm)

if(BASH_EXECUTABLE AND I686_W64_MINGW_GCC)
    set(WOW64_HOST_INCLUDE_FLAGS
        -I "${CMAKE_SOURCE_DIR}/src/wow64")
    foreach(_wine_include ${WINE_INCLUDE_DIRS})
        list(APPEND WOW64_HOST_INCLUDE_FLAGS -I "${_wine_include}")
    endforeach()
    string(REPLACE ";" " " WOW64_HOST_INCLUDE_FLAGS "${WOW64_HOST_INCLUDE_FLAGS}")

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/check_unix_abi_layout.sh.in"
        "${CMAKE_CURRENT_BINARY_DIR}/check_unix_abi_layout.sh"
        @ONLY
    )

    add_test(NAME wow64_unix_abi_layout
             COMMAND "${BASH_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/check_unix_abi_layout.sh")
    set_tests_properties(wow64_unix_abi_layout PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        TIMEOUT 120
        LABELS "wow64;abi")
else()
    message(WARNING "bash or i686 mingw compiler missing - skipping WoW64 ABI layout test.")
endif()

if(BASH_EXECUTABLE AND FILE_EXECUTABLE AND I686_W64_MINGW_OBJDUMP AND NM_EXECUTABLE)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/check_artifacts.sh.in"
        "${CMAKE_CURRENT_BINARY_DIR}/check_artifacts.sh"
        @ONLY
    )

    add_test(NAME wow64_artifacts
             COMMAND "${BASH_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/check_artifacts.sh")
    set_tests_properties(wow64_artifacts PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        TIMEOUT 20
        LABELS "wow64;abi")
else()
    message(WARNING "bash, file, i686 objdump, or nm missing - skipping WoW64 artifact test.")
endif()
