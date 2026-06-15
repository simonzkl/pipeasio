#define WINE_UNIX_LIB
#define WIN32_LEAN_AND_MEAN

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <unixlib.h>

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "pipeasio_config.h"
#include "pipeasio_offsets.h"
#include "pipeasio_wow64_unix.h"

#define PIPEASIO_MAX_NAME_LENGTH 32
#define PIPEASIO_CALLBACK_REPLY_TIMEOUT_MS 2000

typedef struct wow64_channel
{
    audio_sample_t *audio_buffer;
    char            port_name[PIPEASIO_MAX_NAME_LENGTH];
    audio_port_t   *port;
    bool            active;
} wow64_channel;

typedef struct backend_state
{
    audio_client_t *audio_client;
    wow64_channel  *input_channel;
    wow64_channel  *output_channel;
    const char    **phys_input_ports;
    const char    **phys_output_ports;

    LONG pipeasio_number_inputs;
    LONG pipeasio_number_outputs;
    bool pipeasio_connect_to_hardware;
    bool pipeasio_fixed_buffersize;
    bool pipeasio_follow_device_clock;
    LONG pipeasio_preferred_buffersize;
    int  pipeasio_sample_rate;
    char pipeasio_output_device[PIPEASIO_DEVICE_NAME_MAX];
    char pipeasio_input_device[PIPEASIO_DEVICE_NAME_MAX];
    char client_name[PIPEASIO_MAX_NAME_LENGTH];

    LONG host_current_buffersize;
    LONG host_last_reset_quantum;
    _Atomic LONG host_driver_state;
    _Atomic bool host_buffer_index;
    bool host_time_info_mode;
    double host_sample_rate;
    pipeasio_wow64_i64 host_num_samples;
    pipeasio_wow64_i64 host_time_stamp;

    UINT32 pe_iface;
    audio_sample_t *callback_audio_buffer;
    UINT32 callback_audio_buffer_bytes;
    pthread_mutex_t callback_producer_mutex;
    pthread_mutex_t callback_mutex;
    pthread_cond_t  callback_ready;
    pthread_cond_t  callback_done;
    bool            callback_sync_initialized;
    bool            callback_pending;
    bool            callback_reply_ready;
    bool            callback_shutdown;
    LONG            callback_result;
    pipeasio_wow64_callback_params callback_request;

    char error[PIPEASIO_WOW64_ERROR_MAX];
} backend_state;

enum
{
    BackendLoaded,
    BackendInitialized,
    BackendPrepared,
    BackendRunning
};

static _Atomic LONG g_follower_quantum;

static LONG
backend_state_get(const backend_state *b)
{
    return atomic_load_explicit(&b->host_driver_state, memory_order_acquire);
}

static void
backend_state_set(backend_state *b, LONG state)
{
    atomic_store_explicit(&b->host_driver_state, state, memory_order_release);
}

static bool
host_buffer_index_get(const backend_state *b)
{
    return atomic_load_explicit(&b->host_buffer_index, memory_order_relaxed);
}

static void
host_buffer_index_set(backend_state *b, bool index)
{
    atomic_store_explicit(&b->host_buffer_index, index, memory_order_relaxed);
}

static bool
debug_on(void)
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = getenv("PIPEASIO_DEBUG") ? 1 : 0;
    return enabled != 0;
}

static void
debug_log(const char *fmt, ...)
{
    char    buf[512];
    va_list ap;
    int     n;

    if (!debug_on())
        return;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0)
        (void)write(STDERR_FILENO, buf, (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1);
}

static backend_state *
backend_from_handle(UINT64 handle)
{
    return (backend_state *)(uintptr_t)handle;
}

static void
set_error(backend_state *b, const char *fmt, ...)
{
    va_list ap;

    if (!b)
        return;
    va_start(ap, fmt);
    vsnprintf(b->error, sizeof b->error, fmt, ap);
    va_end(ap);
}

static void
copy_backend_error(backend_state *b, char *dst, size_t dst_size)
{
    if (!dst || !dst_size)
        return;
    if (b && b->error[0])
        snprintf(dst, dst_size, "%s", b->error);
    else
        dst[0] = 0;
}

static pipeasio_wow64_i64
i64_from_u64(uint64_t value)
{
    pipeasio_wow64_i64 out;
    out.hi = (ULONG)(value >> 32);
    out.lo = (ULONG)(value & 0xffffffffu);
    return out;
}

static uint64_t
i64_to_u64(pipeasio_wow64_i64 value)
{
    return ((uint64_t)value.hi << 32) | value.lo;
}

static bool
env_bool(const char *name, bool fallback)
{
    const char *value = getenv(name);
    if (!value)
        return fallback;
    if (!strcasecmp(value, "on") || !strcasecmp(value, "true") || !strcmp(value, "1")
        || !strcasecmp(value, "yes"))
        return true;
    if (!strcasecmp(value, "off") || !strcasecmp(value, "false") || !strcmp(value, "0")
        || !strcasecmp(value, "no"))
        return false;
    return fallback;
}

static int
env_int(const char *name, int fallback)
{
    const char *value = getenv(name);
    char       *end;
    long        parsed;

    if (!value || !value[0])
        return fallback;
    errno  = 0;
    parsed = strtol(value, &end, 10);
    if (errno == ERANGE || end == value)
        return fallback;
    return (int)parsed;
}

static void
deadline_after_ms(struct timespec *deadline, long timeout_ms)
{
    if (clock_gettime(CLOCK_REALTIME, deadline))
    {
        deadline->tv_sec  = 0;
        deadline->tv_nsec = 0;
        return;
    }
    deadline->tv_sec += timeout_ms / 1000;
    deadline->tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L)
    {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000L;
    }
}

static void
copy_string(char *dst, size_t dst_size, const char *src)
{
    size_t i = 0;

    if (!dst_size)
        return;
    if (!src)
        src = "";
    for (; src[i] && i + 1 < dst_size; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

static void
copy_env_string(const char *name, char *dst, size_t dst_size)
{
    const char *value = getenv(name);

    if (value && value[0])
        copy_string(dst, dst_size, value);
}

static void
configure_backend(backend_state *b)
{
    struct pipeasio_config cfg;

    pipeasio_config_load(&cfg);

    host_buffer_index_set(b, false);
    b->host_current_buffersize = 0;
    b->host_last_reset_quantum = 0;
    backend_state_set(b, BackendLoaded);
    b->host_sample_rate        = 0.0;
    b->host_time_info_mode     = false;
    b->host_num_samples        = i64_from_u64(0);
    b->host_time_stamp         = i64_from_u64(0);

    b->pipeasio_number_inputs        = cfg.inputs;
    b->pipeasio_number_outputs       = cfg.outputs;
    b->pipeasio_connect_to_hardware  = cfg.auto_connect;
    b->pipeasio_fixed_buffersize     = cfg.fixed_buffer_size;
    b->pipeasio_follow_device_clock  = cfg.follow_device_clock;
    b->pipeasio_preferred_buffersize = cfg.buffer_size;
    b->pipeasio_sample_rate          = cfg.sample_rate;
    copy_string(b->pipeasio_output_device, sizeof b->pipeasio_output_device, cfg.output_device);
    copy_string(b->pipeasio_input_device, sizeof b->pipeasio_input_device, cfg.input_device);

    b->pipeasio_number_inputs
            = env_int("PIPEASIO_NUMBER_INPUTS", b->pipeasio_number_inputs);
    b->pipeasio_number_outputs
            = env_int("PIPEASIO_NUMBER_OUTPUTS", b->pipeasio_number_outputs);
    b->pipeasio_connect_to_hardware
            = env_bool("PIPEASIO_CONNECT_TO_HARDWARE", b->pipeasio_connect_to_hardware);
    b->pipeasio_fixed_buffersize
            = env_bool("PIPEASIO_FIXED_BUFFERSIZE", b->pipeasio_fixed_buffersize);
    b->pipeasio_follow_device_clock
            = env_bool("PIPEASIO_FOLLOW_DEVICE_CLOCK", b->pipeasio_follow_device_clock);
    b->pipeasio_preferred_buffersize
            = env_int("PIPEASIO_PREFERRED_BUFFERSIZE", b->pipeasio_preferred_buffersize);
    b->pipeasio_sample_rate = env_int("PIPEASIO_SAMPLE_RATE", b->pipeasio_sample_rate);
    copy_env_string("PIPEASIO_OUTPUT_DEVICE", b->pipeasio_output_device,
                    sizeof b->pipeasio_output_device);
    copy_env_string("PIPEASIO_INPUT_DEVICE", b->pipeasio_input_device,
                    sizeof b->pipeasio_input_device);

    if (b->pipeasio_number_inputs < 0)
        b->pipeasio_number_inputs = 0;
    if (b->pipeasio_number_inputs > PIPEASIO_WOW64_MAX_CHANNELS)
        b->pipeasio_number_inputs = PIPEASIO_WOW64_MAX_CHANNELS;
    if (b->pipeasio_number_outputs < 0)
        b->pipeasio_number_outputs = 0;
    if (b->pipeasio_number_outputs > PIPEASIO_WOW64_MAX_CHANNELS)
        b->pipeasio_number_outputs = PIPEASIO_WOW64_MAX_CHANNELS;

    if (!(b->pipeasio_preferred_buffersize > 0
          && !(b->pipeasio_preferred_buffersize & (b->pipeasio_preferred_buffersize - 1))
          && b->pipeasio_preferred_buffersize >= PIPEASIO_MIN_BUFFER_SIZE
          && b->pipeasio_preferred_buffersize <= PIPEASIO_MAX_BUFFER_SIZE))
        b->pipeasio_preferred_buffersize = PIPEASIO_DEFAULT_BUFFER_SIZE;
    if (b->pipeasio_sample_rate < 0)
        b->pipeasio_sample_rate = PIPEASIO_DEFAULT_SAMPLE_RATE;

    if (cfg.node_name[0])
        copy_string(b->client_name, sizeof b->client_name, cfg.node_name);
    else
        copy_string(b->client_name, sizeof b->client_name, "PipeASIO");
    copy_env_string("PIPEASIO_CLIENT_NAME", b->client_name, sizeof b->client_name);
}

static LONG
invoke_frontend(backend_state *b, LONG kind, LONG index, LONG direct, double sample_rate,
                LONG selector, LONG value)
{
    pipeasio_wow64_callback_params params;
    LONG                           result = 0;

    if (!b || !b->pe_iface || !b->callback_sync_initialized)
        return 0;

    memset(&params, 0, sizeof params);
    params.kind            = kind;
    params.index           = index;
    params.direct          = direct;
    params.selector        = selector;
    params.value           = value;
    params.sample_rate     = sample_rate;
    params.sample_position = b->host_num_samples;
    params.time_stamp      = b->host_time_stamp;
    params.time_flags      = 0x7;

    pthread_mutex_lock(&b->callback_producer_mutex);
    pthread_mutex_lock(&b->callback_mutex);
    if (!b->callback_shutdown)
    {
        struct timespec deadline;

        b->callback_request     = params;
        b->callback_pending     = true;
        b->callback_reply_ready = false;
        pthread_cond_signal(&b->callback_ready);
        deadline_after_ms(&deadline, PIPEASIO_CALLBACK_REPLY_TIMEOUT_MS);
        while (!b->callback_reply_ready && !b->callback_shutdown)
        {
            int wait_status = pthread_cond_timedwait(&b->callback_done, &b->callback_mutex,
                                                     &deadline);
            if (wait_status == ETIMEDOUT)
            {
                if (b->callback_reply_ready)
                    break;
                b->callback_pending     = false;
                b->callback_reply_ready = false;
                b->callback_shutdown    = true;
                set_error(b, "callback transport timed out");
                pthread_cond_broadcast(&b->callback_ready);
                pthread_cond_broadcast(&b->callback_done);
                break;
            }
            if (wait_status)
            {
                b->callback_pending     = false;
                b->callback_reply_ready = false;
                b->callback_shutdown    = true;
                set_error(b, "callback transport wait failed: %d", wait_status);
                pthread_cond_broadcast(&b->callback_ready);
                pthread_cond_broadcast(&b->callback_done);
                break;
            }
        }
        if (!b->callback_shutdown)
            result = b->callback_result;
    }
    pthread_mutex_unlock(&b->callback_mutex);
    pthread_mutex_unlock(&b->callback_producer_mutex);
    return result;
}

static void
copy_metadata(backend_state *b, pipeasio_wow64_metadata *m)
{
    audio_latency_range_t range;

    if (!m)
        return;
    memset(m, 0, sizeof *m);
    if (!b)
        return;

    m->inputs           = b->pipeasio_number_inputs;
    m->outputs          = b->pipeasio_number_outputs;
    m->preferred_buffer = b->pipeasio_preferred_buffersize;
    m->sample_rate      = b->host_sample_rate;
    m->sample_position  = b->host_num_samples;
    m->time_stamp       = b->host_time_stamp;

    if (b->pipeasio_fixed_buffersize || b->pipeasio_follow_device_clock)
    {
        m->min_buffer = m->max_buffer = m->preferred_buffer = b->host_current_buffersize;
        m->granularity                                      = 0;
    }
    else
    {
        m->min_buffer = PIPEASIO_MIN_BUFFER_SIZE;
        m->max_buffer = PIPEASIO_MAX_BUFFER_SIZE;
        m->granularity = -1;
    }

    m->input_latency = m->preferred_buffer;
    if (b->input_channel && b->pipeasio_number_inputs > 0 && b->input_channel[0].port)
    {
        audio_port_get_latency_range(b->input_channel[0].port, AUDIO_CAPTURE_LATENCY, &range);
        if (range.max)
            m->input_latency = (LONG)range.max;
    }
    m->output_latency = m->preferred_buffer;
    if (b->output_channel && b->pipeasio_number_outputs > 0 && b->output_channel[0].port)
    {
        audio_port_get_latency_range(b->output_channel[0].port, AUDIO_PLAYBACK_LATENCY, &range);
        if (range.max)
            m->output_latency = (LONG)range.max;
    }
}

static bool
init_callback_sync(backend_state *b)
{
    if (pthread_mutex_init(&b->callback_producer_mutex, NULL))
        return false;
    if (pthread_mutex_init(&b->callback_mutex, NULL))
    {
        pthread_mutex_destroy(&b->callback_producer_mutex);
        return false;
    }
    if (pthread_cond_init(&b->callback_ready, NULL))
    {
        pthread_mutex_destroy(&b->callback_mutex);
        pthread_mutex_destroy(&b->callback_producer_mutex);
        return false;
    }
    if (pthread_cond_init(&b->callback_done, NULL))
    {
        pthread_cond_destroy(&b->callback_ready);
        pthread_mutex_destroy(&b->callback_mutex);
        pthread_mutex_destroy(&b->callback_producer_mutex);
        return false;
    }
    b->callback_sync_initialized = true;
    return true;
}

static void
shutdown_callback_sync(backend_state *b)
{
    if (!b || !b->callback_sync_initialized)
        return;
    pthread_mutex_lock(&b->callback_mutex);
    b->callback_shutdown    = true;
    b->callback_pending     = false;
    b->callback_reply_ready = true;
    pthread_cond_broadcast(&b->callback_ready);
    pthread_cond_broadcast(&b->callback_done);
    pthread_mutex_unlock(&b->callback_mutex);
}

static void
reset_callback_sync(backend_state *b)
{
    if (!b || !b->callback_sync_initialized)
        return;
    pthread_mutex_lock(&b->callback_mutex);
    b->callback_pending     = false;
    b->callback_reply_ready = false;
    b->callback_shutdown    = false;
    b->callback_result      = 0;
    memset(&b->callback_request, 0, sizeof b->callback_request);
    pthread_mutex_unlock(&b->callback_mutex);
}

static void
destroy_callback_sync(backend_state *b)
{
    if (!b || !b->callback_sync_initialized)
        return;
    shutdown_callback_sync(b);
    pthread_cond_destroy(&b->callback_done);
    pthread_cond_destroy(&b->callback_ready);
    pthread_mutex_destroy(&b->callback_mutex);
    pthread_mutex_destroy(&b->callback_producer_mutex);
    b->callback_sync_initialized = false;
}

static int
buffer_size_callback(audio_nframes_t nframes, void *arg)
{
    backend_state *b = arg;
    (void)nframes;

    if (backend_state_get(b) == BackendRunning
        && invoke_frontend(b, PIPEASIO_WOW64_CB_NOTIFICATION, 0, 0, 0.0, 1, 3))
        invoke_frontend(b, PIPEASIO_WOW64_CB_NOTIFICATION, 0, 0, 0.0, 3, 0);
    return 0;
}

static void
latency_callback(audio_latency_mode_t mode, void *arg)
{
    backend_state *b = arg;
    (void)mode;

    if (backend_state_get(b) == BackendRunning
        && invoke_frontend(b, PIPEASIO_WOW64_CB_NOTIFICATION, 0, 0, 0.0, 1, 6))
        invoke_frontend(b, PIPEASIO_WOW64_CB_NOTIFICATION, 0, 0, 0.0, 6, 0);
}

static int
sample_rate_callback(audio_nframes_t nframes, void *arg)
{
    backend_state *b = arg;

    b->host_sample_rate = nframes;
    if (backend_state_get(b) == BackendRunning)
        invoke_frontend(b, PIPEASIO_WOW64_CB_SAMPLE_RATE, 0, 0, (double)nframes, 0, 0);
    return 0;
}

static void
maybe_request_follow_device_reset(backend_state *b)
{
    LONG quantum;

    if (!b->pipeasio_follow_device_clock)
        return;

    quantum = (LONG)audio_observed_quantum(b->audio_client);
    if (!quantum || quantum == b->host_current_buffersize
        || quantum == b->host_last_reset_quantum)
        return;

    atomic_store_explicit(&g_follower_quantum, quantum, memory_order_release);
    b->host_last_reset_quantum = quantum;
    debug_log("[pipeasio32-unix] follow_device_clock: observed quantum %ld, requesting reset\n",
              (long)quantum);

    if (invoke_frontend(b, PIPEASIO_WOW64_CB_NOTIFICATION, 0, 0, 0.0, 1, 3))
        invoke_frontend(b, PIPEASIO_WOW64_CB_NOTIFICATION, 0, 0, 0.0, 3, 0);
}

static int
process_callback(audio_nframes_t nframes, void *arg)
{
    backend_state *b = arg;
    bool           buffer_index;

    if (backend_state_get(b) != BackendRunning)
    {
        for (LONG i = 0; i < b->pipeasio_number_outputs; i++)
        {
            audio_sample_t *dst = audio_port_get_buffer(b->output_channel[i].port, nframes);
            if (dst)
                memset(dst, 0, sizeof(*dst) * nframes);
        }
        return 0;
    }

    buffer_index = host_buffer_index_get(b);

    for (LONG i = 0; i < b->pipeasio_number_inputs; i++)
    {
        if (b->input_channel[i].active)
        {
            audio_sample_t *src = audio_port_get_buffer(b->input_channel[i].port, nframes);
            audio_sample_t *dst = b->input_channel[i].audio_buffer
                                  + pipeasio_host_half_offset_samples(buffer_index, nframes);
            if (src)
                memcpy(dst, src, sizeof(*dst) * nframes);
            else
                memset(dst, 0, sizeof(*dst) * nframes);
        }
    }

    b->host_num_samples = i64_from_u64(i64_to_u64(b->host_num_samples) + nframes);
    b->host_time_stamp  = i64_from_u64(audio_get_time_nsec(b->audio_client));

    if (b->host_time_info_mode)
        invoke_frontend(b, PIPEASIO_WOW64_CB_TIME_INFO, buffer_index, 1, 0.0, 0, 0);
    else
        invoke_frontend(b, PIPEASIO_WOW64_CB_BUFFER_SWITCH, buffer_index, 1, 0.0, 0, 0);

    for (LONG i = 0; i < b->pipeasio_number_outputs; i++)
    {
        if (b->output_channel[i].active)
        {
            audio_sample_t *dst = audio_port_get_buffer(b->output_channel[i].port, nframes);
            audio_sample_t *src = b->output_channel[i].audio_buffer
                                  + pipeasio_host_half_offset_samples(buffer_index, nframes);
            if (dst)
                memcpy(dst, src, sizeof(*dst) * nframes);
        }
    }

    host_buffer_index_set(b, !buffer_index);
    maybe_request_follow_device_reset(b);
    return 0;
}

static bool
register_ports(backend_state *b)
{
    for (LONG i = 0; i < b->pipeasio_number_inputs; i++)
    {
        snprintf(b->input_channel[i].port_name, sizeof b->input_channel[i].port_name, "in_%ld",
                 (long)i + 1);
        b->input_channel[i].port = audio_port_register(
                b->audio_client, b->input_channel[i].port_name, AUDIO_DEFAULT_TYPE,
                AUDIO_PORT_IS_INPUT, i);
        if (!b->input_channel[i].port)
            return false;
    }
    for (LONG i = 0; i < b->pipeasio_number_outputs; i++)
    {
        snprintf(b->output_channel[i].port_name, sizeof b->output_channel[i].port_name,
                 "out_%ld", (long)i + 1);
        b->output_channel[i].port = audio_port_register(
                b->audio_client, b->output_channel[i].port_name, AUDIO_DEFAULT_TYPE,
                AUDIO_PORT_IS_OUTPUT, i);
        if (!b->output_channel[i].port)
            return false;
    }
    return true;
}

static void
connect_hardware(backend_state *b)
{
    const char **in_src  = NULL;
    const char **out_dst = NULL;
    const char **use_in;
    const char **use_out;

    if (!b->pipeasio_connect_to_hardware)
        return;

    if (b->pipeasio_input_device[0])
        in_src = audio_get_device_ports(b->audio_client, b->pipeasio_input_device,
                                        AUDIO_PORT_IS_OUTPUT);
    if (b->pipeasio_output_device[0])
        out_dst = audio_get_device_ports(b->audio_client, b->pipeasio_output_device,
                                         AUDIO_PORT_IS_INPUT);
    use_in  = in_src ? in_src : b->phys_input_ports;
    use_out = out_dst ? out_dst : b->phys_output_ports;

    for (LONG i = 0; use_in && use_in[i] && i < b->pipeasio_number_inputs; i++)
    {
        const char *type = audio_port_type(audio_port_by_name(b->audio_client, use_in[i]));
        if (type && strstr(type, "audio"))
            audio_connect(b->audio_client, use_in[i], audio_port_name(b->input_channel[i].port));
    }
    for (LONG i = 0; use_out && use_out[i] && i < b->pipeasio_number_outputs; i++)
    {
        const char *type = audio_port_type(audio_port_by_name(b->audio_client, use_out[i]));
        if (type && strstr(type, "audio"))
            audio_connect(b->audio_client, audio_port_name(b->output_channel[i].port),
                          use_out[i]);
    }

    if (in_src)
        audio_free_ports(in_src);
    if (out_dst)
        audio_free_ports(out_dst);
}

static void
release_backend(backend_state *b)
{
    if (!b)
        return;
    shutdown_callback_sync(b);
    if (backend_state_get(b) == BackendRunning)
        backend_state_set(b, BackendPrepared);
    if (backend_state_get(b) == BackendPrepared && b->audio_client)
        audio_deactivate(b->audio_client);
    if (b->audio_client)
    {
        if (b->input_channel)
        {
            for (LONG i = 0; i < b->pipeasio_number_inputs; i++)
                if (b->input_channel[i].port)
                    audio_port_unregister(b->audio_client, b->input_channel[i].port);
        }
        if (b->output_channel)
        {
            for (LONG i = 0; i < b->pipeasio_number_outputs; i++)
                if (b->output_channel[i].port)
                    audio_port_unregister(b->audio_client, b->output_channel[i].port);
        }
        audio_free_ports(b->phys_input_ports);
        audio_free_ports(b->phys_output_ports);
        audio_close(b->audio_client);
    }
    free(b->input_channel);
    destroy_callback_sync(b);
    free(b);
}

static NTSTATUS
wow64_init(void *args)
{
    pipeasio_wow64_init_params *p = args;
    backend_state              *b;
    uint32_t                    audio_status = 0;

    if (!p || p->version != PIPEASIO_WOW64_VERSION)
        return STATUS_INVALID_PARAMETER;

    b = calloc(1, sizeof *b);
    if (!b)
        return STATUS_NO_MEMORY;
    if (!init_callback_sync(b))
    {
        free(b);
        return STATUS_NO_MEMORY;
    }
    debug_log("[pipeasio32-unix] init: allocated backend %p\n", (void *)b);
    configure_backend(b);
    debug_log("[pipeasio32-unix] init: configured client='%s' in=%ld out=%ld bs=%ld\n",
              b->client_name, (long)b->pipeasio_number_inputs,
              (long)b->pipeasio_number_outputs, (long)b->pipeasio_preferred_buffersize);

    b->audio_client = audio_open(b->client_name, AUDIO_NULL_OPTION, &audio_status);
    debug_log("[pipeasio32-unix] init: audio_open -> %p status=%u\n", (void *)b->audio_client,
              audio_status);
    if (!b->audio_client)
    {
        p->asio_status = 0;
        set_error(b, "audio_open failed for %s", b->client_name);
        copy_backend_error(b, p->error, sizeof p->error);
        release_backend(b);
        return STATUS_SUCCESS;
    }

    audio_set_forced_rate(b->audio_client, (audio_nframes_t)b->pipeasio_sample_rate);
    audio_set_follow_device(b->audio_client, b->pipeasio_follow_device_clock);
    b->host_sample_rate        = audio_get_sample_rate(b->audio_client);
    b->host_current_buffersize = b->pipeasio_preferred_buffersize;
    if (b->pipeasio_follow_device_clock)
    {
        LONG hint = atomic_load_explicit(&g_follower_quantum, memory_order_acquire);
        if (hint)
            b->host_current_buffersize = hint;
    }
    debug_log("[pipeasio32-unix] init: sample_rate=%f current_bs=%ld\n", b->host_sample_rate,
              (long)b->host_current_buffersize);

    size_t channels = (size_t)b->pipeasio_number_inputs + (size_t)b->pipeasio_number_outputs;
    b->input_channel = calloc(channels ? channels : 1, sizeof *b->input_channel);
    if (!b->input_channel)
    {
        p->asio_status = 0;
        set_error(b, "channel allocation failed");
        copy_backend_error(b, p->error, sizeof p->error);
        release_backend(b);
        return STATUS_SUCCESS;
    }
    b->output_channel = b->input_channel + b->pipeasio_number_inputs;

    b->phys_input_ports = audio_get_ports(b->audio_client, NULL, NULL,
                                          AUDIO_PORT_IS_PHYSICAL | AUDIO_PORT_IS_OUTPUT);
    b->phys_output_ports = audio_get_ports(b->audio_client, NULL, NULL,
                                           AUDIO_PORT_IS_PHYSICAL | AUDIO_PORT_IS_INPUT);
    debug_log("[pipeasio32-unix] init: got physical port caches\n");

    if (!register_ports(b) || !audio_set_buffer_size_callback(b->audio_client,
                                                              buffer_size_callback, b)
        || !audio_set_latency_callback(b->audio_client, latency_callback, b)
        || !audio_set_process_callback(b->audio_client, process_callback, b)
        || !audio_set_sample_rate_callback(b->audio_client, sample_rate_callback, b))
    {
        p->asio_status = 0;
        set_error(b, "backend initialization failed");
        copy_backend_error(b, p->error, sizeof p->error);
        release_backend(b);
        return STATUS_SUCCESS;
    }

    backend_state_set(b, BackendInitialized);
    p->backend           = (UINT64)(uintptr_t)b;
    p->asio_status       = 1;
    copy_metadata(b, &p->metadata);
    copy_backend_error(b, p->error, sizeof p->error);
    debug_log("[pipeasio32-unix] init: success backend=%p\n", (void *)b);
    return STATUS_SUCCESS;
}

static NTSTATUS
wow64_create_buffers(void *args)
{
    pipeasio_wow64_create_buffers_params *p = args;
    backend_state                        *b;
    size_t                                expected_bytes;

    if (!p || p->version != PIPEASIO_WOW64_VERSION)
        return STATUS_INVALID_PARAMETER;
    b = backend_from_handle(p->backend);
    if (!b || backend_state_get(b) != BackendInitialized)
        return STATUS_INVALID_HANDLE;
    if (p->num_channels <= 0 || p->num_channels > PIPEASIO_WOW64_MAX_CHANNELS
        || p->buffer_size <= 0 || p->buffer_size > PIPEASIO_WOW64_MAX_BUFFER_FRAMES
        || !p->buffer_base || !p->pe_iface)
        return STATUS_INVALID_PARAMETER;

    expected_bytes = pipeasio_host_callback_size_bytes(
            (uint32_t)b->pipeasio_number_inputs, (uint32_t)b->pipeasio_number_outputs,
            (size_t)p->buffer_size, sizeof(audio_sample_t));
    if (!expected_bytes || (size_t)p->buffer_bytes < expected_bytes)
    {
        p->asio_status = -997;
        set_error(b, "callback buffer too small: got %lu need %zu",
                  (unsigned long)p->buffer_bytes, expected_bytes);
        copy_backend_error(b, p->error, sizeof p->error);
        return STATUS_SUCCESS;
    }

    if (b->pipeasio_fixed_buffersize || b->pipeasio_follow_device_clock)
    {
        if (b->host_current_buffersize != p->buffer_size)
        {
            p->asio_status = -997;
            set_error(b, "unsupported fixed buffer size %ld", (long)p->buffer_size);
            copy_backend_error(b, p->error, sizeof p->error);
            return STATUS_SUCCESS;
        }
    }
    else if (!(p->buffer_size > 0 && !(p->buffer_size & (p->buffer_size - 1))
               && p->buffer_size >= PIPEASIO_MIN_BUFFER_SIZE
               && p->buffer_size <= PIPEASIO_MAX_BUFFER_SIZE))
    {
        p->asio_status = -997;
        set_error(b, "invalid buffer size %ld", (long)p->buffer_size);
        copy_backend_error(b, p->error, sizeof p->error);
        return STATUS_SUCCESS;
    }

    b->host_current_buffersize = p->buffer_size;
    if (!audio_set_buffer_size(b->audio_client, (audio_nframes_t)p->buffer_size))
    {
        p->asio_status = -999;
        set_error(b, "audio_set_buffer_size(%ld) failed", (long)p->buffer_size);
        copy_backend_error(b, p->error, sizeof p->error);
        return STATUS_SUCCESS;
    }

    reset_callback_sync(b);
    b->pe_iface                    = p->pe_iface;
    b->callback_audio_buffer       = (audio_sample_t *)(uintptr_t)p->buffer_base;
    b->callback_audio_buffer_bytes = p->buffer_bytes;
    for (LONG i = 0; i < b->pipeasio_number_inputs; i++)
    {
        b->input_channel[i].active       = false;
        b->input_channel[i].audio_buffer = b->callback_audio_buffer
                                           + pipeasio_host_input_offset_samples(
                                                   i, b->host_current_buffersize);
    }
    for (LONG i = 0; i < b->pipeasio_number_outputs; i++)
    {
        b->output_channel[i].active       = false;
        b->output_channel[i].audio_buffer = b->callback_audio_buffer
                                            + pipeasio_host_output_offset_samples(
                                                    i, b->pipeasio_number_inputs,
                                                    b->host_current_buffersize);
    }

    for (LONG i = 0; i < p->num_channels; i++)
    {
        LONG ch = p->buffers[i].channel;
        if (p->buffers[i].is_input)
        {
            if (ch < 0 || ch >= b->pipeasio_number_inputs)
            {
                p->asio_status = -997;
                set_error(b, "invalid input channel %ld", (long)ch);
                copy_backend_error(b, p->error, sizeof p->error);
                return STATUS_SUCCESS;
            }
            b->input_channel[ch].active = true;
        }
        else
        {
            if (ch < 0 || ch >= b->pipeasio_number_outputs)
            {
                p->asio_status = -997;
                set_error(b, "invalid output channel %ld", (long)ch);
                copy_backend_error(b, p->error, sizeof p->error);
                return STATUS_SUCCESS;
            }
            b->output_channel[ch].active = true;
        }
    }

    b->host_time_info_mode = p->time_info_mode ? true : false;

    if (!audio_activate(b->audio_client))
    {
        p->asio_status = -1000;
        set_error(b, "audio_activate failed");
        copy_backend_error(b, p->error, sizeof p->error);
        return STATUS_SUCCESS;
    }

    connect_hardware(b);
    backend_state_set(b, BackendPrepared);
    p->asio_status       = 0;
    copy_metadata(b, &p->metadata);
    copy_backend_error(b, p->error, sizeof p->error);
    return STATUS_SUCCESS;
}

static NTSTATUS
wow64_start(void *args)
{
    pipeasio_wow64_simple_params *p = args;
    backend_state                *b;
    bool                          buffer_index = false;

    if (!p || p->version != PIPEASIO_WOW64_VERSION)
        return STATUS_INVALID_PARAMETER;
    b = backend_from_handle(p->backend);
    if (!b || backend_state_get(b) != BackendPrepared)
        return STATUS_INVALID_HANDLE;

    if (b->callback_audio_buffer && b->callback_audio_buffer_bytes)
        memset(b->callback_audio_buffer, 0, b->callback_audio_buffer_bytes);
    host_buffer_index_set(b, buffer_index);
    b->host_num_samples = i64_from_u64(0);
    b->host_time_stamp  = i64_from_u64(audio_get_time_nsec(b->audio_client));

    if (b->host_time_info_mode)
        invoke_frontend(b, PIPEASIO_WOW64_CB_TIME_INFO, buffer_index, 1, 0.0, 0, 0);
    else
        invoke_frontend(b, PIPEASIO_WOW64_CB_BUFFER_SWITCH, buffer_index, 1, 0.0, 0, 0);
    host_buffer_index_set(b, !buffer_index);
    backend_state_set(b, BackendRunning);

    p->asio_status = 0;
    copy_metadata(b, &p->metadata);
    copy_backend_error(b, p->error, sizeof p->error);
    return STATUS_SUCCESS;
}

static NTSTATUS
wow64_stop(void *args)
{
    pipeasio_wow64_simple_params *p = args;
    backend_state                *b;

    if (!p || p->version != PIPEASIO_WOW64_VERSION)
        return STATUS_INVALID_PARAMETER;
    b = backend_from_handle(p->backend);
    if (!b || backend_state_get(b) != BackendRunning)
        return STATUS_INVALID_HANDLE;
    backend_state_set(b, BackendPrepared);
    p->asio_status       = 0;
    copy_metadata(b, &p->metadata);
    copy_backend_error(b, p->error, sizeof p->error);
    return STATUS_SUCCESS;
}

static NTSTATUS
wow64_dispose_buffers(void *args)
{
    pipeasio_wow64_simple_params *p = args;
    backend_state                *b;

    if (!p || p->version != PIPEASIO_WOW64_VERSION)
        return STATUS_INVALID_PARAMETER;
    b = backend_from_handle(p->backend);
    if (!b)
        return STATUS_INVALID_HANDLE;
    if (backend_state_get(b) == BackendRunning)
        backend_state_set(b, BackendPrepared);
    if (backend_state_get(b) != BackendPrepared)
        return STATUS_INVALID_HANDLE;
    shutdown_callback_sync(b);
    if (!audio_deactivate(b->audio_client))
    {
        p->asio_status = -1000;
        set_error(b, "audio_deactivate failed");
    }
    else
    {
        backend_state_set(b, BackendInitialized);
        b->pe_iface          = 0;
        b->callback_audio_buffer = NULL;
        b->callback_audio_buffer_bytes = 0;
        p->asio_status = 0;
    }
    copy_metadata(b, &p->metadata);
    copy_backend_error(b, p->error, sizeof p->error);
    return STATUS_SUCCESS;
}

static NTSTATUS
wow64_get_sample_position(void *args)
{
    pipeasio_wow64_position_params *p = args;
    backend_state                  *b;

    if (!p || p->version != PIPEASIO_WOW64_VERSION)
        return STATUS_INVALID_PARAMETER;
    b = backend_from_handle(p->backend);
    if (!b)
        return STATUS_INVALID_HANDLE;
    p->sample_position = b->host_num_samples;
    p->time_stamp      = b->host_time_stamp;
    p->asio_status     = 0;
    copy_backend_error(b, p->error, sizeof p->error);
    return STATUS_SUCCESS;
}

static NTSTATUS
wow64_wait_callback(void *args)
{
    pipeasio_wow64_callback_wait_params *p = args;
    backend_state                       *b;

    if (!p || p->version != PIPEASIO_WOW64_VERSION)
        return STATUS_INVALID_PARAMETER;
    b = backend_from_handle(p->backend);
    if (!b || !b->callback_sync_initialized)
        return STATUS_INVALID_HANDLE;

    pthread_mutex_lock(&b->callback_mutex);
    while (!b->callback_pending && !b->callback_shutdown)
        pthread_cond_wait(&b->callback_ready, &b->callback_mutex);
    p->shutdown = b->callback_shutdown ? 1 : 0;
    if (!p->shutdown)
        p->callback = b->callback_request;
    pthread_mutex_unlock(&b->callback_mutex);

    copy_backend_error(b, p->error, sizeof p->error);
    return STATUS_SUCCESS;
}

static NTSTATUS
wow64_reply_callback(void *args)
{
    pipeasio_wow64_callback_reply_params *p = args;
    backend_state                        *b;

    if (!p || p->version != PIPEASIO_WOW64_VERSION)
        return STATUS_INVALID_PARAMETER;
    b = backend_from_handle(p->backend);
    if (!b || !b->callback_sync_initialized)
        return STATUS_INVALID_HANDLE;

    pthread_mutex_lock(&b->callback_mutex);
    if (!b->callback_shutdown && b->callback_pending)
    {
        b->callback_result      = p->result;
        b->callback_pending     = false;
        b->callback_reply_ready = true;
        pthread_cond_signal(&b->callback_done);
    }
    pthread_mutex_unlock(&b->callback_mutex);

    copy_backend_error(b, p->error, sizeof p->error);
    return STATUS_SUCCESS;
}

static NTSTATUS
wow64_close(void *args)
{
    pipeasio_wow64_simple_params *p = args;
    backend_state                *b;

    if (!p || p->version != PIPEASIO_WOW64_VERSION)
        return STATUS_INVALID_PARAMETER;
    b = backend_from_handle(p->backend);
    release_backend(b);
    p->backend     = 0;
    p->asio_status = 0;
    return STATUS_SUCCESS;
}

const unixlib_entry_t __wine_unix_call_funcs[] = {
    wow64_init,
    wow64_create_buffers,
    wow64_start,
    wow64_stop,
    wow64_dispose_buffers,
    wow64_get_sample_position,
    wow64_close,
    wow64_wait_callback,
    wow64_reply_callback,
};

#ifdef _WIN64
const unixlib_entry_t __wine_unix_call_wow64_funcs[] = {
    wow64_init,
    wow64_create_buffers,
    wow64_start,
    wow64_stop,
    wow64_dispose_buffers,
    wow64_get_sample_position,
    wow64_close,
    wow64_wait_callback,
    wow64_reply_callback,
};
#endif

_Static_assert(sizeof(__wine_unix_call_funcs) / sizeof(__wine_unix_call_funcs[0])
                       == PIPEASIO_WOW64_CALL_COUNT,
               "unix call table size drift");
