#ifndef PIPEASIO_WOW64_UNIX_H
#define PIPEASIO_WOW64_UNIX_H

#include <windef.h>

#define PIPEASIO_WOW64_VERSION 2
#define PIPEASIO_WOW64_MAX_CHANNELS 256
#define PIPEASIO_WOW64_MAX_BUFFER_FRAMES 8192
#define PIPEASIO_WOW64_ERROR_MAX 256
#define PIPEASIO_WOW64_DRIVER_VERSION 92

typedef UINT32 pipeasio_wow64_ptr32;

#include <pshpack4.h>

typedef struct pipeasio_wow64_i64
{
    ULONG hi;
    ULONG lo;
} pipeasio_wow64_i64;

typedef struct pipeasio_wow64_metadata
{
    LONG               inputs;
    LONG               outputs;
    LONG               min_buffer;
    LONG               max_buffer;
    LONG               preferred_buffer;
    LONG               granularity;
    LONG               input_latency;
    LONG               output_latency;
    double             sample_rate;
    pipeasio_wow64_i64 sample_position;
    pipeasio_wow64_i64 time_stamp;
} pipeasio_wow64_metadata;

typedef struct pipeasio_wow64_buffer_desc
{
    LONG is_input;
    LONG channel;
} pipeasio_wow64_buffer_desc;

enum pipeasio_wow64_unix_call
{
    PIPEASIO_WOW64_CALL_INIT = 0,
    PIPEASIO_WOW64_CALL_CREATE_BUFFERS,
    PIPEASIO_WOW64_CALL_START,
    PIPEASIO_WOW64_CALL_STOP,
    PIPEASIO_WOW64_CALL_DISPOSE_BUFFERS,
    PIPEASIO_WOW64_CALL_GET_SAMPLE_POSITION,
    PIPEASIO_WOW64_CALL_CLOSE,
    PIPEASIO_WOW64_CALL_WAIT_CALLBACK,
    PIPEASIO_WOW64_CALL_REPLY_CALLBACK,
    PIPEASIO_WOW64_CALL_COUNT
};

enum pipeasio_wow64_callback_kind
{
    PIPEASIO_WOW64_CB_NONE = 0,
    PIPEASIO_WOW64_CB_BUFFER_SWITCH,
    PIPEASIO_WOW64_CB_TIME_INFO,
    PIPEASIO_WOW64_CB_SAMPLE_RATE,
    PIPEASIO_WOW64_CB_NOTIFICATION,
};

typedef struct pipeasio_wow64_init_params
{
    UINT32                    version;
    UINT64                    backend;
    LONG                      asio_status;
    pipeasio_wow64_metadata   metadata;
    char                      error[PIPEASIO_WOW64_ERROR_MAX];
} pipeasio_wow64_init_params;

typedef struct pipeasio_wow64_create_buffers_params
{
    UINT32                     version;
    UINT64                     backend;
    pipeasio_wow64_ptr32       pe_iface;
    pipeasio_wow64_ptr32       buffer_base;
    UINT32                     buffer_bytes;
    LONG                       time_info_mode;
    LONG                       num_channels;
    LONG                       buffer_size;
    LONG                       asio_status;
    pipeasio_wow64_metadata    metadata;
    pipeasio_wow64_buffer_desc buffers[PIPEASIO_WOW64_MAX_CHANNELS];
    char                       error[PIPEASIO_WOW64_ERROR_MAX];
} pipeasio_wow64_create_buffers_params;

typedef struct pipeasio_wow64_simple_params
{
    UINT32                  version;
    UINT64                  backend;
    LONG                    asio_status;
    pipeasio_wow64_metadata metadata;
    char                    error[PIPEASIO_WOW64_ERROR_MAX];
} pipeasio_wow64_simple_params;

typedef struct pipeasio_wow64_position_params
{
    UINT32             version;
    UINT64             backend;
    LONG               asio_status;
    pipeasio_wow64_i64 sample_position;
    pipeasio_wow64_i64 time_stamp;
    char               error[PIPEASIO_WOW64_ERROR_MAX];
} pipeasio_wow64_position_params;

typedef struct pipeasio_wow64_callback_params
{
    LONG                           kind;
    LONG                           index;
    LONG                           direct;
    LONG                           selector;
    LONG                           value;
    double                         sample_rate;
    pipeasio_wow64_i64             sample_position;
    pipeasio_wow64_i64             time_stamp;
    ULONG                          time_flags;
    LONG                           result;
} pipeasio_wow64_callback_params;

typedef struct pipeasio_wow64_callback_wait_params
{
    UINT32                         version;
    UINT64                         backend;
    LONG                           shutdown;
    pipeasio_wow64_callback_params callback;
    char                           error[PIPEASIO_WOW64_ERROR_MAX];
} pipeasio_wow64_callback_wait_params;

typedef struct pipeasio_wow64_callback_reply_params
{
    UINT32 version;
    UINT64 backend;
    LONG   result;
    char   error[PIPEASIO_WOW64_ERROR_MAX];
} pipeasio_wow64_callback_reply_params;

#include <poppack.h>

#endif /* PIPEASIO_WOW64_UNIX_H */
