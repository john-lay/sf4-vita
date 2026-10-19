/*
 * sf4-vita: a minimal OpenSL ES for Cricket Audio.
 *
 * Cricket (Cki::GraphOutputAndroid) creates an engine, an output mix and one
 * audio player: PCM 16-bit stereo from an Android simple buffer queue (two
 * buffers) into the output mix. It enqueues a silent buffer, sets PLAYING,
 * and from then on renders the next buffer from the queue's "buffer done"
 * callback. VitaSDK's libOpenSLES (a port of Android's reference
 * implementation) accepted all of that but never called the callback, so
 * nothing was ever mixed (RESEARCH.md, 2026-10-17).
 *
 * This implements exactly that: each player plays its queued buffers
 * through its own sceAudioOut port (see the thread comment below) and fires
 * the callback as each buffer is consumed. Interfaces we don't implement log their name and
 * return SL_RESULT_FEATURE_UNSUPPORTED. The SL_IID_* constants still come
 * from libOpenSLES; interfaces are matched by pointer.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/audioout.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include "utils/logger.h"

#define GRAIN 256       // frames per sceAudioOutOutput (5.3 ms at 48 kHz)
#define RING 4096       // frames buffered between the queue and the port (85 ms)

// Above the game's main thread (default 160), so a busy frame doesn't starve
// the port: the output thread most of all, then the feeder, which runs
// Cricket's mixing. Both mostly sleep on a condition variable or the port.
#define PRIO_OUTPUT 64
#define PRIO_FEEDER 80
#define MAX_QUEUE 16

enum { OBJ_ENGINE, OBJ_OUTPUT_MIX, OBJ_PLAYER };

typedef struct Obj {
    // Interface handles point at these vtable pointers.
    const struct SLObjectItf_ *obj_itf;
    const struct SLEngineItf_ *engine_itf;
    const struct SLPlayItf_ *play_itf;
    const struct SLAndroidSimpleBufferQueueItf_ *bq_itf;
    const struct SLVolumeItf_ *volume_itf;

    int type;
    SLuint32 state;

    // Player
    int channels, rate;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_cond_t space;      // ring has room / something to feed
    pthread_t feeder, output;
    int threads_started, quit;
    int16_t ring[RING * 2];    // stereo, gain applied
    int r_read, r_fill;
    volatile SLuint32 play_state;
    struct { const int16_t *data; SLuint32 frames; } queue[MAX_QUEUE];
    int q_head, q_count;
    SLuint32 q_pos;       // frames of queue[q_head] already played
    SLuint32 q_index;     // buffers completed (SLAndroidSimpleBufferQueueState.index)
    slAndroidSimpleBufferQueueCallback bq_cb;
    void *bq_ctx;
    float gain;
    SLmillibel level;
    SLboolean mute;
    int port;
} Obj;

#define FROM(itf, field) ((Obj *)((char *)(itf) - offsetof(Obj, field)))

static SLresult unsupported(const char *what) {
    l_warn("OpenSL: %s not implemented", what);
    return SL_RESULT_FEATURE_UNSUPPORTED;
}
#define UNSUPPORTED(name) static SLresult name##_u(void) { return unsupported(#name); }

/* ---- Player thread ----------------------------------------------------------- */

static void update_gain(Obj *p) {
    p->gain = p->mute ? 0.0f : powf(10.0f, p->level / 2000.0f);
}

/* Two threads per player, as Android has AudioFlinger behind the queue:
 * - feeder: copies queued buffers into the ring and, once a buffer has been
 *   copied, calls the buffer-done callback (where Cricket renders the next
 *   one, which can take several ms);
 * - output: drains the ring into sceAudioOut one grain at a time and never
 *   waits on the game, so a slow render doesn't starve the port.
 */
// Every 5 s (debug log): how long Cricket's renders took (its budget is
// audioUpdateMs x maxRenderLoad = 16 ms, see patch.c), how low the ring got,
// and how many output samples sat at full scale (clipping).
static struct { SceUInt64 start, render_sum, render_max; int renders, ring_min, clipped; } st;

static void stats_render(SceUInt64 us) {
    st.render_sum += us;
    st.renders++;
    if (us > st.render_max)
        st.render_max = us;
    SceUInt64 now = sceKernelGetProcessTimeWide();
    if (!st.start)
        st.start = now;
    if (now - st.start >= 5000000) {
        l_debug("audio: render avg %u us, max %u us (%d renders), ring min %d frames, clipped %d samples",
                (unsigned)(st.render_sum / st.renders), (unsigned)st.render_max, st.renders, st.ring_min, st.clipped);
        memset(&st, 0, sizeof(st));
        st.start = now;
        st.ring_min = RING;
    }
}

#ifdef AUDIO_DUMP
// Diagnostics: skip the first AUDIO_DUMP=skip,seconds of what goes to the
// audio port, keep the next `seconds` and write them out once, as raw 48 kHz
// stereo s16. (The write stalls the port for a moment.)
static const int dump_cfg[2] = { AUDIO_DUMP };
static int16_t *dump;
static int dump_skip, dump_frames, dump_done;

static void dump_add(const int16_t *src, int frames, int channels) {
    const int max = dump_cfg[1] * 48000;
    if (dump_done)
        return;
    if (dump_skip < dump_cfg[0] * 48000) {
        int n = dump_cfg[0] * 48000 - dump_skip < frames ? dump_cfg[0] * 48000 - dump_skip : frames;
        dump_skip += n;
        src += n * channels;
        frames -= n;
    }
    if (!frames)
        return;
    if (!dump && !(dump = malloc(max * 4))) {
        dump_done = 1;
        l_error("audio: no memory for the %d s dump", dump_cfg[1]);
        return;
    }
    if (!dump_frames)
        l_info("audio: recording %d s of output", dump_cfg[1]);
    for (int i = 0; i < frames && dump_frames < max; i++, dump_frames++) {
        dump[dump_frames * 2] = src[i * channels];
        dump[dump_frames * 2 + 1] = src[i * channels + (channels == 2)];
    }
    if (dump_frames == max) {
        dump_done = 1;
        // sceIo directly, in pieces, and checked: the stdio route left no file on the Vita.
        SceUID fd = sceIoOpen("ux0:data/sf4/audio_dump.raw", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        int written = 0;
        if (fd >= 0) {
            while (written < max * 4) {
                int chunk = max * 4 - written < 256 * 1024 ? max * 4 - written : 256 * 1024;
                int r = sceIoWrite(fd, (char *)dump + written, chunk);
                if (r <= 0) {
                    l_error("audio: sceIoWrite failed: 0x%08x after %d bytes", r, written);
                    break;
                }
                written += r;
            }
            sceIoClose(fd);
            l_info("audio: wrote %d of %d bytes to ux0:data/sf4/audio_dump.raw", written, max * 4);
        } else {
            l_error("audio: sceIoOpen(ux0:data/sf4/audio_dump.raw) failed: 0x%08x", fd);
        }
        free(dump);
    }
}
#endif

static void *feeder_thread(void *arg) {
    Obj *p = arg;
    st.ring_min = RING;
    sceKernelChangeThreadPriority(0, PRIO_FEEDER);
    while (1) {
        pthread_mutex_lock(&p->lock);
        while (!p->quit && !(p->play_state == SL_PLAYSTATE_PLAYING && p->q_count && p->r_fill < RING))
            pthread_cond_wait(&p->space, &p->lock);
        if (p->quit) {
            pthread_mutex_unlock(&p->lock);
            break;
        }
        const int16_t *src = p->queue[p->q_head].data;
        SLuint32 avail = p->queue[p->q_head].frames - p->q_pos;
        int n = RING - p->r_fill < (int)avail ? RING - p->r_fill : (int)avail;
        float g = p->gain;
        int w = (p->r_read + p->r_fill) % RING;
        for (int i = 0; i < n; i++, w = (w + 1) % RING) {
            SLuint32 f = p->q_pos + i;
            int16_t l = src[f * p->channels], r = p->channels == 2 ? src[f * 2 + 1] : l;
            p->ring[w * 2] = g == 1.0f ? l : (int16_t)(l * g);
            p->ring[w * 2 + 1] = g == 1.0f ? r : (int16_t)(r * g);
            st.clipped += (l >= 32767 || l <= -32767) + (r >= 32767 || r <= -32767);
        }
        p->r_fill += n;
        p->q_pos += n;
        int done = 0;
        if (p->q_pos >= p->queue[p->q_head].frames) {
            p->q_head = (p->q_head + 1) % MAX_QUEUE;
            p->q_count--;
            p->q_pos = 0;
            p->q_index++;
            done = 1;
        }
        slAndroidSimpleBufferQueueCallback cb = p->bq_cb;
        void *ctx = p->bq_ctx;
        pthread_cond_signal(&p->cond);
        pthread_mutex_unlock(&p->lock);

        // The callback enqueues the next buffer, so call it without the lock.
        if (done && cb) {
            SceUInt64 t0 = sceKernelGetProcessTimeWide();
            cb((SLAndroidSimpleBufferQueueItf)&p->bq_itf, ctx);
            stats_render(sceKernelGetProcessTimeWide() - t0);
        }
    }
    return NULL;
}

static void *output_thread(void *arg) {
    Obj *p = arg;
    sceKernelChangeThreadPriority(0, PRIO_OUTPUT);
    int16_t out[GRAIN * 2];
    // BGM, not MAIN: on a real Vita the music came out of the MAIN port muffled
    // (speaker and headphones) although the samples were clean; the BGM port
    // plays them as they are (RESEARCH.md, 2026-10-19).
    p->port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, GRAIN, p->rate, SCE_AUDIO_OUT_MODE_STEREO);
    if (p->port < 0) {
        l_error("OpenSL: sceAudioOutOpenPort(%d Hz) failed: 0x%08x", p->rate, p->port);
        return NULL;
    }
    l_info("OpenSL: audio port %d open, %d Hz, %d channel(s)", p->port, p->rate, p->channels);

    static int underruns;
    while (1) {
        pthread_mutex_lock(&p->lock);
        while (!p->quit && p->play_state != SL_PLAYSTATE_PLAYING)
            pthread_cond_wait(&p->cond, &p->lock);
        if (p->quit) {
            pthread_mutex_unlock(&p->lock);
            break;
        }
        int n = p->r_fill < GRAIN ? p->r_fill : GRAIN;
        for (int i = 0; i < n; i++, p->r_read = (p->r_read + 1) % RING) {
            out[i * 2] = p->ring[p->r_read * 2];
            out[i * 2 + 1] = p->ring[p->r_read * 2 + 1];
        }
        p->r_fill -= n;
        if (p->r_fill < st.ring_min)
            st.ring_min = p->r_fill;
        if (n < GRAIN && p->q_index > 1 && ++underruns % 50 == 1)  // after the first real buffer
            l_warn("OpenSL: underrun (%d so far)", underruns);
        pthread_cond_signal(&p->space);
        pthread_mutex_unlock(&p->lock);

        if (n < GRAIN)
            memset(out + n * 2, 0, (GRAIN - n) * 4);
#ifdef AUDIO_DUMP
        dump_add(out, GRAIN, 2);
#endif
        sceAudioOutOutput(p->port, out);
    }
    sceAudioOutReleasePort(p->port);
    return NULL;
}

/* ---- Buffer queue -------------------------------------------------------------- */

static SLresult bq_Enqueue(SLAndroidSimpleBufferQueueItf self, const void *buf, SLuint32 size) {
    Obj *p = FROM(self, bq_itf);
    pthread_mutex_lock(&p->lock);
    if (p->q_count == MAX_QUEUE) {
        pthread_mutex_unlock(&p->lock);
        return SL_RESULT_BUFFER_INSUFFICIENT;
    }
    int i = (p->q_head + p->q_count) % MAX_QUEUE;
    p->queue[i].data = buf;
    p->queue[i].frames = size / (2 * p->channels);
    p->q_count++;
    pthread_cond_signal(&p->space);
    pthread_mutex_unlock(&p->lock);
    return SL_RESULT_SUCCESS;
}

static SLresult bq_Clear(SLAndroidSimpleBufferQueueItf self) {
    Obj *p = FROM(self, bq_itf);
    pthread_mutex_lock(&p->lock);
    p->q_count = 0;
    p->q_pos = 0;
    pthread_mutex_unlock(&p->lock);
    return SL_RESULT_SUCCESS;
}

static SLresult bq_GetState(SLAndroidSimpleBufferQueueItf self, SLAndroidSimpleBufferQueueState *st) {
    Obj *p = FROM(self, bq_itf);
    pthread_mutex_lock(&p->lock);
    st->count = p->q_count;
    st->index = p->q_index;
    pthread_mutex_unlock(&p->lock);
    return SL_RESULT_SUCCESS;
}

static SLresult bq_RegisterCallback(SLAndroidSimpleBufferQueueItf self, slAndroidSimpleBufferQueueCallback cb, void *ctx) {
    Obj *p = FROM(self, bq_itf);
    pthread_mutex_lock(&p->lock);
    p->bq_cb = cb;
    p->bq_ctx = ctx;
    pthread_mutex_unlock(&p->lock);
    return SL_RESULT_SUCCESS;
}

static const struct SLAndroidSimpleBufferQueueItf_ bq_vtable = {
    .Enqueue = bq_Enqueue,
    .Clear = bq_Clear,
    .GetState = bq_GetState,
    .RegisterCallback = bq_RegisterCallback,
};

/* ---- Play ------------------------------------------------------------------------- */

static SLresult play_SetPlayState(SLPlayItf self, SLuint32 state) {
    Obj *p = FROM(self, play_itf);
    pthread_mutex_lock(&p->lock);
    p->play_state = state;
    if (state == SL_PLAYSTATE_STOPPED) {
        p->q_pos = 0;  // a stopped player rewinds its current buffer
        p->r_fill = 0;
    }
    pthread_cond_signal(&p->cond);
    pthread_cond_signal(&p->space);
    pthread_mutex_unlock(&p->lock);
    return SL_RESULT_SUCCESS;
}

static SLresult play_GetPlayState(SLPlayItf self, SLuint32 *state) {
    *state = FROM(self, play_itf)->play_state;
    return SL_RESULT_SUCCESS;
}

static SLresult play_GetPosition(SLPlayItf self, SLmillisecond *ms) {
    Obj *p = FROM(self, play_itf);
    *ms = 0;
    (void)p;
    return SL_RESULT_SUCCESS;
}

UNSUPPORTED(Play_GetDuration) UNSUPPORTED(Play_RegisterCallback) UNSUPPORTED(Play_SetCallbackEventsMask)
UNSUPPORTED(Play_GetCallbackEventsMask) UNSUPPORTED(Play_SetMarkerPosition) UNSUPPORTED(Play_ClearMarkerPosition)
UNSUPPORTED(Play_GetMarkerPosition) UNSUPPORTED(Play_SetPositionUpdatePeriod) UNSUPPORTED(Play_GetPositionUpdatePeriod)

static const struct SLPlayItf_ play_vtable = {
    .SetPlayState = play_SetPlayState,
    .GetPlayState = play_GetPlayState,
    .GetDuration = (void *)Play_GetDuration_u,
    .GetPosition = play_GetPosition,
    .RegisterCallback = (void *)Play_RegisterCallback_u,
    .SetCallbackEventsMask = (void *)Play_SetCallbackEventsMask_u,
    .GetCallbackEventsMask = (void *)Play_GetCallbackEventsMask_u,
    .SetMarkerPosition = (void *)Play_SetMarkerPosition_u,
    .ClearMarkerPosition = (void *)Play_ClearMarkerPosition_u,
    .GetMarkerPosition = (void *)Play_GetMarkerPosition_u,
    .SetPositionUpdatePeriod = (void *)Play_SetPositionUpdatePeriod_u,
    .GetPositionUpdatePeriod = (void *)Play_GetPositionUpdatePeriod_u,
};

/* ---- Volume ----------------------------------------------------------------------- */

static SLresult vol_SetVolumeLevel(SLVolumeItf self, SLmillibel level) {
    Obj *p = FROM(self, volume_itf);
    p->level = level;
    update_gain(p);
    return SL_RESULT_SUCCESS;
}
static SLresult vol_GetVolumeLevel(SLVolumeItf self, SLmillibel *level) {
    *level = FROM(self, volume_itf)->level;
    return SL_RESULT_SUCCESS;
}
static SLresult vol_GetMaxVolumeLevel(SLVolumeItf self, SLmillibel *level) {
    *level = 0;
    return SL_RESULT_SUCCESS;
}
static SLresult vol_SetMute(SLVolumeItf self, SLboolean mute) {
    Obj *p = FROM(self, volume_itf);
    p->mute = mute;
    update_gain(p);
    return SL_RESULT_SUCCESS;
}
static SLresult vol_GetMute(SLVolumeItf self, SLboolean *mute) {
    *mute = FROM(self, volume_itf)->mute;
    return SL_RESULT_SUCCESS;
}
UNSUPPORTED(Volume_EnableStereoPosition) UNSUPPORTED(Volume_IsEnabledStereoPosition)
UNSUPPORTED(Volume_SetStereoPosition) UNSUPPORTED(Volume_GetStereoPosition)

static const struct SLVolumeItf_ volume_vtable = {
    .SetVolumeLevel = vol_SetVolumeLevel,
    .GetVolumeLevel = vol_GetVolumeLevel,
    .GetMaxVolumeLevel = vol_GetMaxVolumeLevel,
    .SetMute = vol_SetMute,
    .GetMute = vol_GetMute,
    .EnableStereoPosition = (void *)Volume_EnableStereoPosition_u,
    .IsEnabledStereoPosition = (void *)Volume_IsEnabledStereoPosition_u,
    .SetStereoPosition = (void *)Volume_SetStereoPosition_u,
    .GetStereoPosition = (void *)Volume_GetStereoPosition_u,
};

/* ---- Object ------------------------------------------------------------------------ */

static SLresult obj_Realize(SLObjectItf self, SLboolean async) {
    Obj *o = FROM(self, obj_itf);
    if (o->type == OBJ_PLAYER && !o->threads_started) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 256 * 1024);  // Cricket renders on the feeder
        pthread_create(&o->feeder, &attr, feeder_thread, o);
        pthread_attr_setstacksize(&attr, 32 * 1024);
        pthread_create(&o->output, &attr, output_thread, o);
        pthread_attr_destroy(&attr);
        o->threads_started = 1;
    }
    o->state = SL_OBJECT_STATE_REALIZED;
    return SL_RESULT_SUCCESS;
}

static SLresult obj_Resume(SLObjectItf self, SLboolean async) { return SL_RESULT_SUCCESS; }

static SLresult obj_GetState(SLObjectItf self, SLuint32 *state) {
    *state = FROM(self, obj_itf)->state;
    return SL_RESULT_SUCCESS;
}

static SLresult obj_GetInterface(SLObjectItf self, const SLInterfaceID iid, void *out) {
    Obj *o = FROM(self, obj_itf);
    void *itf = NULL;
    if (iid == SL_IID_OBJECT)
        itf = &o->obj_itf;
    else if (iid == SL_IID_ENGINE && o->type == OBJ_ENGINE)
        itf = &o->engine_itf;
    else if (o->type == OBJ_PLAYER) {
        if (iid == SL_IID_PLAY)
            itf = &o->play_itf;
        else if (iid == SL_IID_ANDROIDSIMPLEBUFFERQUEUE || iid == SL_IID_BUFFERQUEUE)
            itf = &o->bq_itf;  // same layout
        else if (iid == SL_IID_VOLUME)
            itf = &o->volume_itf;
    }
    if (!itf) {
        l_warn("OpenSL: GetInterface: interface %p not available on object type %d", iid, o->type);
        return SL_RESULT_FEATURE_UNSUPPORTED;
    }
    *(void **)out = itf;
    return SL_RESULT_SUCCESS;
}

static void obj_Destroy(SLObjectItf self) {
    Obj *o = FROM(self, obj_itf);
    if (o->type == OBJ_PLAYER) {
        pthread_mutex_lock(&o->lock);
        o->quit = 1;
        pthread_cond_broadcast(&o->cond);
        pthread_cond_broadcast(&o->space);
        pthread_mutex_unlock(&o->lock);
        if (o->threads_started) {
            pthread_join(o->feeder, NULL);
            pthread_join(o->output, NULL);
        }
        pthread_mutex_destroy(&o->lock);
        pthread_cond_destroy(&o->cond);
        pthread_cond_destroy(&o->space);
    }
    free(o);
}

UNSUPPORTED(Object_RegisterCallback) UNSUPPORTED(Object_SetPriority) UNSUPPORTED(Object_GetPriority)
UNSUPPORTED(Object_SetLossOfControlInterfaces)
static void obj_AbortAsyncOperation(SLObjectItf self) {}

static const struct SLObjectItf_ obj_vtable = {
    .Realize = obj_Realize,
    .Resume = obj_Resume,
    .GetState = obj_GetState,
    .GetInterface = obj_GetInterface,
    .RegisterCallback = (void *)Object_RegisterCallback_u,
    .AbortAsyncOperation = obj_AbortAsyncOperation,
    .Destroy = obj_Destroy,
    .SetPriority = (void *)Object_SetPriority_u,
    .GetPriority = (void *)Object_GetPriority_u,
    .SetLossOfControlInterfaces = (void *)Object_SetLossOfControlInterfaces_u,
};

static Obj *new_obj(int type) {
    Obj *o = calloc(1, sizeof(Obj));
    o->obj_itf = &obj_vtable;
    o->type = type;
    o->state = SL_OBJECT_STATE_UNREALIZED;
    return o;
}

/* ---- Engine ------------------------------------------------------------------------ */

static SLresult engine_CreateOutputMix(SLEngineItf self, SLObjectItf *mix, SLuint32 n,
                                       const SLInterfaceID *ids, const SLboolean *req) {
    *mix = (SLObjectItf)&new_obj(OBJ_OUTPUT_MIX)->obj_itf;
    return SL_RESULT_SUCCESS;
}

static SLresult engine_CreateAudioPlayer(SLEngineItf self, SLObjectItf *player, SLDataSource *src,
                                         SLDataSink *sink, SLuint32 n, const SLInterfaceID *ids,
                                         const SLboolean *req) {
    SLuint32 locator = src && src->pLocator ? *(SLuint32 *)src->pLocator : 0;
    SLDataFormat_PCM *fmt = src ? src->pFormat : NULL;
    if ((locator != SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE && locator != SL_DATALOCATOR_BUFFERQUEUE) ||
        !fmt || fmt->formatType != SL_DATAFORMAT_PCM || fmt->bitsPerSample != 16 ||
        fmt->numChannels < 1 || fmt->numChannels > 2) {
        l_error("OpenSL: CreateAudioPlayer: unsupported source (locator 0x%x, format %d, %d bits, %d ch)",
                locator, fmt ? fmt->formatType : 0, fmt ? fmt->bitsPerSample : 0, fmt ? fmt->numChannels : 0);
        return SL_RESULT_CONTENT_UNSUPPORTED;
    }
    Obj *p = new_obj(OBJ_PLAYER);
    p->play_itf = &play_vtable;
    p->bq_itf = &bq_vtable;
    p->volume_itf = &volume_vtable;
    p->channels = fmt->numChannels;
    p->rate = fmt->samplesPerSec / 1000;  // milliHz
    p->play_state = SL_PLAYSTATE_STOPPED;
    p->port = -1;
    update_gain(p);
    pthread_mutex_init(&p->lock, NULL);
    pthread_cond_init(&p->cond, NULL);
    pthread_cond_init(&p->space, NULL);
    *player = (SLObjectItf)&p->obj_itf;
    return SL_RESULT_SUCCESS;
}

UNSUPPORTED(Engine_CreateLEDDevice) UNSUPPORTED(Engine_CreateVibraDevice) UNSUPPORTED(Engine_CreateAudioRecorder)
UNSUPPORTED(Engine_CreateMidiPlayer) UNSUPPORTED(Engine_CreateListener) UNSUPPORTED(Engine_Create3DGroup)
UNSUPPORTED(Engine_CreateMetadataExtractor) UNSUPPORTED(Engine_CreateExtensionObject)
UNSUPPORTED(Engine_QueryNumSupportedInterfaces) UNSUPPORTED(Engine_QuerySupportedInterfaces)
UNSUPPORTED(Engine_QueryNumSupportedExtensions) UNSUPPORTED(Engine_QuerySupportedExtension)
UNSUPPORTED(Engine_IsExtensionSupported)

static const struct SLEngineItf_ engine_vtable = {
    .CreateLEDDevice = (void *)Engine_CreateLEDDevice_u,
    .CreateVibraDevice = (void *)Engine_CreateVibraDevice_u,
    .CreateAudioPlayer = engine_CreateAudioPlayer,
    .CreateAudioRecorder = (void *)Engine_CreateAudioRecorder_u,
    .CreateMidiPlayer = (void *)Engine_CreateMidiPlayer_u,
    .CreateListener = (void *)Engine_CreateListener_u,
    .Create3DGroup = (void *)Engine_Create3DGroup_u,
    .CreateOutputMix = engine_CreateOutputMix,
    .CreateMetadataExtractor = (void *)Engine_CreateMetadataExtractor_u,
    .CreateExtensionObject = (void *)Engine_CreateExtensionObject_u,
    .QueryNumSupportedInterfaces = (void *)Engine_QueryNumSupportedInterfaces_u,
    .QuerySupportedInterfaces = (void *)Engine_QuerySupportedInterfaces_u,
    .QueryNumSupportedExtensions = (void *)Engine_QueryNumSupportedExtensions_u,
    .QuerySupportedExtension = (void *)Engine_QuerySupportedExtension_u,
    .IsExtensionSupported = (void *)Engine_IsExtensionSupported_u,
};

SLresult slCreateEngine_vita(SLObjectItf *engine, SLuint32 numOptions, const SLEngineOption *options,
                             SLuint32 numInterfaces, const SLInterfaceID *ids, const SLboolean *req) {
    Obj *e = new_obj(OBJ_ENGINE);
    e->engine_itf = &engine_vtable;
    *engine = (SLObjectItf)&e->obj_itf;
    return SL_RESULT_SUCCESS;
}
