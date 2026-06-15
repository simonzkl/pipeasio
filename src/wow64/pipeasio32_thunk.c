#define WIN32_LEAN_AND_MEAN
#define COBJMACROS

#define WIN32_NO_STATUS
#include <windows.h>
#include <objbase.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winternl.h>
#include <unixlib.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pipeasio_offsets.h"
#include "pipeasio_wow64_unix.h"

#if defined(__i386__)
#define ASIO_THISCALL __attribute__((thiscall))
#else
#define ASIO_THISCALL
#endif

typedef struct IPipeASIO IPipeASIO;

typedef pipeasio_wow64_i64 w_int64_t;

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
    LONG(ASIO_THISCALL *Init)(IPipeASIO *, void *);
    void(ASIO_THISCALL *GetDriverName)(IPipeASIO *, char *);
    LONG(ASIO_THISCALL *GetDriverVersion)(IPipeASIO *);
    void(ASIO_THISCALL *GetErrorMessage)(IPipeASIO *, char *);
    LONG(ASIO_THISCALL *Start)(IPipeASIO *);
    LONG(ASIO_THISCALL *Stop)(IPipeASIO *);
    LONG(ASIO_THISCALL *GetChannels)(IPipeASIO *, LONG *, LONG *);
    LONG(ASIO_THISCALL *GetLatencies)(IPipeASIO *, LONG *, LONG *);
    LONG(ASIO_THISCALL *GetBufferSize)(IPipeASIO *, LONG *, LONG *, LONG *, LONG *);
    LONG(ASIO_THISCALL *CanSampleRate)(IPipeASIO *, double);
    LONG(ASIO_THISCALL *GetSampleRate)(IPipeASIO *, double *);
    LONG(ASIO_THISCALL *SetSampleRate)(IPipeASIO *, double);
    LONG(ASIO_THISCALL *GetClockSources)(IPipeASIO *, void *, LONG *);
    LONG(ASIO_THISCALL *SetClockSource)(IPipeASIO *, LONG);
    LONG(ASIO_THISCALL *GetSamplePosition)(IPipeASIO *, w_int64_t *, w_int64_t *);
    LONG(ASIO_THISCALL *GetChannelInfo)(IPipeASIO *, void *);
    LONG(ASIO_THISCALL *CreateBuffers)(IPipeASIO *, BufferInformation *, LONG, LONG, Callbacks *);
    LONG(ASIO_THISCALL *DisposeBuffers)(IPipeASIO *);
    LONG(ASIO_THISCALL *ControlPanel)(IPipeASIO *);
    LONG(ASIO_THISCALL *Future)(IPipeASIO *, LONG, void *);
    LONG(ASIO_THISCALL *OutputReady)(IPipeASIO *);
} IPipeASIOVtbl;

struct IPipeASIO
{
    const IPipeASIOVtbl *lpVtbl;
    LONG                 ref;
    LONG                 state;
    UINT64               backend;
    LONG                 inputs;
    LONG                 outputs;
    LONG                 min_buffer;
    LONG                 max_buffer;
    LONG                 preferred_buffer;
    LONG                 granularity;
    LONG                 input_latency;
    LONG                 output_latency;
    LONG                 buffer_size;
    double               sample_rate;
    w_int64_t            sample_position;
    w_int64_t            time_stamp;
    Callbacks           *callbacks;
    float               *callback_audio_buffer;
    HANDLE               callback_thread;
    DWORD                callback_thread_id;
    BOOL                 input_active[PIPEASIO_WOW64_MAX_CHANNELS];
    BOOL                 output_active[PIPEASIO_WOW64_MAX_CHANNELS];
    char                 error[PIPEASIO_WOW64_ERROR_MAX];
};

typedef struct PipeASIOFactory
{
    const IClassFactoryVtbl *lpVtbl;
    LONG                     ref;
} PipeASIOFactory;

enum
{
    ThunkLoaded,
    ThunkInitialized,
    ThunkPrepared,
    ThunkRunning
};

static const CLSID CLSID_PipeASIO
        = { 0x2d3ca9e2, 0x1193, 0x4c5d, { 0xb5, 0xfd, 0x38, 0x79, 0x8f, 0x3d, 0xc0, 0x74 } };

static HINSTANCE g_instance;
static BOOL      g_unix_init_attempted;
static NTSTATUS  g_unix_init_status;

static HRESULT STDMETHODCALLTYPE asio_QueryInterface(IPipeASIO *iface, REFIID riid, void **out);
static ULONG STDMETHODCALLTYPE   asio_AddRef(IPipeASIO *iface);
static ULONG STDMETHODCALLTYPE   asio_Release(IPipeASIO *iface);
static LONG ASIO_THISCALL       asio_Init(IPipeASIO *iface, void *sys_ref);
static void ASIO_THISCALL       asio_GetDriverName(IPipeASIO *iface, char *name);
static LONG ASIO_THISCALL       asio_GetDriverVersion(IPipeASIO *iface);
static void ASIO_THISCALL       asio_GetErrorMessage(IPipeASIO *iface, char *string);
static LONG ASIO_THISCALL       asio_Start(IPipeASIO *iface);
static LONG ASIO_THISCALL       asio_Stop(IPipeASIO *iface);
static LONG ASIO_THISCALL       asio_GetChannels(IPipeASIO *iface, LONG *inputs, LONG *outputs);
static LONG ASIO_THISCALL       asio_GetLatencies(IPipeASIO *iface, LONG *input, LONG *output);
static LONG ASIO_THISCALL       asio_GetBufferSize(IPipeASIO *iface, LONG *min, LONG *max,
                                                   LONG *preferred, LONG *granularity);
static LONG ASIO_THISCALL       asio_CanSampleRate(IPipeASIO *iface, double rate);
static LONG ASIO_THISCALL       asio_GetSampleRate(IPipeASIO *iface, double *rate);
static LONG ASIO_THISCALL       asio_SetSampleRate(IPipeASIO *iface, double rate);
static LONG ASIO_THISCALL       asio_GetClockSources(IPipeASIO *iface, void *clocks, LONG *num);
static LONG ASIO_THISCALL       asio_SetClockSource(IPipeASIO *iface, LONG index);
static LONG ASIO_THISCALL       asio_GetSamplePosition(IPipeASIO *iface, w_int64_t *pos,
                                                       w_int64_t *stamp);
static LONG ASIO_THISCALL       asio_GetChannelInfo(IPipeASIO *iface, void *info);
static LONG ASIO_THISCALL       asio_CreateBuffers(IPipeASIO *iface, BufferInformation *buffers,
                                                   LONG channels, LONG size,
                                                   Callbacks *callbacks);
static LONG ASIO_THISCALL       asio_DisposeBuffers(IPipeASIO *iface);
static LONG ASIO_THISCALL       asio_ControlPanel(IPipeASIO *iface);
static LONG ASIO_THISCALL       asio_Future(IPipeASIO *iface, LONG selector, void *opt);
static LONG ASIO_THISCALL       asio_OutputReady(IPipeASIO *iface);
static DWORD WINAPI             callback_thread_proc(void *arg);

static const IPipeASIOVtbl asio_vtbl = {
    asio_QueryInterface, asio_AddRef, asio_Release,
    asio_Init,           asio_GetDriverName, asio_GetDriverVersion,
    asio_GetErrorMessage, asio_Start,        asio_Stop,
    asio_GetChannels,    asio_GetLatencies,  asio_GetBufferSize,
    asio_CanSampleRate,  asio_GetSampleRate, asio_SetSampleRate,
    asio_GetClockSources, asio_SetClockSource, asio_GetSamplePosition,
    asio_GetChannelInfo, asio_CreateBuffers, asio_DisposeBuffers,
    asio_ControlPanel,   asio_Future,        asio_OutputReady
};

static BOOL
ensure_unixlib(IPipeASIO *iface)
{
    if (!g_unix_init_attempted)
    {
        g_unix_init_status = __wine_init_unix_call();
        g_unix_init_attempted = TRUE;
    }
    if (g_unix_init_status)
    {
        snprintf(iface->error, sizeof iface->error, "__wine_init_unix_call failed: 0x%08lx",
                 (unsigned long)g_unix_init_status);
        return FALSE;
    }
    return TRUE;
}

static NTSTATUS
unix_call(IPipeASIO *iface, unsigned int code, void *params)
{
    if (!ensure_unixlib(iface))
        return g_unix_init_status;
    return WINE_UNIX_CALL(code, params);
}

static void
copy_error(IPipeASIO *iface, const char *error)
{
    if (!iface)
        return;
    if (error && error[0])
        lstrcpynA(iface->error, error, sizeof iface->error);
    else
        iface->error[0] = 0;
}

static void
copy_metadata(IPipeASIO *iface, const pipeasio_wow64_metadata *m)
{
    iface->inputs           = m->inputs;
    iface->outputs          = m->outputs;
    iface->min_buffer       = m->min_buffer;
    iface->max_buffer       = m->max_buffer;
    iface->preferred_buffer = m->preferred_buffer;
    iface->granularity      = m->granularity;
    iface->input_latency    = m->input_latency ? m->input_latency : m->preferred_buffer;
    iface->output_latency   = m->output_latency ? m->output_latency : m->preferred_buffer;
    iface->sample_rate      = m->sample_rate;
    iface->sample_position  = m->sample_position;
    iface->time_stamp       = m->time_stamp;
}

static LONG
call_simple(IPipeASIO *iface, unsigned int code)
{
    pipeasio_wow64_simple_params p;
    NTSTATUS                     status;

    if (!ensure_unixlib(iface) || !iface->backend)
        return -1000;

    ZeroMemory(&p, sizeof p);
    p.version = PIPEASIO_WOW64_VERSION;
    p.backend = iface->backend;
    status    = unix_call(iface, code, &p);
    if (status)
    {
        snprintf(iface->error, sizeof iface->error, "unix call %u failed: 0x%08lx", code,
                 (unsigned long)status);
        return -1000;
    }
    copy_error(iface, p.error);
    copy_metadata(iface, &p.metadata);
    return p.asio_status;
}

static UINT32
ptr32(const void *ptr)
{
    return (UINT32)(uintptr_t)ptr;
}

static BOOL
compute_buffer_bytes(IPipeASIO *iface, LONG size, DWORD *out)
{
    size_t bytes;

    if (iface->inputs < 0 || iface->outputs < 0 || (!iface->inputs && !iface->outputs)
        || iface->inputs > PIPEASIO_WOW64_MAX_CHANNELS
        || iface->outputs > PIPEASIO_WOW64_MAX_CHANNELS || size <= 0
        || size > PIPEASIO_WOW64_MAX_BUFFER_FRAMES)
        return FALSE;
    bytes = pipeasio_host_callback_size_bytes((uint32_t)iface->inputs, (uint32_t)iface->outputs,
                                              (size_t)size, sizeof(float));
    if (bytes > UINT32_MAX)
        return FALSE;
    *out = (DWORD)bytes;
    return TRUE;
}

static void
close_backend(IPipeASIO *iface)
{
    if (iface->backend && ensure_unixlib(iface))
    {
        pipeasio_wow64_simple_params p;
        ZeroMemory(&p, sizeof p);
        p.version = PIPEASIO_WOW64_VERSION;
        p.backend = iface->backend;
        unix_call(iface, PIPEASIO_WOW64_CALL_CLOSE, &p);
    }
    iface->backend = 0;
}

static void
free_audio_buffer(IPipeASIO *iface)
{
    if (iface->callback_audio_buffer)
        HeapFree(GetProcessHeap(), 0, iface->callback_audio_buffer);
    iface->callback_audio_buffer = NULL;
}

static void
clear_buffer_pointers(BufferInformation *buffers, LONG channels)
{
    if (!buffers || channels <= 0)
        return;
    if (channels > PIPEASIO_WOW64_MAX_CHANNELS)
        channels = PIPEASIO_WOW64_MAX_CHANNELS;
    for (LONG i = 0; i < channels; i++)
    {
        buffers[i].audioBufferStart = NULL;
        buffers[i].audioBufferEnd   = NULL;
    }
}

static BOOL
is_callback_thread(IPipeASIO *iface)
{
    return iface->callback_thread_id && GetCurrentThreadId() == iface->callback_thread_id;
}

static void
wait_callback_thread(IPipeASIO *iface)
{
    HANDLE thread = iface->callback_thread;

    if (!thread)
        return;
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    iface->callback_thread    = NULL;
    iface->callback_thread_id = 0;
}

static LONG
dispatch_frontend_callback(IPipeASIO *iface, const pipeasio_wow64_callback_params *p)
{
    LONG result = 0;

    if (!iface || !p || !iface->callbacks)
        return 0;

    iface->sample_position = p->sample_position;
    iface->time_stamp      = p->time_stamp;

    switch (p->kind)
    {
    case PIPEASIO_WOW64_CB_TIME_INFO:
    {
        TimeInformation time;
        ZeroMemory(&time, sizeof time);
        time._2            = 1.0;
        time.numSamples    = p->sample_position;
        time.timeStamp     = p->time_stamp;
        time.sampleRate    = iface->sample_rate;
        time.flags         = p->time_flags;
        if (iface->callbacks->swapBuffersWithTimeInfo)
            iface->callbacks->swapBuffersWithTimeInfo(&time, p->index, p->direct);
        else if (iface->callbacks->swapBuffers)
            iface->callbacks->swapBuffers(p->index, p->direct);
        break;
    }
    case PIPEASIO_WOW64_CB_BUFFER_SWITCH:
        if (iface->callbacks->swapBuffers)
            iface->callbacks->swapBuffers(p->index, p->direct);
        break;
    case PIPEASIO_WOW64_CB_SAMPLE_RATE:
        iface->sample_rate = p->sample_rate;
        if (iface->callbacks->sampleRateChanged)
            iface->callbacks->sampleRateChanged(p->sample_rate);
        break;
    case PIPEASIO_WOW64_CB_NOTIFICATION:
        if (iface->callbacks->sendNotification)
            result = iface->callbacks->sendNotification(p->selector, p->value, NULL, NULL);
        break;
    default:
        break;
    }

    return result;
}

static DWORD WINAPI
callback_thread_proc(void *arg)
{
    IPipeASIO *iface = arg;

    for (;;)
    {
        pipeasio_wow64_callback_wait_params wait_params;
        pipeasio_wow64_callback_reply_params reply_params;
        NTSTATUS status;
        LONG     result;

        ZeroMemory(&wait_params, sizeof wait_params);
        wait_params.version = PIPEASIO_WOW64_VERSION;
        wait_params.backend = iface->backend;
        status = unix_call(iface, PIPEASIO_WOW64_CALL_WAIT_CALLBACK, &wait_params);
        if (status || wait_params.shutdown)
            break;
        copy_error(iface, wait_params.error);

        result = dispatch_frontend_callback(iface, &wait_params.callback);

        ZeroMemory(&reply_params, sizeof reply_params);
        reply_params.version = PIPEASIO_WOW64_VERSION;
        reply_params.backend = iface->backend;
        reply_params.result  = result;
        status = unix_call(iface, PIPEASIO_WOW64_CALL_REPLY_CALLBACK, &reply_params);
        if (status)
            break;
        copy_error(iface, reply_params.error);
    }

    return 0;
}

static HRESULT STDMETHODCALLTYPE
asio_QueryInterface(IPipeASIO *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &CLSID_PipeASIO))
    {
        *out = iface;
        asio_AddRef(iface);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
asio_AddRef(IPipeASIO *iface)
{
    return InterlockedIncrement(&iface->ref);
}

static ULONG STDMETHODCALLTYPE
asio_Release(IPipeASIO *iface)
{
    ULONG ref = InterlockedDecrement(&iface->ref);
    if (!ref)
    {
        if (is_callback_thread(iface))
        {
            /* A final Release from inside bufferSwitch cannot safely join,
             * close the backend, or free iface while this thread is still
             * unwinding.  Treat it as unsupported and leak instead of
             * deadlocking or using freed state. */
            snprintf(iface->error, sizeof iface->error,
                     "teardown from ASIO callback thread is unsupported");
            iface->callbacks = NULL;
            return ref;
        }
        if (iface->state == ThunkRunning)
            asio_Stop(iface);
        if (iface->state == ThunkPrepared)
            asio_DisposeBuffers(iface);
        close_backend(iface);
        free_audio_buffer(iface);
        HeapFree(GetProcessHeap(), 0, iface);
    }
    return ref;
}

static LONG ASIO_THISCALL
asio_Init(IPipeASIO *iface, void *sys_ref)
{
    pipeasio_wow64_init_params p;
    NTSTATUS                   status;

    (void)sys_ref;

    if (iface->state >= ThunkInitialized)
        return 1;
    ZeroMemory(&p, sizeof p);
    p.version = PIPEASIO_WOW64_VERSION;
    status    = unix_call(iface, PIPEASIO_WOW64_CALL_INIT, &p);
    if (status)
    {
        snprintf(iface->error, sizeof iface->error, "init unix call failed: 0x%08lx",
                 (unsigned long)status);
        return 0;
    }
    copy_error(iface, p.error);
    if (p.asio_status != 1 || !p.backend)
        return 0;

    iface->backend = p.backend;
    copy_metadata(iface, &p.metadata);
    iface->state = ThunkInitialized;
    return 1;
}

static void ASIO_THISCALL
asio_GetDriverName(IPipeASIO *iface, char *name)
{
    (void)iface;
    lstrcpyA(name, "PipeASIO");
}

static LONG ASIO_THISCALL
asio_GetDriverVersion(IPipeASIO *iface)
{
    (void)iface;
    return PIPEASIO_WOW64_DRIVER_VERSION;
}

static void ASIO_THISCALL
asio_GetErrorMessage(IPipeASIO *iface, char *string)
{
    if (iface->error[0])
        lstrcpynA(string, iface->error, 124);
    else
        lstrcpyA(string, "PipeASIO WoW64 thunk");
}

static LONG ASIO_THISCALL
asio_Start(IPipeASIO *iface)
{
    LONG rc;
    if (iface->state != ThunkPrepared)
        return -1000;
    rc = call_simple(iface, PIPEASIO_WOW64_CALL_START);
    if (rc == 0)
        iface->state = ThunkRunning;
    return rc;
}

static LONG ASIO_THISCALL
asio_Stop(IPipeASIO *iface)
{
    LONG rc;
    if (iface->state != ThunkRunning)
        return -1000;
    rc = call_simple(iface, PIPEASIO_WOW64_CALL_STOP);
    if (rc == 0)
        iface->state = ThunkPrepared;
    return rc;
}

static LONG ASIO_THISCALL
asio_GetChannels(IPipeASIO *iface, LONG *inputs, LONG *outputs)
{
    if (!inputs || !outputs)
        return -998;
    *inputs  = iface->inputs;
    *outputs = iface->outputs;
    return iface->state >= ThunkInitialized ? 0 : -1000;
}

static LONG ASIO_THISCALL
asio_GetLatencies(IPipeASIO *iface, LONG *input, LONG *output)
{
    if (!input || !output)
        return -998;
    *input  = iface->input_latency;
    *output = iface->output_latency;
    return iface->state >= ThunkInitialized ? 0 : -1000;
}

static LONG ASIO_THISCALL
asio_GetBufferSize(IPipeASIO *iface, LONG *min, LONG *max, LONG *preferred, LONG *granularity)
{
    if (!min || !max || !preferred || !granularity)
        return -998;
    *min         = iface->min_buffer;
    *max         = iface->max_buffer;
    *preferred   = iface->preferred_buffer;
    *granularity = iface->granularity;
    return iface->state >= ThunkInitialized ? 0 : -1000;
}

static LONG ASIO_THISCALL
asio_CanSampleRate(IPipeASIO *iface, double rate)
{
    return iface->state >= ThunkInitialized && rate == iface->sample_rate ? 0 : -995;
}

static LONG ASIO_THISCALL
asio_GetSampleRate(IPipeASIO *iface, double *rate)
{
    if (!rate)
        return -998;
    *rate = iface->sample_rate;
    return iface->state >= ThunkInitialized ? 0 : -1000;
}

static LONG ASIO_THISCALL
asio_SetSampleRate(IPipeASIO *iface, double rate)
{
    return iface->state >= ThunkInitialized && rate == iface->sample_rate ? 0 : -995;
}

static LONG ASIO_THISCALL
asio_GetClockSources(IPipeASIO *iface, void *clocks, LONG *num)
{
    LONG *lclocks = clocks;
    (void)iface;
    if (!clocks || !num)
        return -998;
    *lclocks++ = 0;
    *lclocks++ = -1;
    *lclocks++ = -1;
    *lclocks++ = 1;
    lstrcpyA((char *)lclocks, "Internal");
    *num = 1;
    return 0;
}

static LONG ASIO_THISCALL
asio_SetClockSource(IPipeASIO *iface, LONG index)
{
    (void)iface;
    return index == 0 ? 0 : -1000;
}

static LONG ASIO_THISCALL
asio_GetSamplePosition(IPipeASIO *iface, w_int64_t *pos, w_int64_t *stamp)
{
    pipeasio_wow64_position_params p;
    NTSTATUS                       status;

    if (!pos || !stamp)
        return -998;
    if (iface->state < ThunkInitialized || !iface->backend)
        return -1000;

    ZeroMemory(&p, sizeof p);
    p.version = PIPEASIO_WOW64_VERSION;
    p.backend = iface->backend;
    status    = unix_call(iface, PIPEASIO_WOW64_CALL_GET_SAMPLE_POSITION, &p);
    if (status)
    {
        snprintf(iface->error, sizeof iface->error, "position unix call failed: 0x%08lx",
                 (unsigned long)status);
        return -1000;
    }
    copy_error(iface, p.error);
    iface->sample_position = p.sample_position;
    iface->time_stamp      = p.time_stamp;
    *pos                   = iface->sample_position;
    *stamp                 = iface->time_stamp;
    return p.asio_status;
}

static LONG ASIO_THISCALL
asio_GetChannelInfo(IPipeASIO *iface, void *info)
{
    LONG *linfo;
    LONG  channel;
    LONG  is_input;
    BOOL  active;
    char  name[32] = { 0 };

    if (!info)
        return -998;
    linfo    = info;
    channel  = *linfo++;
    is_input = *linfo++;
    if (channel < 0
        || (is_input ? channel >= iface->inputs || channel >= PIPEASIO_WOW64_MAX_CHANNELS
                     : channel >= iface->outputs || channel >= PIPEASIO_WOW64_MAX_CHANNELS))
        return -998;
    active = is_input ? iface->input_active[channel] : iface->output_active[channel];
    *linfo++ = active;
    *linfo++ = 0;
    *linfo++ = 19;
    snprintf(name, sizeof name, "%s_%ld", is_input ? "in" : "out", (long)channel + 1);
    memcpy(linfo, name, sizeof name);
    return 0;
}

static LONG ASIO_THISCALL
asio_CreateBuffers(IPipeASIO *iface, BufferInformation *buffers, LONG channels, LONG size,
                   Callbacks *callbacks)
{
    pipeasio_wow64_create_buffers_params p;
    NTSTATUS                             status;
    DWORD                                bytes;
    float                               *base;

    if (iface->state != ThunkInitialized)
        return -1000;
    if (!buffers || !callbacks || channels <= 0 || channels > PIPEASIO_WOW64_MAX_CHANNELS)
        return -997;
    clear_buffer_pointers(buffers, channels);
    if (!compute_buffer_bytes(iface, size, &bytes))
        return -997;

    free_audio_buffer(iface);
    base = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
    if (!base)
        return -994;

    ZeroMemory(iface->input_active, sizeof iface->input_active);
    ZeroMemory(iface->output_active, sizeof iface->output_active);
    ZeroMemory(&p, sizeof p);
    p.version           = PIPEASIO_WOW64_VERSION;
    p.backend           = iface->backend;
    p.pe_iface          = ptr32(iface);
    p.buffer_base       = ptr32(base);
    p.buffer_bytes      = bytes;
    p.time_info_mode    = callbacks->sendNotification
                                  && callbacks->sendNotification(7, 0, NULL, NULL)
                          ? 1
                          : 0;
    p.num_channels      = channels;
    p.buffer_size       = size;

    for (LONG i = 0; i < channels; i++)
    {
        LONG   ch       = buffers[i].channelNumber;
        LONG   is_input = buffers[i].isInputType ? 1 : 0;
        float *start;
        float *end;

        if (ch < 0
            || (is_input ? ch >= iface->inputs || ch >= PIPEASIO_WOW64_MAX_CHANNELS
                         : ch >= iface->outputs || ch >= PIPEASIO_WOW64_MAX_CHANNELS))
        {
            clear_buffer_pointers(buffers, channels);
            ZeroMemory(iface->input_active, sizeof iface->input_active);
            ZeroMemory(iface->output_active, sizeof iface->output_active);
            HeapFree(GetProcessHeap(), 0, base);
            return -997;
        }

        size_t channel_offset = is_input ? pipeasio_host_input_offset_samples((uint32_t)ch,
                                                                              (size_t)size)
                                         : pipeasio_host_output_offset_samples(
                                                   (uint32_t)ch, (uint32_t)iface->inputs,
                                                   (size_t)size);

        start = base + channel_offset + pipeasio_host_half_offset_samples(0, (size_t)size);
        end   = base + channel_offset + pipeasio_host_half_offset_samples(1, (size_t)size);
        buffers[i].audioBufferStart = start;
        buffers[i].audioBufferEnd   = end;
        p.buffers[i].is_input       = is_input;
        p.buffers[i].channel        = ch;
        if (is_input)
            iface->input_active[ch] = TRUE;
        else
            iface->output_active[ch] = TRUE;
    }

    iface->callbacks             = callbacks;
    iface->callback_audio_buffer = base;

    status = unix_call(iface, PIPEASIO_WOW64_CALL_CREATE_BUFFERS, &p);
    if (status || p.asio_status != 0)
    {
        if (status)
            snprintf(iface->error, sizeof iface->error, "create buffers unix call failed: 0x%08lx",
                     (unsigned long)status);
        else
            copy_error(iface, p.error);
        iface->callbacks = NULL;
        ZeroMemory(iface->input_active, sizeof iface->input_active);
        ZeroMemory(iface->output_active, sizeof iface->output_active);
        clear_buffer_pointers(buffers, channels);
        free_audio_buffer(iface);
        return status ? -1000 : p.asio_status;
    }

    copy_error(iface, p.error);
    copy_metadata(iface, &p.metadata);
    iface->callback_thread = CreateThread(NULL, 0, callback_thread_proc, iface, 0,
                                          &iface->callback_thread_id);
    if (!iface->callback_thread)
    {
        LONG dispose_rc;

        snprintf(iface->error, sizeof iface->error, "callback thread creation failed");
        dispose_rc = call_simple(iface, PIPEASIO_WOW64_CALL_DISPOSE_BUFFERS);
        (void)dispose_rc;
        iface->callbacks = NULL;
        ZeroMemory(iface->input_active, sizeof iface->input_active);
        ZeroMemory(iface->output_active, sizeof iface->output_active);
        clear_buffer_pointers(buffers, channels);
        free_audio_buffer(iface);
        return -994;
    }
    iface->buffer_size = size;
    iface->state       = ThunkPrepared;
    return 0;
}

static LONG ASIO_THISCALL
asio_DisposeBuffers(IPipeASIO *iface)
{
    LONG rc;
    if (is_callback_thread(iface))
    {
        snprintf(iface->error, sizeof iface->error,
                 "teardown from ASIO callback thread is unsupported");
        return -997;
    }
    if (iface->state == ThunkRunning)
        asio_Stop(iface);
    if (iface->state != ThunkPrepared)
        return -1000;
    rc = call_simple(iface, PIPEASIO_WOW64_CALL_DISPOSE_BUFFERS);
    wait_callback_thread(iface);
    if (rc == 0)
    {
        iface->callbacks = NULL;
        ZeroMemory(iface->input_active, sizeof iface->input_active);
        ZeroMemory(iface->output_active, sizeof iface->output_active);
        free_audio_buffer(iface);
        iface->state = ThunkInitialized;
    }
    return rc;
}

static LONG ASIO_THISCALL
asio_ControlPanel(IPipeASIO *iface)
{
    (void)iface;
    return 0;
}

static LONG ASIO_THISCALL
asio_Future(IPipeASIO *iface, LONG selector, void *opt)
{
    (void)iface;
    (void)opt;
    switch (selector)
    {
    case 3:
    case 0x23111961:
    case 0x23111983:
    case 0x23112004:
        return -1000;
    case 10:
        return 0x3f4847a0;
    default:
        return -998;
    }
}

static LONG ASIO_THISCALL
asio_OutputReady(IPipeASIO *iface)
{
    (void)iface;
    return -1000;
}

static HRESULT STDMETHODCALLTYPE
cf_QueryInterface(IClassFactory *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory))
    {
        *out = iface;
        IClassFactory_AddRef(iface);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
cf_AddRef(IClassFactory *iface)
{
    PipeASIOFactory *factory = (PipeASIOFactory *)iface;
    return InterlockedIncrement(&factory->ref);
}

static ULONG STDMETHODCALLTYPE
cf_Release(IClassFactory *iface)
{
    PipeASIOFactory *factory = (PipeASIOFactory *)iface;
    return InterlockedDecrement(&factory->ref);
}

static HRESULT STDMETHODCALLTYPE
cf_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **out)
{
    IPipeASIO *obj;
    HRESULT   hr;
    (void)iface;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    if (!out)
        return E_POINTER;
    *out = NULL;
    obj = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *obj);
    if (!obj)
        return E_OUTOFMEMORY;
    obj->lpVtbl = &asio_vtbl;
    obj->ref    = 1;
    obj->state  = ThunkLoaded;
    hr          = asio_QueryInterface(obj, riid, out);
    asio_Release(obj);
    return hr;
}

static HRESULT STDMETHODCALLTYPE
cf_LockServer(IClassFactory *iface, BOOL lock)
{
    (void)iface;
    (void)lock;
    return S_OK;
}

static const IClassFactoryVtbl cf_vtbl
        = { cf_QueryInterface, cf_AddRef, cf_Release, cf_CreateInstance, cf_LockServer };
static PipeASIOFactory factory = { &cf_vtbl, 1 };

BOOL WINAPI
DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
        g_instance = instance;
    return TRUE;
}

__declspec(dllexport) HRESULT WINAPI
DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!IsEqualCLSID(clsid, &CLSID_PipeASIO))
        return CLASS_E_CLASSNOTAVAILABLE;
    return cf_QueryInterface((IClassFactory *)&factory, riid, out);
}

__declspec(dllexport) HRESULT WINAPI
DllCanUnloadNow(void)
{
    return S_FALSE;
}

static LONG
register_string_value(HKEY root, const char *key_name, const char *value_name, const char *value)
{
    HKEY key;
    LONG rc;

    rc = RegCreateKeyExA(root, key_name, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &key, NULL);
    if (rc != ERROR_SUCCESS)
        return rc;
    rc = RegSetValueExA(key, value_name, 0, REG_SZ, (const BYTE *)value,
                        (DWORD)strlen(value) + 1);
    RegCloseKey(key);
    return rc;
}

static LONG
delete_tree_allow_missing(HKEY root, const char *key_name)
{
    LONG rc = RegDeleteTreeA(root, key_name);
    return rc == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : rc;
}

__declspec(dllexport) HRESULT WINAPI
DllRegisterServer(void)
{
    static const char clsid_key[]
            = "CLSID\\{2D3CA9E2-1193-4C5D-B5FD-38798F3DC074}\\InprocServer32";
    static const char asio_key[]        = "Software\\ASIO\\PipeASIO";
    static const char dll_name[]        = "pipeasio32.dll";
    static const char threading_model[] = "Apartment";
    static const char wine_clsid[]      = "{2D3CA9E2-1193-4C5D-B5FD-38798F3DC074}";
    static const char wine_desc[]       = "PipeASIO Driver";
    LONG              rc;

    rc = register_string_value(HKEY_CLASSES_ROOT, clsid_key, NULL, dll_name);
    if (rc == ERROR_SUCCESS)
        rc = register_string_value(HKEY_CLASSES_ROOT, clsid_key, "ThreadingModel",
                                   threading_model);
    if (rc == ERROR_SUCCESS)
        rc = register_string_value(HKEY_LOCAL_MACHINE, asio_key, "CLSID", wine_clsid);
    if (rc == ERROR_SUCCESS)
        rc = register_string_value(HKEY_LOCAL_MACHINE, asio_key, "Description", wine_desc);
    return rc == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(rc);
}

__declspec(dllexport) HRESULT WINAPI
DllUnregisterServer(void)
{
    LONG rc;

    rc = delete_tree_allow_missing(HKEY_CLASSES_ROOT,
                                   "CLSID\\{2D3CA9E2-1193-4C5D-B5FD-38798F3DC074}");
    if (rc == ERROR_SUCCESS)
        rc = delete_tree_allow_missing(HKEY_LOCAL_MACHINE, "Software\\ASIO\\PipeASIO");
    return rc == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(rc);
}
