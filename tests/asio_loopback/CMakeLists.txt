# Wine PE loopback analyzer: plays a frame counter out of PipeASIO's
# output ports and verifies it on the inputs after an external PipeWire
# loopback (see run.sh).  Checks bit-exactness, sample continuity,
# channel mapping, and measured vs reported round-trip latency.

find_program(WINEGCC winegcc REQUIRED)
find_program(I686_W64_MINGW_GCC i686-w64-mingw32-gcc)
find_program(FILE_EXECUTABLE file)
find_program(BASH_EXECUTABLE bash)

set(LOOP_SRC  "${CMAKE_CURRENT_SOURCE_DIR}/asio_loopback.c")
set(LOOP_OUT  "${CMAKE_CURRENT_BINARY_DIR}/asio_loopback.exe.so")

add_custom_command(
    OUTPUT  "${LOOP_OUT}"
    COMMAND ${WINEGCC} -m64 -mno-cygwin -mconsole
                       -O0 -g -Wall -Wextra
                       "${LOOP_SRC}"
                       -lole32
                       -o "${LOOP_OUT}"
    DEPENDS "${LOOP_SRC}"
    COMMENT "winegcc asio_loopback (Wine PE)"
    VERBATIM
)

add_custom_target(asio_loopback ALL DEPENDS "${LOOP_OUT}")

if(I686_W64_MINGW_GCC)
    set(LOOP32_OUT "${CMAKE_CURRENT_BINARY_DIR}/asio_loopback32.exe")
    add_custom_command(
        OUTPUT  "${LOOP32_OUT}"
        COMMAND "${I686_W64_MINGW_GCC}" -mconsole
                         -O0 -g -Wall -Wextra
                         "${LOOP_SRC}"
                         -lole32
                         -o "${LOOP32_OUT}"
        DEPENDS "${LOOP_SRC}"
        COMMENT "mingw asio_loopback32 (PE32)"
        VERBATIM
    )
    add_custom_target(asio_loopback32 ALL DEPENDS "${LOOP32_OUT}")

    if(FILE_EXECUTABLE AND BASH_EXECUTABLE)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/check_loopback32_artifact.sh.in"
            "${CMAKE_CURRENT_BINARY_DIR}/check_loopback32_artifact.sh"
            @ONLY
        )
        add_test(NAME asio_loopback32_artifact
                 COMMAND "${BASH_EXECUTABLE}"
                         "${CMAKE_CURRENT_BINARY_DIR}/check_loopback32_artifact.sh")
        set_tests_properties(asio_loopback32_artifact PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            TIMEOUT 20
            LABELS "wow64;abi")
    endif()
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/run.sh"
    "${CMAKE_CURRENT_BINARY_DIR}/run.sh"
    @ONLY
)
