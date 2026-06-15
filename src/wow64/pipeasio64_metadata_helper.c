#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pipeasio_wow64_ipc.h"

typedef struct IPipeASIO IPipeASIO;

typedef struct w_int64_t
{
    ULONG hi;
    ULONG lo;
} w_int64_t;

typedef struct BufferInformation
{
    LONG  isInputType;
    LONG  channelNumber;
    void *audioBufferStart;
    void *audioBufferEnd;
} BufferInformation;

typedef struct TimeInformation
{
    LONG      _1[4];
    double    _2;
    w_int64_t timeStamp;
    w_int64_t numSamples;
    double    sampleRate;
    ULONG     flags;
    char      _3[12];
    double    speedForTimeCode;
    w_int64_t timeStampForTimeCode;
    ULONG     flagsForTimeCode;
    char      _4[64];
} TimeInformation;

typedef struct Callbacks
{
    void (*swapBuffers)(LONG, LONG);
    void (*sampleRateChanged)(double);
    LONG (*sendNotification)(LONG, LONG, void *, double *);
    void *(*swapBuffersWithTimeInfo)(TimeInformation *, LONG, LONG);
} Callbacks;

typedef struct IPipeASIOVtbl
{
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPipeASIO *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPipeASIO *);
    ULONG(STDMETHODCALLTYPE *Release)(IPipeASIO *);
    LONG(STDMETHODCALLTYPE *Init)(IPipeASIO *, void *);
    void(STDMETHODCALLTYPE *GetDriverName)(IPipeASIO *, char *);
    LONG(STDMETHODCALLTYPE *GetDriverVersion)(IPipeASIO *);
    void(STDMETHODCALLTYPE *GetErrorMessage)(IPipeASIO *, char *);
    LONG(STDMETHODCALLTYPE *Start)(IPipeASIO *);
    LONG(STDMETHODCALLTYPE *Stop)(IPipeASIO *);
    LONG(STDMETHODCALLTYPE *GetChannels)(IPipeASIO *, LONG *, LONG *);
    LONG(STDMETHODCALLTYPE *GetLatencies)(IPipeASIO *, LONG *, LONG *);
    LONG(STDMETHODCALLTYPE *GetBufferSize)(IPipeASIO *, LONG *, LONG *, LONG *, LONG *);
    LONG(STDMETHODCALLTYPE *CanSampleRate)(IPipeASIO *, double);
    LONG(STDMETHODCALLTYPE *GetSampleRate)(IPipeASIO *, double *);
    LONG(STDMETHODCALLTYPE *SetSampleRate)(IPipeASIO *, double);
    LONG(STDMETHODCALLTYPE *GetClockSources)(IPipeASIO *, void *, LONG *);
    LONG(STDMETHODCALLTYPE *SetClockSource)(IPipeASIO *, LONG);
    LONG(STDMETHODCALLTYPE *GetSamplePosition)(IPipeASIO *, w_int64_t *, w_int64_t *);
    LONG(STDMETHODCALLTYPE *GetChannelInfo)(IPipeASIO *, void *);
    LONG(STDMETHODCALLTYPE *CreateBuffers)(IPipeASIO *, BufferInformation *, LONG, LONG,
                                           Callbacks *);
    LONG(STDMETHODCALLTYPE *DisposeBuffers)(IPipeASIO *);
    LONG(STDMETHODCALLTYPE *ControlPanel)(IPipeASIO *);
    LONG(STDMETHODCALLTYPE *Future)(IPipeASIO *, LONG, void *);
    LONG(STDMETHODCALLTYPE *OutputReady)(IPipeASIO *);
} IPipeASIOVtbl;

struct IPipeASIO
{
    const IPipeASIOVtbl *lpVtbl;
};

static const CLSID CLSID_PipeASIO
        = { 0x2d3ca9e2, 0x1193, 0x4c5d, { 0xb5, 0xfd, 0x38, 0x79, 0x8f, 0x3d, 0xc0, 0x74 } };

typedef struct bridge_state
{
    HANDLE mapping;
    HANDLE command_event;
    HANDLE reply_event;
    HANDLE callback_event;
    HANDLE callback_done_event;
    pipeasio_wow64_shared *shared;
    IPipeASIO             *asio;
    BufferInformation      buffers[PIPEASIO_WOW64_MAX_CHANNELS];
    Callbacks              callbacks;
    LONG                   state;
} bridge_state;

enum
{
    HelperLoaded,
    HelperInitialized,
    HelperPrepared,
    HelperRunning
};

static bridge_state *g_bridge;

static int
fail(const char *what, LONG rc)
{
    fprintf(stderr, "%s failed: 0x%08lx\n", what, (unsigned long)rc);
    return 1;
}

static void
set_error(bridge_state *b, const char *what, LONG rc)
{
    if (!b || !b->shared)
        return;
    snprintf(b->shared->error, sizeof b->shared->error, "%s failed: %ld", what, (long)rc);
}

static void
copy_metadata(bridge_state *b)
{
    pipeasio_wow64_shared *s = b->shared;
    LONG                  rc;
    if (!b->asio)
        return;
    rc = b->asio->lpVtbl->GetChannels(b->asio, &s->inputs, &s->outputs);
    if (rc != 0)
        set_error(b, "GetChannels", rc);
    rc = b->asio->lpVtbl->GetBufferSize(b->asio, &s->min_buffer, &s->max_buffer,
                                        &s->preferred_buffer, &s->granularity);
    if (rc != 0)
        set_error(b, "GetBufferSize", rc);
    rc = b->asio->lpVtbl->GetSampleRate(b->asio, &s->sample_rate);
    if (rc != 0)
        set_error(b, "GetSampleRate", rc);
    if (b->state >= HelperPrepared)
    {
        rc = b->asio->lpVtbl->GetLatencies(b->asio, &s->input_latency, &s->output_latency);
        if (rc != 0)
            set_error(b, "GetLatencies", rc);
    }
}

static void
copy_real_inputs_to_shared(bridge_state *b, LONG index)
{
    LONG frames = b->shared->buffer_size;
    if (index < 0 || index > 1 || frames <= 0 || frames > PIPEASIO_WOW64_MAX_BUFFER_FRAMES)
        return;
    for (LONG i = 0; i < b->shared->num_channels; i++)
    {
        if (!b->shared->buffers[i].is_input)
            continue;
        float *src = index ? b->buffers[i].audioBufferEnd : b->buffers[i].audioBufferStart;
        float *dst = &b->shared->samples[pipeasio_wow64_buffer_offset(i, index)];
        if (src)
            memcpy(dst, src, (size_t)frames * sizeof(float));
        else
            memset(dst, 0, (size_t)frames * sizeof(float));
    }
}

static void
copy_shared_outputs_to_real(bridge_state *b, LONG index)
{
    LONG frames = b->shared->buffer_size;
    if (index < 0 || index > 1 || frames <= 0 || frames > PIPEASIO_WOW64_MAX_BUFFER_FRAMES)
        return;
    for (LONG i = 0; i < b->shared->num_channels; i++)
    {
        if (b->shared->buffers[i].is_input)
            continue;
        float *src = &b->shared->samples[pipeasio_wow64_buffer_offset(i, index)];
        float *dst = index ? b->buffers[i].audioBufferEnd : b->buffers[i].audioBufferStart;
        if (dst)
            memcpy(dst, src, (size_t)frames * sizeof(float));
    }
}

static LONG
invoke_frontend(bridge_state *b, LONG kind, LONG index, LONG direct, TimeInformation *time,
                double sample_rate, LONG selector, LONG value)
{
    pipeasio_wow64_shared *s = b->shared;
    LONG                  generation;

    if (kind == PIPEASIO_WOW64_CB_BUFFER_SWITCH || kind == PIPEASIO_WOW64_CB_TIME_INFO)
        copy_real_inputs_to_shared(b, index);

    generation                    = s->callback_generation + 1;
    s->callback_kind              = kind;
    s->callback_index             = index;
    s->callback_direct            = direct;
    s->callback_selector          = selector;
    s->callback_value             = value;
    s->callback_result            = 0;
    s->callback_sample_rate       = sample_rate;
    s->callback_sample_position.hi = time ? time->numSamples.hi : s->sample_position.hi;
    s->callback_sample_position.lo = time ? time->numSamples.lo : s->sample_position.lo;
    s->callback_time_stamp.hi      = time ? time->timeStamp.hi : s->time_stamp.hi;
    s->callback_time_stamp.lo      = time ? time->timeStamp.lo : s->time_stamp.lo;
    s->callback_time_flags         = time ? time->flags : 0;
    s->sample_position             = s->callback_sample_position;
    s->time_stamp                  = s->callback_time_stamp;
    s->callback_generation         = generation;

    ResetEvent(b->callback_done_event);
    SetEvent(b->callback_event);
    WaitForSingleObject(b->callback_done_event, INFINITE);

    if (kind == PIPEASIO_WOW64_CB_BUFFER_SWITCH || kind == PIPEASIO_WOW64_CB_TIME_INFO)
        copy_shared_outputs_to_real(b, index);

    return s->callback_result;
}

static void
helper_swap_buffers(LONG index, LONG direct)
{
    if (g_bridge)
        invoke_frontend(g_bridge, PIPEASIO_WOW64_CB_BUFFER_SWITCH, index, direct, NULL, 0.0, 0, 0);
}

static void
helper_sample_rate_changed(double rate)
{
    if (g_bridge)
        invoke_frontend(g_bridge, PIPEASIO_WOW64_CB_SAMPLE_RATE, 0, 0, NULL, rate, 0, 0);
}

static LONG
helper_send_notification(LONG selector, LONG value, void *message, double *opt)
{
    (void)message;
    (void)opt;
    if (!g_bridge)
        return selector == 7 ? 1 : 0;
    return invoke_frontend(g_bridge, PIPEASIO_WOW64_CB_NOTIFICATION, 0, 0, NULL, 0.0, selector,
                           value);
}

static void *
helper_swap_buffers_with_time_info(TimeInformation *time, LONG index, LONG direct)
{
    if (g_bridge)
        invoke_frontend(g_bridge, PIPEASIO_WOW64_CB_TIME_INFO, index, direct, time, 0.0, 0, 0);
    return NULL;
}

static LONG
handle_init(bridge_state *b)
{
    HRESULT hr;
    LONG    rc;

    if (b->state >= HelperInitialized)
        return 0;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
        set_error(b, "CoInitializeEx", hr);
        return hr;
    }

    hr = CoCreateInstance(&CLSID_PipeASIO, NULL, CLSCTX_INPROC_SERVER, &CLSID_PipeASIO,
                          (void **)&b->asio);
    if (FAILED(hr) || !b->asio)
    {
        set_error(b, "CoCreateInstance(pipeasio64)", hr);
        CoUninitialize();
        return hr;
    }

    rc = b->asio->lpVtbl->Init(b->asio, NULL);
    if (rc != 1)
    {
        set_error(b, "pipeasio64 Init", rc);
        b->asio->lpVtbl->Release(b->asio);
        b->asio = NULL;
        CoUninitialize();
        return rc;
    }

    b->state = HelperInitialized;
    copy_metadata(b);
    return 0;
}

static LONG
handle_create_buffers(bridge_state *b)
{
    LONG rc;
    if (b->state != HelperInitialized)
        return -1000;
    if (b->shared->num_channels <= 0 || b->shared->num_channels > PIPEASIO_WOW64_MAX_CHANNELS
        || b->shared->buffer_size <= 0
        || b->shared->buffer_size > PIPEASIO_WOW64_MAX_BUFFER_FRAMES)
        return -997;

    ZeroMemory(b->buffers, sizeof b->buffers);
    for (LONG i = 0; i < b->shared->num_channels; i++)
    {
        b->buffers[i].isInputType   = b->shared->buffers[i].is_input;
        b->buffers[i].channelNumber = b->shared->buffers[i].channel;
    }
    b->callbacks.swapBuffers              = helper_swap_buffers;
    b->callbacks.sampleRateChanged        = helper_sample_rate_changed;
    b->callbacks.sendNotification         = helper_send_notification;
    b->callbacks.swapBuffersWithTimeInfo  = helper_swap_buffers_with_time_info;

    rc = b->asio->lpVtbl->CreateBuffers(b->asio, b->buffers, b->shared->num_channels,
                                        b->shared->buffer_size, &b->callbacks);
    if (rc != 0)
    {
        set_error(b, "CreateBuffers", rc);
        return rc;
    }
    b->state = HelperPrepared;
    copy_metadata(b);
    return 0;
}

static LONG
handle_start(bridge_state *b)
{
    LONG rc;
    if (b->state != HelperPrepared)
        return -1000;
    rc = b->asio->lpVtbl->Start(b->asio);
    if (rc == 0)
        b->state = HelperRunning;
    else
        set_error(b, "Start", rc);
    copy_metadata(b);
    return rc;
}

static LONG
handle_stop(bridge_state *b)
{
    LONG rc;
    if (b->state != HelperRunning)
        return -1000;
    rc = b->asio->lpVtbl->Stop(b->asio);
    if (rc == 0)
        b->state = HelperPrepared;
    else
        set_error(b, "Stop", rc);
    copy_metadata(b);
    return rc;
}

static LONG
handle_dispose(bridge_state *b)
{
    LONG rc;
    if (b->state == HelperRunning)
        handle_stop(b);
    if (b->state != HelperPrepared)
        return -1000;
    rc = b->asio->lpVtbl->DisposeBuffers(b->asio);
    if (rc == 0)
        b->state = HelperInitialized;
    else
        set_error(b, "DisposeBuffers", rc);
    copy_metadata(b);
    return rc;
}

static void
release_bridge(bridge_state *b)
{
    if (b->asio)
    {
        if (b->state == HelperRunning)
            handle_stop(b);
        if (b->state == HelperPrepared)
            handle_dispose(b);
        b->asio->lpVtbl->Release(b->asio);
        b->asio = NULL;
    }
    if (b->state >= HelperInitialized)
        CoUninitialize();
    b->state = HelperLoaded;
}

static int
bridge_loop(int argc, char **argv)
{
    bridge_state b;
    BOOL         done = FALSE;
    (void)argc;

    ZeroMemory(&b, sizeof b);
    b.mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, argv[2]);
    b.command_event = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, argv[3]);
    b.reply_event = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, argv[4]);
    b.callback_event = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, argv[5]);
    b.callback_done_event = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, argv[6]);
    if (!b.mapping || !b.command_event || !b.reply_event || !b.callback_event
        || !b.callback_done_event)
        return 2;

    b.shared = MapViewOfFile(b.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(pipeasio_wow64_shared));
    if (!b.shared)
        return 3;
    if (b.shared->magic != PIPEASIO_WOW64_MAGIC || b.shared->version != PIPEASIO_WOW64_VERSION)
        return 4;

    g_bridge = &b;

    while (!done)
    {
        LONG command;
        if (WaitForSingleObject(b.command_event, INFINITE) != WAIT_OBJECT_0)
            break;
        command = b.shared->command;
        b.shared->command_status = -1000;
        b.shared->error[0]       = 0;

        switch (command)
        {
        case PIPEASIO_WOW64_CMD_INIT:
            b.shared->command_status = handle_init(&b);
            break;
        case PIPEASIO_WOW64_CMD_CREATE_BUFFERS:
            b.shared->command_status = handle_create_buffers(&b);
            break;
        case PIPEASIO_WOW64_CMD_START:
            b.shared->command_status = handle_start(&b);
            break;
        case PIPEASIO_WOW64_CMD_STOP:
            b.shared->command_status = handle_stop(&b);
            break;
        case PIPEASIO_WOW64_CMD_DISPOSE_BUFFERS:
            b.shared->command_status = handle_dispose(&b);
            break;
        case PIPEASIO_WOW64_CMD_EXIT:
            release_bridge(&b);
            b.shared->command_status = 0;
            done = TRUE;
            break;
        default:
            b.shared->command_status = -998;
            break;
        }
        b.shared->command_done_generation = b.shared->command_generation;
        SetEvent(b.reply_event);
    }

    release_bridge(&b);
    g_bridge = NULL;
    if (b.shared)
        UnmapViewOfFile(b.shared);
    if (b.mapping)
        CloseHandle(b.mapping);
    if (b.command_event)
        CloseHandle(b.command_event);
    if (b.reply_event)
        CloseHandle(b.reply_event);
    if (b.callback_event)
        CloseHandle(b.callback_event);
    if (b.callback_done_event)
        CloseHandle(b.callback_done_event);
    return 0;
}

static int
metadata_once(int argc, char **argv)
{
    const char *out_path = argc > 1 ? argv[1] : "pipeasio-wow64-metadata.txt";
    IPipeASIO  *asio     = NULL;
    HRESULT     hr;
    LONG        rc;
    LONG        inputs      = 0;
    LONG        outputs     = 0;
    LONG        min         = 0;
    LONG        max         = 0;
    LONG        preferred   = 0;
    LONG        granularity = 0;
    double      rate        = 0.0;
    FILE       *out;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
        return fail("CoInitializeEx", hr);

    hr = CoCreateInstance(&CLSID_PipeASIO, NULL, CLSCTX_INPROC_SERVER, &CLSID_PipeASIO,
                          (void **)&asio);
    if (FAILED(hr) || !asio)
    {
        CoUninitialize();
        return fail("CoCreateInstance(pipeasio64)", hr);
    }

    rc = asio->lpVtbl->Init(asio, NULL);
    if (rc != 1)
        return fail("pipeasio64 Init", rc);
    rc = asio->lpVtbl->GetChannels(asio, &inputs, &outputs);
    if (rc != 0)
        return fail("pipeasio64 GetChannels", rc);
    rc = asio->lpVtbl->GetBufferSize(asio, &min, &max, &preferred, &granularity);
    if (rc != 0)
        return fail("pipeasio64 GetBufferSize", rc);
    rc = asio->lpVtbl->GetSampleRate(asio, &rate);
    if (rc != 0)
        return fail("pipeasio64 GetSampleRate", rc);

    out = fopen(out_path, "w");
    if (!out)
        return fail("fopen(metadata)", GetLastError());
    fprintf(out, "%ld %ld %ld %ld %ld %ld %.17g\n", (long)inputs, (long)outputs, (long)min,
            (long)max, (long)preferred, (long)granularity, rate);
    fclose(out);

    asio->lpVtbl->Release(asio);
    CoUninitialize();
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc == 7 && strcmp(argv[1], "--bridge") == 0)
        return bridge_loop(argc, argv);
    return metadata_once(argc, argv);
}
