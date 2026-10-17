/*
 * sf4-vita: drives libGltest2jni.so the way jp.gltest2.android.GlTest2Renderer
 * does on Android (see docs/java-side.md).
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "game.h"
#include "ime.h"
#include "input.h"

#include <stdarg.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_ImplBridge.h>
#include <so_util/so_util.h>

#include "utils/dialog.h"
#include "utils/logger.h"

extern so_module so_mod;
extern struct JNINativeInterface *_jni;  // FalsoJNI's function table

// Stand-ins for Java objects the game only passes around or compares.
#define RENDERER_CLASS ((jclass)0x42424242)   // jp/gltest2/android/GlTest2Renderer
#define APP_CONTEXT    ((jobject)0x41414141)  // android.content.Context

// GlTest2Renderer state the Java side owns.
volatile int sf4_animation = 1;       // s_Animation_flag (restart/stopAnimation)
volatile int sf4_frame_interval = 50; // s_frame_interval, ms (FrameIntervalSet)

static JavaDynArray *data_buf, *send_buf, *recv_buf; // direct ByteBuffers
static JavaDynArray *touch_tbl;                      // short[5 * 8]

/* ---- Direct ByteBuffers ----------------------------------------------------
 * FalsoJNI leaves these unimplemented. We represent a direct buffer as a byte
 * JavaDynArray, so the address and capacity are its backing array and length.
 */

static int is_direct_buffer(jobject o) {
    return o && (o == data_buf || o == send_buf || o == recv_buf);
}

static void *GetDirectBufferAddress_sf4(JNIEnv *env, jobject buf) {
    if (!is_direct_buffer(buf)) {
        l_error("GetDirectBufferAddress on unknown object 0x%x", (int)buf);
        return NULL;
    }
    return ((JavaDynArray *)buf)->array;
}

static jlong GetDirectBufferCapacity_sf4(JNIEnv *env, jobject buf) {
    return is_direct_buffer(buf) ? ((JavaDynArray *)buf)->len : -1;
}

/* ---- Receiver objects ----------------------------------------------------
 * FalsoJNI's instance-method calls drop the receiver. Record it so methods
 * like String.getBytes() can see which object they were called on.
 */

jobject sf4_jni_this;

static jobject (*orig_CallObjectMethodV)(JNIEnv *, jobject, jmethodID, va_list);
static jobject (*orig_CallObjectMethodA)(JNIEnv *, jobject, jmethodID, const jvalue *);

static jobject CallObjectMethod_sf4(JNIEnv *env, jobject obj, jmethodID id, ...) {
    va_list args;
    va_start(args, id);
    sf4_jni_this = obj;
    jobject ret = orig_CallObjectMethodV(env, obj, id, args);
    va_end(args);
    return ret;
}

static jobject CallObjectMethodV_sf4(JNIEnv *env, jobject obj, jmethodID id, va_list args) {
    sf4_jni_this = obj;
    return orig_CallObjectMethodV(env, obj, id, args);
}

static jobject CallObjectMethodA_sf4(JNIEnv *env, jobject obj, jmethodID id, const jvalue *args) {
    sf4_jni_this = obj;
    return orig_CallObjectMethodA(env, obj, id, args);
}

static JavaDynArray *new_direct_buffer(jsize size) {
    JavaDynArray *b = jda_alloc(size, FIELD_TYPE_BYTE);
    if (!b)
        fatal_error("Error: out of memory allocating a %d byte buffer.", size);
    memset(b->array, 0, size);
    return b;
}

void *sf4_data_buffer(int *size) {
    if (size)
        *size = data_buf ? data_buf->len : 0;
    return data_buf ? data_buf->array : NULL;
}

/* ---- Native entry points (all `static native` on GlTest2Renderer) ------- */

#define NATIVE(name) "Java_jp_gltest2_android_GlTest2Renderer_" #name

static void *native_symbol(const char *name) {
    void *f = (void *)so_symbol(&so_mod, name);
    if (!f)
        fatal_error("Error: %s not found in libGltest2jni.so.", name);
    return f;
}

static void (*NativeInit)(JNIEnv *, jclass, jobject, jobject, jobject);
static jboolean (*NativePreUpdate)(JNIEnv *, jclass);
static void (*NativeUpdate)(JNIEnv *, jclass, jshortArray);
static void (*NativeDraw)(JNIEnv *, jclass);
static void (*NativePostUpdate)(JNIEnv *, jclass);
static void (*initCricket)(JNIEnv *, jclass, jobject);
static void (*NativeHttpConnectionResponce)(JNIEnv *, jclass, jint, jint);
static void (*NativeHttpConnectionDidFailWithError)(JNIEnv *, jclass, jstring, jstring);
static void (*NativeHttpConnectionDidFinish)(JNIEnv *, jclass);

/* ---- HTTP ------------------------------------------------------------------
 * NetConnection queues a ConnectResult and onDrawFrame hands the oldest one to
 * native code before the next update: response code, then the error (tag and
 * message) or the data, then "finished". Offline, every request fails like a
 * connection error would.
 */

static int http_failures;

void sf4_http_fail(void) { http_failures++; }

static void http_deliver(void) {
    if (!http_failures)
        return;
    http_failures--;
    NativeHttpConnectionResponce(&jni, RENDERER_CLASS, 0, 0);
    NativeHttpConnectionDidFailWithError(&jni, RENDERER_CLASS,
                                         jni->NewStringUTF(&jni, "ConnectionError"),
                                         jni->NewStringUTF(&jni, "No network connection"));
    NativeHttpConnectionDidFinish(&jni, RENDERER_CLASS);
}

void sf4_game_init(void) {
    sf4_java_init();
    _jni->GetDirectBufferAddress = GetDirectBufferAddress_sf4;
    _jni->GetDirectBufferCapacity = GetDirectBufferCapacity_sf4;
    orig_CallObjectMethodV = _jni->CallObjectMethodV;
    orig_CallObjectMethodA = _jni->CallObjectMethodA;
    _jni->CallObjectMethod = CallObjectMethod_sf4;
    _jni->CallObjectMethodV = CallObjectMethodV_sf4;
    _jni->CallObjectMethodA = CallObjectMethodA_sf4;

    NativeInit       = native_symbol(NATIVE(NativeInit));
    NativePreUpdate  = native_symbol(NATIVE(NativePreUpdate));
    NativeUpdate     = native_symbol(NATIVE(NativeUpdate));
    NativeDraw       = native_symbol(NATIVE(NativeDraw));
    NativePostUpdate = native_symbol(NATIVE(NativePostUpdate));
    initCricket      = native_symbol(NATIVE(initCricket));
    NativeHttpConnectionResponce         = native_symbol(NATIVE(NativeHttpConnectionResponce));
    NativeHttpConnectionDidFailWithError = native_symbol(NATIVE(NativeHttpConnectionDidFailWithError));
    NativeHttpConnectionDidFinish        = native_symbol(NATIVE(NativeHttpConnectionDidFinish));

    // onSurfaceCreated
    data_buf = new_direct_buffer(32 * 1024 * 1024);
    send_buf = new_direct_buffer(4800);
    recv_buf = new_direct_buffer(4800);

    l_info("Calling initCricket");
    initCricket(&jni, RENDERER_CLASS, APP_CONTEXT);
    l_success("initCricket returned");

    l_info("Calling NativeInit");
    NativeInit(&jni, RENDERER_CLASS, (jobject)data_buf, (jobject)send_buf, (jobject)recv_buf);
    l_success("NativeInit returned");

    // TouchInit(5): 5 touch points x 8 shorts, all zero
    touch_tbl = jda_alloc(5 * 8, FIELD_TYPE_SHORT);
    memset(touch_tbl->array, 0, 5 * 8 * sizeof(jshort));
    input_init(touch_tbl->array);
}

/* ---- Frame pacing ----------------------------------------------------------
 * onDrawFrame waits until s_frame_interval ms have passed since the previous
 * frame started (setAnimationInterval / FrameIntervalSet), and on Android
 * eglSwapBuffers also blocks on the display's vsync. The game logic runs one
 * step per frame, so without this it runs as fast as the emulator can go.
 */
// Every 5 s, log the frame rate achieved and how long the game's own work
// (NativePreUpdate .. NativePostUpdate) took per frame, to tell slow emulation
// apart from pacing problems.
static SceUInt64 stat_start, stat_work;
static int stat_frames;

static void frame_stats(SceUInt64 work_start, SceUInt64 now) {
    stat_work += now - work_start;
    stat_frames++;
    if (!stat_start)
        stat_start = work_start;
    if (now - stat_start >= 5000000) {
        l_debug("frames: %.1f fps (target %.1f), game work %.1f ms/frame",
                stat_frames * 1e6 / (now - stat_start),
                1000.0 / (sf4_frame_interval < 17 ? 16.667 : sf4_frame_interval),
                stat_work / 1000.0 / stat_frames);
        stat_start = now;
        stat_work = 0;
        stat_frames = 0;
    }
}

static void frame_wait(void) {
    static SceUInt64 prev;
    SceUInt64 interval = (SceUInt64)sf4_frame_interval * 1000;
    if (interval < 16667)
        interval = 16667;  // 60 Hz, the vsync limit on a phone
    SceUInt64 now = sceKernelGetProcessTimeWide();
    if (prev && now - prev < interval) {
        sceKernelDelayThread((SceUInt32)(interval - (now - prev)));
        now = sceKernelGetProcessTimeWide();
    }
    // Keep a steady cadence, but don't try to catch up after a long frame.
    prev = (prev && now - prev < interval * 2) ? prev + interval : now;
}

void sf4_game_frame(void) {
    frame_wait();
    SceUInt64 work_start = sceKernelGetProcessTimeWide();
    static int frames = 0;
    if (frames < 3)
        l_info("frame %d", frames);

    ime_poll();          // TextBoxActivity finishing
    input_poll_touch();  // on Android touch events arrive between frames
    input_poll_pad();

    // onDrawFrame, minus the purchase plumbing we never trigger
    http_deliver();
    NativePreUpdate(&jni, RENDERER_CLASS);
    if (sf4_animation) {
        NativeUpdate(&jni, RENDERER_CLASS, (jshortArray)touch_tbl);
        NativeDraw(&jni, RENDERER_CLASS);
        input_frame_drawn();  // m_touch_move_ok_flag = true
    }
    NativePostUpdate(&jni, RENDERER_CLASS);
    frame_stats(work_start, sceKernelGetProcessTimeWide());

    if (frames < 3)
        l_success("frame %d done", frames);
#ifdef GL_TRACE
    extern void gltrace_frame_end(void);
    gltrace_frame_end();
#endif
    frames++;
}
