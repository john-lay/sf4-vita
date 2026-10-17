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

void sf4_java_init(void);  // java.c: runtime values for fake Java fields
void sf4_game_init(void);   // onSurfaceCreated: buffers, initCricket, NativeInit
void sf4_game_frame(void);  // onDrawFrame: PreUpdate/Update/Draw/PostUpdate

#endif
