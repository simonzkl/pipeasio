#ifndef PIPEASIO_WOW64_IPC_H
#define PIPEASIO_WOW64_IPC_H

#define PIPEASIO_WOW64_MAGIC 0x57503634UL /* WP64 */
#define PIPEASIO_WOW64_VERSION 1
#define PIPEASIO_WOW64_MAX_CHANNELS 64
#define PIPEASIO_WOW64_MAX_BUFFER_FRAMES 8192
#define PIPEASIO_WOW64_NAME_MAX 64

enum pipeasio_wow64_command
{
    PIPEASIO_WOW64_CMD_NONE = 0,
    PIPEASIO_WOW64_CMD_INIT,
    PIPEASIO_WOW64_CMD_CREATE_BUFFERS,
    PIPEASIO_WOW64_CMD_START,
    PIPEASIO_WOW64_CMD_STOP,
    PIPEASIO_WOW64_CMD_DISPOSE_BUFFERS,
    PIPEASIO_WOW64_CMD_EXIT,
};

enum pipeasio_wow64_callback
{
    PIPEASIO_WOW64_CB_NONE = 0,
    PIPEASIO_WOW64_CB_BUFFER_SWITCH,
    PIPEASIO_WOW64_CB_TIME_INFO,
    PIPEASIO_WOW64_CB_SAMPLE_RATE,
    PIPEASIO_WOW64_CB_NOTIFICATION,
};

typedef struct pipeasio_wow64_i64
{
    ULONG hi;
    ULONG lo;
} pipeasio_wow64_i64;

typedef struct pipeasio_wow64_buffer_desc
{
    LONG  is_input;
    LONG  channel;
    ULONG sample_offset;
} pipeasio_wow64_buffer_desc;

#pragma pack(push, 4)
typedef struct pipeasio_wow64_shared
{
    ULONG magic;
    ULONG version;

    volatile LONG command;
    volatile LONG command_generation;
    volatile LONG command_done_generation;
    LONG          command_status;

    LONG inputs;
    LONG outputs;
    LONG min_buffer;
    LONG max_buffer;
    LONG preferred_buffer;
    LONG granularity;
    LONG input_latency;
    LONG output_latency;
    double sample_rate;

    LONG num_channels;
    LONG buffer_size;
    pipeasio_wow64_buffer_desc buffers[PIPEASIO_WOW64_MAX_CHANNELS];

    volatile LONG callback_kind;
    volatile LONG callback_generation;
    volatile LONG callback_done_generation;
    LONG          callback_index;
    LONG          callback_direct;
    LONG          callback_selector;
    LONG          callback_value;
    LONG          callback_result;
    double        callback_sample_rate;
    pipeasio_wow64_i64 callback_sample_position;
    pipeasio_wow64_i64 callback_time_stamp;
    ULONG             callback_time_flags;

    pipeasio_wow64_i64 sample_position;
    pipeasio_wow64_i64 time_stamp;

    char error[256];
    float samples[PIPEASIO_WOW64_MAX_CHANNELS * 2 * PIPEASIO_WOW64_MAX_BUFFER_FRAMES];
} pipeasio_wow64_shared;
#pragma pack(pop)

static inline ULONG
pipeasio_wow64_buffer_offset(LONG slot, LONG half)
{
    return (ULONG)((slot * 2 + half) * PIPEASIO_WOW64_MAX_BUFFER_FRAMES);
}

#endif
