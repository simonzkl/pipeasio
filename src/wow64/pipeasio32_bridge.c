#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pipeasio_wow64_ipc.h"

#if defined(__i386__)
#define ASIO_THISCALL __attribute__((thiscall))
#else
#define ASIO_THISCALL
#endif

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
    Callbacks           *callbacks;
    BOOL                 input_active[PIPEASIO_WOW64_MAX_CHANNELS];
    BOOL                 output_active[PIPEASIO_WOW64_MAX_CHANNELS];

    HANDLE mapping;
    HANDLE command_event;
    HANDLE reply_event;
    HANDLE callback_event;
    HANDLE callback_done_event;
    HANDLE callback_stop_event;
    HANDLE callback_thread;
    HANDLE helper_process;
    pipeasio_wow64_shared *shared;

    char mapping_name[PIPEASIO_WOW64_NAME_MAX];
    char command_event_name[PIPEASIO_WOW64_NAME_MAX];
    char reply_event_name[PIPEASIO_WOW64_NAME_MAX];
    char callback_event_name[PIPEASIO_WOW64_NAME_MAX];
    char callback_done_event_name[PIPEASIO_WOW64_NAME_MAX];
};

typedef struct TestFactory
{
    const IClassFactoryVtbl *lpVtbl;
    LONG                     ref;
} TestFactory;

enum
{
    BridgeLoaded,
    BridgeInitialized,
    BridgePrepared,
    BridgeRunning
};

static const CLSID CLSID_PipeASIO
        = { 0x2d3ca9e2, 0x1193, 0x4c5d, { 0xb5, 0xfd, 0x38, 0x79, 0x8f, 0x3d, 0xc0, 0x74 } };

static HINSTANCE g_instance;

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
candidate_exists(const char *path)
{
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL
resolve_helper_path(char *path, DWORD path_size)
{
    const char *env = getenv("PIPEASIO_WOW64_HELPER");
    if (env && env[0])
    {
        lstrcpynA(path, env, path_size);
        return TRUE;
    }

    static const char *candidates[] = {
        "C:\\windows\\sysnative\\pipeasio64-helper.exe",
        "C:\\windows\\system32\\pipeasio64-helper.exe",
        "C:\\windows\\syswow64\\pipeasio64-helper.exe",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
    {
        if (candidate_exists(candidates[i]))
        {
            lstrcpynA(path, candidates[i], path_size);
            return TRUE;
        }
    }
    return FALSE;
}

static void
copy_metadata(IPipeASIO *iface)
{
    pipeasio_wow64_shared *s = iface->shared;
    iface->inputs           = s->inputs;
    iface->outputs          = s->outputs;
    iface->min_buffer       = s->min_buffer;
    iface->max_buffer       = s->max_buffer;
    iface->preferred_buffer = s->preferred_buffer;
    iface->granularity      = s->granularity;
    iface->input_latency    = s->input_latency ? s->input_latency : s->preferred_buffer;
    iface->output_latency   = s->output_latency ? s->output_latency : s->preferred_buffer;
    iface->sample_rate      = s->sample_rate;
}

static LONG
send_command(IPipeASIO *iface, LONG command)
{
    LONG generation;
    if (!iface->shared || !iface->command_event || !iface->reply_event)
        return -1000;

    generation                         = iface->shared->command_generation + 1;
    iface->shared->command_status      = -1000;
    iface->shared->command             = command;
    iface->shared->command_generation  = generation;
    ResetEvent(iface->reply_event);
    SetEvent(iface->command_event);
    if (WaitForSingleObject(iface->reply_event, INFINITE) != WAIT_OBJECT_0)
        return -1000;
    if (iface->shared->command_done_generation != generation)
        return -1000;
    copy_metadata(iface);
    return iface->shared->command_status;
}

static DWORD WINAPI
callback_thread_proc(void *arg)
{
    IPipeASIO *iface = arg;
    HANDLE     waits[2];

    waits[0] = iface->callback_event;
    waits[1] = iface->callback_stop_event;

    for (;;)
    {
        DWORD waited = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (waited == WAIT_OBJECT_0 + 1 || waited == WAIT_FAILED)
            break;
        if (waited != WAIT_OBJECT_0)
            continue;

        pipeasio_wow64_shared *s = iface->shared;
        LONG kind                = s->callback_kind;
        LONG result              = 0;

        if (iface->callbacks)
        {
            if (kind == PIPEASIO_WOW64_CB_TIME_INFO)
            {
                TimeInformation time;
                ZeroMemory(&time, sizeof time);
                time._2            = 1.0;
                time.numSamples.hi = s->callback_sample_position.hi;
                time.numSamples.lo = s->callback_sample_position.lo;
                time.timeStamp.hi  = s->callback_time_stamp.hi;
                time.timeStamp.lo  = s->callback_time_stamp.lo;
                time.sampleRate    = s->sample_rate;
                time.flags         = s->callback_time_flags;
                if (iface->callbacks->swapBuffersWithTimeInfo)
                    iface->callbacks->swapBuffersWithTimeInfo(&time, s->callback_index,
                                                              s->callback_direct);
                else if (iface->callbacks->swapBuffers)
                    iface->callbacks->swapBuffers(s->callback_index, s->callback_direct);
            }
            else if (kind == PIPEASIO_WOW64_CB_BUFFER_SWITCH)
            {
                if (iface->callbacks->swapBuffers)
                    iface->callbacks->swapBuffers(s->callback_index, s->callback_direct);
            }
            else if (kind == PIPEASIO_WOW64_CB_SAMPLE_RATE)
            {
                iface->sample_rate = s->callback_sample_rate;
                if (iface->callbacks->sampleRateChanged)
                    iface->callbacks->sampleRateChanged(s->callback_sample_rate);
            }
            else if (kind == PIPEASIO_WOW64_CB_NOTIFICATION)
            {
                if (iface->callbacks->sendNotification)
                    result = iface->callbacks->sendNotification(s->callback_selector,
                                                                s->callback_value, NULL, NULL);
            }
        }

        s->callback_result          = result;
        s->callback_done_generation = s->callback_generation;
        SetEvent(iface->callback_done_event);
    }

    return 0;
}

static BOOL
ensure_callback_thread(IPipeASIO *iface)
{
    if (iface->callback_thread)
        return TRUE;
    iface->callback_stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!iface->callback_stop_event)
        return FALSE;
    iface->callback_thread = CreateThread(NULL, 0, callback_thread_proc, iface, 0, NULL);
    return iface->callback_thread != NULL;
}

static void
close_bridge(IPipeASIO *iface)
{
    if (iface->shared && iface->helper_process)
    {
        send_command(iface, PIPEASIO_WOW64_CMD_EXIT);
        WaitForSingleObject(iface->helper_process, 5000);
    }

    if (iface->callback_stop_event)
        SetEvent(iface->callback_stop_event);
    if (iface->callback_thread)
    {
        WaitForSingleObject(iface->callback_thread, INFINITE);
        CloseHandle(iface->callback_thread);
        iface->callback_thread = NULL;
    }

    if (iface->helper_process)
    {
        CloseHandle(iface->helper_process);
        iface->helper_process = NULL;
    }
    if (iface->shared)
    {
        UnmapViewOfFile(iface->shared);
        iface->shared = NULL;
    }
    if (iface->mapping)
    {
        CloseHandle(iface->mapping);
        iface->mapping = NULL;
    }
    if (iface->command_event)
        CloseHandle(iface->command_event);
    if (iface->reply_event)
        CloseHandle(iface->reply_event);
    if (iface->callback_event)
        CloseHandle(iface->callback_event);
    if (iface->callback_done_event)
        CloseHandle(iface->callback_done_event);
    if (iface->callback_stop_event)
        CloseHandle(iface->callback_stop_event);
    iface->command_event       = NULL;
    iface->reply_event         = NULL;
    iface->callback_event      = NULL;
    iface->callback_done_event = NULL;
    iface->callback_stop_event = NULL;
}

static BOOL
start_helper(IPipeASIO *iface)
{
    char                helper[MAX_PATH];
    char                cmd[MAX_PATH + PIPEASIO_WOW64_NAME_MAX * 5 + 96];
    STARTUPINFOA        si;
    PROCESS_INFORMATION pi;
    DWORD               pid = GetCurrentProcessId();

    if (iface->helper_process)
        return TRUE;
    if (!resolve_helper_path(helper, sizeof helper))
        return FALSE;

    snprintf(iface->mapping_name, sizeof iface->mapping_name, "Local\\PipeASIOWoW64-%lu-%p-map",
             (unsigned long)pid, iface);
    snprintf(iface->command_event_name, sizeof iface->command_event_name,
             "Local\\PipeASIOWoW64-%lu-%p-cmd", (unsigned long)pid, iface);
    snprintf(iface->reply_event_name, sizeof iface->reply_event_name,
             "Local\\PipeASIOWoW64-%lu-%p-reply", (unsigned long)pid, iface);
    snprintf(iface->callback_event_name, sizeof iface->callback_event_name,
             "Local\\PipeASIOWoW64-%lu-%p-cb", (unsigned long)pid, iface);
    snprintf(iface->callback_done_event_name, sizeof iface->callback_done_event_name,
             "Local\\PipeASIOWoW64-%lu-%p-cbdone", (unsigned long)pid, iface);

    iface->mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                        sizeof(pipeasio_wow64_shared), iface->mapping_name);
    iface->command_event       = CreateEventA(NULL, FALSE, FALSE, iface->command_event_name);
    iface->reply_event         = CreateEventA(NULL, FALSE, FALSE, iface->reply_event_name);
    iface->callback_event      = CreateEventA(NULL, FALSE, FALSE, iface->callback_event_name);
    iface->callback_done_event = CreateEventA(NULL, FALSE, FALSE, iface->callback_done_event_name);
    if (!iface->mapping || !iface->command_event || !iface->reply_event || !iface->callback_event
        || !iface->callback_done_event)
        return FALSE;

    iface->shared = MapViewOfFile(iface->mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                                  sizeof(pipeasio_wow64_shared));
    if (!iface->shared)
        return FALSE;
    ZeroMemory(iface->shared, sizeof(*iface->shared));
    iface->shared->magic   = PIPEASIO_WOW64_MAGIC;
    iface->shared->version = PIPEASIO_WOW64_VERSION;

    snprintf(cmd, sizeof cmd, "\"%s\" --bridge \"%s\" \"%s\" \"%s\" \"%s\" \"%s\"", helper,
             iface->mapping_name, iface->command_event_name, iface->reply_event_name,
             iface->callback_event_name, iface->callback_done_event_name);

    ZeroMemory(&si, sizeof si);
    ZeroMemory(&pi, sizeof pi);
    si.cb          = sizeof si;
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return FALSE;
    CloseHandle(pi.hThread);
    iface->helper_process = pi.hProcess;
    return TRUE;
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
        close_bridge(iface);
        HeapFree(GetProcessHeap(), 0, iface);
    }
    return ref;
}

static LONG ASIO_THISCALL
asio_Init(IPipeASIO *iface, void *sys_ref)
{
    (void)sys_ref;
    if (iface->state >= BridgeInitialized)
        return 1;
    if (!start_helper(iface))
        return 0;
    if (send_command(iface, PIPEASIO_WOW64_CMD_INIT) != 0)
        return 0;
    iface->state = BridgeInitialized;
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
    return 92;
}

static void ASIO_THISCALL
asio_GetErrorMessage(IPipeASIO *iface, char *string)
{
    if (iface->shared && iface->shared->error[0])
        lstrcpynA(string, iface->shared->error, 124);
    else
        lstrcpyA(string, "WoW64 bridge");
}

static LONG ASIO_THISCALL
asio_Start(IPipeASIO *iface)
{
    LONG rc;
    if (iface->state != BridgePrepared)
        return -1000;
    rc = send_command(iface, PIPEASIO_WOW64_CMD_START);
    if (rc == 0)
        iface->state = BridgeRunning;
    return rc;
}

static LONG ASIO_THISCALL
asio_Stop(IPipeASIO *iface)
{
    LONG rc;
    if (iface->state != BridgeRunning)
        return -1000;
    rc = send_command(iface, PIPEASIO_WOW64_CMD_STOP);
    if (rc == 0)
        iface->state = BridgePrepared;
    return rc;
}

static LONG ASIO_THISCALL
asio_GetChannels(IPipeASIO *iface, LONG *inputs, LONG *outputs)
{
    if (!inputs || !outputs)
        return -998;
    *inputs  = iface->inputs;
    *outputs = iface->outputs;
    return iface->state >= BridgeInitialized ? 0 : -1000;
}

static LONG ASIO_THISCALL
asio_GetLatencies(IPipeASIO *iface, LONG *input, LONG *output)
{
    if (!input || !output)
        return -998;
    *input  = iface->input_latency;
    *output = iface->output_latency;
    return iface->state >= BridgeInitialized ? 0 : -1000;
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
    return iface->state >= BridgeInitialized ? 0 : -1000;
}

static LONG ASIO_THISCALL
asio_CanSampleRate(IPipeASIO *iface, double rate)
{
    return iface->state >= BridgeInitialized && rate == iface->sample_rate ? 0 : -995;
}

static LONG ASIO_THISCALL
asio_GetSampleRate(IPipeASIO *iface, double *rate)
{
    if (!rate)
        return -998;
    *rate = iface->sample_rate;
    return iface->state >= BridgeInitialized ? 0 : -1000;
}

static LONG ASIO_THISCALL
asio_SetSampleRate(IPipeASIO *iface, double rate)
{
    return iface->state >= BridgeInitialized && rate == iface->sample_rate ? 0 : -995;
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
    if (!pos || !stamp)
        return -998;
    if (iface->shared)
    {
        pos->hi   = iface->shared->sample_position.hi;
        pos->lo   = iface->shared->sample_position.lo;
        stamp->hi = iface->shared->time_stamp.hi;
        stamp->lo = iface->shared->time_stamp.lo;
    }
    else
    {
        pos->hi = pos->lo = stamp->hi = stamp->lo = 0;
    }
    return iface->state >= BridgeInitialized ? 0 : -1000;
}

static LONG ASIO_THISCALL
asio_GetChannelInfo(IPipeASIO *iface, void *info)
{
    LONG *linfo;
    LONG  channel;
    LONG  is_input;
    BOOL  active;
    char  name[32];

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
    LONG rc;
    if (iface->state != BridgeInitialized)
        return -1000;
    if (!buffers || !callbacks || channels <= 0 || channels > PIPEASIO_WOW64_MAX_CHANNELS
        || size <= 0 || size > PIPEASIO_WOW64_MAX_BUFFER_FRAMES)
        return -997;
    if (!ensure_callback_thread(iface))
        return -994;

    ZeroMemory(iface->input_active, sizeof iface->input_active);
    ZeroMemory(iface->output_active, sizeof iface->output_active);
    ZeroMemory(iface->shared->buffers, sizeof iface->shared->buffers);
    ZeroMemory(iface->shared->samples, sizeof iface->shared->samples);
    iface->shared->num_channels = channels;
    iface->shared->buffer_size  = size;
    iface->callbacks            = callbacks;

    for (LONG i = 0; i < channels; i++)
    {
        LONG ch = buffers[i].channelNumber;
        if (ch < 0
            || (buffers[i].isInputType
                        ? ch >= iface->inputs || ch >= PIPEASIO_WOW64_MAX_CHANNELS
                        : ch >= iface->outputs || ch >= PIPEASIO_WOW64_MAX_CHANNELS))
            return -997;
        iface->shared->buffers[i].is_input      = buffers[i].isInputType ? 1 : 0;
        iface->shared->buffers[i].channel       = ch;
        iface->shared->buffers[i].sample_offset = pipeasio_wow64_buffer_offset(i, 0);
        if (buffers[i].isInputType)
            iface->input_active[ch] = TRUE;
        else
            iface->output_active[ch] = TRUE;
    }

    rc = send_command(iface, PIPEASIO_WOW64_CMD_CREATE_BUFFERS);
    if (rc != 0)
    {
        iface->callbacks = NULL;
        return rc;
    }

    for (LONG i = 0; i < channels; i++)
    {
        buffers[i].audioBufferStart = &iface->shared->samples[pipeasio_wow64_buffer_offset(i, 0)];
        buffers[i].audioBufferEnd   = &iface->shared->samples[pipeasio_wow64_buffer_offset(i, 1)];
    }
    iface->buffer_size = size;
    iface->state       = BridgePrepared;
    return 0;
}

static LONG ASIO_THISCALL
asio_DisposeBuffers(IPipeASIO *iface)
{
    LONG rc;
    if (iface->state == BridgeRunning)
        asio_Stop(iface);
    if (iface->state != BridgePrepared)
        return -1000;
    rc = send_command(iface, PIPEASIO_WOW64_CMD_DISPOSE_BUFFERS);
    if (rc == 0)
    {
        iface->callbacks = NULL;
        ZeroMemory(iface->input_active, sizeof iface->input_active);
        ZeroMemory(iface->output_active, sizeof iface->output_active);
        iface->state = BridgeInitialized;
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
    TestFactory *factory = (TestFactory *)iface;
    return InterlockedIncrement(&factory->ref);
}

static ULONG STDMETHODCALLTYPE
cf_Release(IClassFactory *iface)
{
    TestFactory *factory = (TestFactory *)iface;
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
    obj->state  = BridgeLoaded;
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
static TestFactory factory = { &cf_vtbl, 1 };

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

__declspec(dllexport) HRESULT WINAPI
DllRegisterServer(void)
{
    static const char key_name[]
            = "CLSID\\{2D3CA9E2-1193-4C5D-B5FD-38798F3DC074}\\InprocServer32";
    static const char threading_model[] = "Apartment";
    char              path[MAX_PATH];
    HKEY              key;
    LONG              rc;

    if (!GetModuleFileNameA(g_instance, path, sizeof path))
        return E_FAIL;
    rc = RegCreateKeyExA(HKEY_CLASSES_ROOT, key_name, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL,
                         &key, NULL);
    if (rc != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(rc);
    rc = RegSetValueExA(key, NULL, 0, REG_SZ, (const BYTE *)path, lstrlenA(path) + 1);
    if (rc == ERROR_SUCCESS)
        rc = RegSetValueExA(key, "ThreadingModel", 0, REG_SZ, (const BYTE *)threading_model,
                            sizeof threading_model);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(rc);
}

__declspec(dllexport) HRESULT WINAPI
DllUnregisterServer(void)
{
    LONG rc = RegDeleteTreeA(HKEY_CLASSES_ROOT,
                             "CLSID\\{2D3CA9E2-1193-4C5D-B5FD-38798F3DC074}");
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(rc);
}
