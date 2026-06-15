# Wine PE loopback analyzer: plays a frame counter out of PipeASIO's
# output ports and verifies it on the inputs after an external PipeWire
# loopback (see run.sh).  Checks bit-exactness, sample continuity,
# channel mapping, and measured vs reported round-trip latency.

find_program(WINEGCC winegcc REQUIRED)
find_program(I686_W64_MINGW_GCC i686-w64-mingw32-gcc)

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
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/run.sh"
    "${CMAKE_CURRENT_BINARY_DIR}/run.sh"
    @ONLY
)
