/*
 * sf4-vita: game driver, see game.c.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef SF4_GAME_H
#define SF4_GAME_H

// GlTest2Renderer state that native code changes through Java calls.
extern volatile int sf4_animation;
extern volatile int sf4_frame_interval;

// The 32 MB direct ByteBuffer FileLoad/LocalFileLoad/LocalFileSave work through.
void *sf4_data_buffer(int *size);

// Receiver of the current instance-method call (FalsoJNI doesn't pass it).
#include <falso_jni/FalsoJNI.h>
extern jobject sf4_jni_this;

// connectHTTP: there is no network, so the request fails on the next frame.
void sf4_http_fail(void);

// Load-time breakdown for the 5 s log: where the time inside game frames goes.
// prof_add(stage, start, bytes) adds the time since start (process time, us).
enum {
    PROF_FILE,    // FileLoad/LocalFileLoad: finding and reading files
    PROF_PNG,     // PngFileLoad: read, decode and upload
    PROF_TEX,     // texture uploads through sf4_compat.c, conversions included
    PROF_TEX_GL,  // ... of which inside vitaGL's glTexImage2D & co.
    PROF_ZLIB,    // the game's uncompress/inflate calls (zlib-packed .obm files)
    PROF_STAGES
};
#include <psp2/kernel/processmgr.h>
static inline unsigned long long prof_now(void) { return sceKernelGetProcessTimeWide(); }
void prof_add(int stage, unsigned long long start, unsigned bytes);

// Character-select portraits (*_sel_chara.obm, 1024x1024 RGBA) are uploaded at
// half size. FileLoad sets sf4_portrait_pending for the next 1024x1024 RGBA
// glTexImage2D (reimpl/sf4_compat.c), which clears it:
//   PORTRAIT_HALVE: halve the game's pixels there (slow on the Vita);
//   PORTRAIT_READY: upload sf4_portrait_px (w x h) instead, the half-size copy
//     made on the PC (tools/portraits.py, DATA_PATH "half/<name>.half"). The
//     game got a stand-in .obm whose zlib payload is SF4_PORTRAIT_MAGIC, which
//     uncompress_sf4 answers with just the 1024x1024 header.
enum { PORTRAIT_NONE, PORTRAIT_HALVE, PORTRAIT_READY };
extern int sf4_portrait_pending;
extern unsigned char *sf4_portrait_px;
extern int sf4_portrait_w, sf4_portrait_h;
#define SF4_PORTRAIT_MAGIC "sf4-vita:portrait"  // 17 bytes, no NUL

void sf4_java_init(void);  // java.c: runtime values for fake Java fields
void sf4_game_init(void);   // onSurfaceCreated: buffers, initCricket, NativeInit
void sf4_game_frame(void);  // onDrawFrame: PreUpdate/Update/Draw/PostUpdate

#endif
