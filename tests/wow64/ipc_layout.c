#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "pipeasio_wow64_ipc.h"

#ifndef EXPECTED_POINTER_SIZE
#error EXPECTED_POINTER_SIZE must be defined by the test runner
#endif

#define CHECK(expr, msg) _Static_assert((expr), msg)

CHECK(sizeof(void *) == EXPECTED_POINTER_SIZE, "unexpected compiler bitness");

CHECK(PIPEASIO_WOW64_MAGIC == 0x57503634UL, "magic changed");
CHECK(PIPEASIO_WOW64_VERSION == 1, "version changed");
CHECK(PIPEASIO_WOW64_MAX_CHANNELS == 64, "channel limit changed");
CHECK(PIPEASIO_WOW64_MAX_BUFFER_FRAMES == 8192, "buffer frame limit changed");

CHECK(PIPEASIO_WOW64_CMD_INIT == 1, "command enum drift");
CHECK(PIPEASIO_WOW64_CMD_EXIT == 6, "command enum drift");
CHECK(PIPEASIO_WOW64_CB_TIME_INFO == 2, "callback enum drift");
CHECK(PIPEASIO_WOW64_CB_NOTIFICATION == 4, "callback enum drift");

CHECK(sizeof(pipeasio_wow64_i64) == 8, "i64 layout changed");
CHECK(offsetof(pipeasio_wow64_i64, hi) == 0, "i64 hi offset changed");
CHECK(offsetof(pipeasio_wow64_i64, lo) == 4, "i64 lo offset changed");

CHECK(sizeof(pipeasio_wow64_buffer_desc) == 12, "buffer descriptor size changed");
CHECK(offsetof(pipeasio_wow64_buffer_desc, is_input) == 0,
      "buffer descriptor is_input offset changed");
CHECK(offsetof(pipeasio_wow64_buffer_desc, channel) == 4,
      "buffer descriptor channel offset changed");
CHECK(offsetof(pipeasio_wow64_buffer_desc, sample_offset) == 8,
      "buffer descriptor sample_offset changed");

CHECK(offsetof(pipeasio_wow64_shared, magic) == 0, "magic offset changed");
CHECK(offsetof(pipeasio_wow64_shared, command) == 8, "command offset changed");
CHECK(offsetof(pipeasio_wow64_shared, inputs) == 24, "inputs offset changed");
CHECK(offsetof(pipeasio_wow64_shared, sample_rate) == 56, "sample_rate offset changed");
CHECK(offsetof(pipeasio_wow64_shared, buffers) == 72, "buffers offset changed");
CHECK(offsetof(pipeasio_wow64_shared, callback_kind) == 840, "callback offset changed");
CHECK(offsetof(pipeasio_wow64_shared, callback_sample_rate) == 872,
      "callback_sample_rate offset changed");
CHECK(offsetof(pipeasio_wow64_shared, callback_sample_position) == 880,
      "callback_sample_position offset changed");
CHECK(offsetof(pipeasio_wow64_shared, sample_position) == 900,
      "sample_position offset changed");
CHECK(offsetof(pipeasio_wow64_shared, error) == 916, "error offset changed");
CHECK(offsetof(pipeasio_wow64_shared, samples) == 1172, "samples offset changed");
CHECK(sizeof(pipeasio_wow64_shared) == 4195476, "shared layout size changed");

CHECK(((0 * 2 + 0) * PIPEASIO_WOW64_MAX_BUFFER_FRAMES) == 0, "buffer offset changed");
CHECK(((0 * 2 + 1) * PIPEASIO_WOW64_MAX_BUFFER_FRAMES) == 8192, "buffer offset changed");
CHECK(((1 * 2 + 0) * PIPEASIO_WOW64_MAX_BUFFER_FRAMES) == 16384, "buffer offset changed");

ULONG
pipeasio_wow64_ipc_layout_probe(LONG slot, LONG half)
{
    return pipeasio_wow64_buffer_offset(slot, half);
}
