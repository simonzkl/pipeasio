#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "pipeasio_wow64_unix.h"

#ifndef EXPECTED_POINTER_SIZE
#error EXPECTED_POINTER_SIZE must be defined by the test runner
#endif

#define CHECK(expr, msg) _Static_assert((expr), msg)

CHECK(sizeof(void *) == EXPECTED_POINTER_SIZE, "unexpected compiler bitness");

CHECK(PIPEASIO_WOW64_VERSION == 2, "version changed");
CHECK(PIPEASIO_WOW64_MAX_CHANNELS == 256, "channel limit changed");
CHECK(PIPEASIO_WOW64_MAX_BUFFER_FRAMES == 8192, "buffer frame limit changed");

CHECK(PIPEASIO_WOW64_CALL_INIT == 0, "unix call enum drift");
CHECK(PIPEASIO_WOW64_CALL_CLOSE == 6, "unix call enum drift");
CHECK(PIPEASIO_WOW64_CALL_WAIT_CALLBACK == 7, "unix call enum drift");
CHECK(PIPEASIO_WOW64_CALL_REPLY_CALLBACK == 8, "unix call enum drift");
CHECK(PIPEASIO_WOW64_CALL_COUNT == 9, "unix call count drift");
CHECK(PIPEASIO_WOW64_CB_TIME_INFO == 2, "callback enum drift");
CHECK(PIPEASIO_WOW64_CB_NOTIFICATION == 4, "callback enum drift");

CHECK(sizeof(pipeasio_wow64_i64) == 8, "i64 layout changed");
CHECK(offsetof(pipeasio_wow64_i64, hi) == 0, "i64 hi offset changed");
CHECK(offsetof(pipeasio_wow64_i64, lo) == 4, "i64 lo offset changed");

CHECK(sizeof(pipeasio_wow64_metadata) == 56, "metadata size changed");
CHECK(offsetof(pipeasio_wow64_metadata, inputs) == 0, "metadata inputs offset changed");
CHECK(offsetof(pipeasio_wow64_metadata, sample_rate) == 32,
      "metadata sample_rate offset changed");
CHECK(offsetof(pipeasio_wow64_metadata, sample_position) == 40,
      "metadata sample_position offset changed");
CHECK(offsetof(pipeasio_wow64_metadata, time_stamp) == 48,
      "metadata time_stamp offset changed");

CHECK(sizeof(pipeasio_wow64_buffer_desc) == 8, "buffer descriptor size changed");
CHECK(offsetof(pipeasio_wow64_buffer_desc, is_input) == 0,
      "buffer descriptor is_input offset changed");
CHECK(offsetof(pipeasio_wow64_buffer_desc, channel) == 4,
      "buffer descriptor channel offset changed");

CHECK(sizeof(pipeasio_wow64_init_params) == 328, "init params size changed");
CHECK(offsetof(pipeasio_wow64_init_params, backend) == 4, "init backend offset changed");
CHECK(offsetof(pipeasio_wow64_init_params, metadata) == 16, "init metadata offset changed");
CHECK(offsetof(pipeasio_wow64_init_params, error) == 72, "init error offset changed");

CHECK(sizeof(pipeasio_wow64_create_buffers_params) == 2400,
      "create-buffers params size changed");
CHECK(offsetof(pipeasio_wow64_create_buffers_params, backend) == 4,
      "create backend offset changed");
CHECK(offsetof(pipeasio_wow64_create_buffers_params, metadata) == 40,
      "create metadata offset changed");
CHECK(offsetof(pipeasio_wow64_create_buffers_params, buffers) == 96,
      "create buffers offset changed");
CHECK(offsetof(pipeasio_wow64_create_buffers_params, error) == 2144,
      "create error offset changed");

CHECK(sizeof(pipeasio_wow64_simple_params) == 328, "simple params size changed");
CHECK(offsetof(pipeasio_wow64_simple_params, backend) == 4, "simple backend offset changed");
CHECK(offsetof(pipeasio_wow64_simple_params, metadata) == 16, "simple metadata offset changed");
CHECK(offsetof(pipeasio_wow64_simple_params, error) == 72, "simple error offset changed");

CHECK(sizeof(pipeasio_wow64_position_params) == 288, "position params size changed");
CHECK(offsetof(pipeasio_wow64_position_params, sample_position) == 16,
      "position sample_position offset changed");
CHECK(offsetof(pipeasio_wow64_position_params, error) == 32, "position error offset changed");

CHECK(sizeof(pipeasio_wow64_callback_params) == 52, "callback params size changed");
CHECK(offsetof(pipeasio_wow64_callback_params, sample_rate) == 20,
      "callback sample_rate offset changed");
CHECK(offsetof(pipeasio_wow64_callback_params, sample_position) == 28,
      "callback sample_position offset changed");
CHECK(offsetof(pipeasio_wow64_callback_params, result) == 48,
      "callback result offset changed");

CHECK(sizeof(pipeasio_wow64_callback_wait_params) == 324,
      "callback wait params size changed");
CHECK(offsetof(pipeasio_wow64_callback_wait_params, callback) == 16,
      "callback wait callback offset changed");
CHECK(offsetof(pipeasio_wow64_callback_wait_params, error) == 68,
      "callback wait error offset changed");

CHECK(sizeof(pipeasio_wow64_callback_reply_params) == 272,
      "callback reply params size changed");
CHECK(offsetof(pipeasio_wow64_callback_reply_params, result) == 12,
      "callback reply result offset changed");
CHECK(offsetof(pipeasio_wow64_callback_reply_params, error) == 16,
      "callback reply error offset changed");

int
pipeasio_wow64_unix_abi_probe(void)
{
    return 0;
}
