/*
 * sf4-vita: optional GL call tracing (build with -DGL_TRACE=ON).
 *
 * Wraps the GLES1 calls that decide what ends up on screen and logs each
 * distinct argument combination once, so the log shows *how* the game draws
 * without a line per call. Entries go first in the import table, ahead of
 * vitaGL. Diagnostic only; not part of normal builds.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifdef GL_TRACE

#include <stdint.h>
#include <string.h>
#include <vitaGL.h>
#include <so_util/so_util.h>

#include "utils/logger.h"

// Small open-addressing set of hashes of logged argument combinations.
#define SEEN_MAX 4096
static uint32_t seen[SEEN_MAX];

static int first_time(uint32_t h) {
    h |= 1;
    for (uint32_t i = h % SEEN_MAX, n = 0; n < SEEN_MAX; i = (i + 1) % SEEN_MAX, n++) {
        if (seen[i] == h) return 0;
        if (!seen[i]) { seen[i] = h; return 1; }
    }
    return 0;
}

static uint32_t mix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
#define H1(a)          mix(2166136261u, (uint32_t)(a))
#define H2(a, b)       mix(H1(a), (uint32_t)(b))
#define H3(a, b, c)    mix(H2(a, b), (uint32_t)(c))
#define H4(a, b, c, d) mix(H3(a, b, c), (uint32_t)(d))

// Every call is logged during frame FULL_FRAME; otherwise only first occurrences.
#define FULL_FRAME 300
static int draws_this_frame, frame_no;
#define ONCE(hash, ...) do { if (first_time(hash) || frame_no == FULL_FRAME) l_info("[gl] " __VA_ARGS__); } while (0)

void gltrace_frame_end(void) {
    if (frame_no < 5 || frame_no % 60 == 0)
        l_info("[gl] frame %d: %d draw calls", frame_no, draws_this_frame);
    draws_this_frame = 0;
    frame_no++;
}

static void t_glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *p) {
    ONCE(H4(1, size, type, stride), "glVertexPointer(size %d, type 0x%x, stride %d)", size, type, stride);
    glVertexPointer(size, type, stride, p);
}
static void t_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *p) {
    ONCE(H4(2, size, type, stride), "glTexCoordPointer(size %d, type 0x%x, stride %d)", size, type, stride);
    glTexCoordPointer(size, type, stride, p);
}
static void t_glColorPointer(GLint size, GLenum type, GLsizei stride, const void *p) {
    ONCE(H4(3, size, type, stride), "glColorPointer(size %d, type 0x%x, stride %d)", size, type, stride);
    glColorPointer(size, type, stride, p);
}
static void t_glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h,
                           GLint border, GLenum fmt, GLenum type, const void *data) {
    ONCE(H4(4, ifmt, fmt, type) ^ H2(w, h), "glTexImage2D(level %d, ifmt 0x%x, %dx%d, fmt 0x%x, type 0x%x, data %s)",
         level, ifmt, w, h, fmt, type, data ? "yes" : "NULL");
    while (glGetError()) {}  // drop stale errors so the check below is ours
    glTexImage2D(target, level, ifmt, w, h, border, fmt, type, data);
    GLenum e = glGetError();
    if (e) l_error("[gl] glTexImage2D(0x%x, %dx%d) -> error 0x%x", ifmt, w, h, e);
}
static void t_glCompressedTexImage2D(GLenum target, GLint level, GLenum ifmt, GLsizei w, GLsizei h,
                                     GLint border, GLsizei size, const void *data) {
    ONCE(H4(5, ifmt, w, h), "glCompressedTexImage2D(level %d, fmt 0x%x, %dx%d, %d bytes)", level, ifmt, w, h, size);
    while (glGetError()) {}
    glCompressedTexImage2D(target, level, ifmt, w, h, border, size, data);
    GLenum e = glGetError();
    if (e) l_error("[gl] glCompressedTexImage2D(0x%x, %dx%d) -> error 0x%x", ifmt, w, h, e);
}
static void t_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                              GLenum fmt, GLenum type, const void *data) {
    ONCE(H4(6, fmt, type, 0), "glTexSubImage2D(fmt 0x%x, type 0x%x, %dx%d at %d,%d)", fmt, type, w, h, x, y);
    glTexSubImage2D(target, level, x, y, w, h, fmt, type, data);
}
static void t_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    draws_this_frame++;
    ONCE(H2(7, mode), "glDrawArrays(mode 0x%x, count %d)", mode, count);
    glDrawArrays(mode, first, count);
}
static void t_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *idx) {
    draws_this_frame++;
    ONCE(H3(8, mode, type), "glDrawElements(mode 0x%x, count %d, type 0x%x)", mode, count, type);
    glDrawElements(mode, count, type, idx);
}
static void t_glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    ONCE(H4(9, x, y, w) ^ h, "glViewport(%d, %d, %d, %d)", x, y, w, h);
    glViewport(x, y, w, h);
}
static void t_glOrthof(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) {
    ONCE(H4(10, (int)l, (int)r, (int)b) ^ (int)t, "glOrthof(%.1f, %.1f, %.1f, %.1f, %.1f, %.1f)", l, r, b, t, n, f);
    glOrthof(l, r, b, t, n, f);
}
static void t_glFrustumf(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) {
    ONCE(H2(11, (int)(n * 1000)), "glFrustumf(%.3f, %.3f, %.3f, %.3f, %.3f, %.3f)", l, r, b, t, n, f);
    glFrustumf(l, r, b, t, n, f);
}
static void t_glEnable(GLenum cap) {
    ONCE(H2(12, cap), "glEnable(0x%x)", cap);
    glEnable(cap);
}
static void t_glDisable(GLenum cap) { ONCE(H2(13, cap), "glDisable(0x%x)", cap); glDisable(cap); }
static void t_glBlendFunc(GLenum s, GLenum d) { ONCE(H3(14, s, d), "glBlendFunc(0x%x, 0x%x)", s, d); glBlendFunc(s, d); }

static void t_glBindFramebufferOES(GLenum target, GLuint fb) {
    ONCE(H2(15, fb), "glBindFramebufferOES(%u)", fb);
    glBindFramebuffer(target, fb);
}
static void t_glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    ONCE(H4(16, (int)(r * 255), (int)(g * 255), (int)(b * 255)), "glClearColor(%.2f, %.2f, %.2f, %.2f)", r, g, b, a);
    glClearColor(r, g, b, a);
}
static void t_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    ONCE(H4(17, (int)(r * 255), (int)(g * 255), (int)(b * 255)) ^ (int)(a * 255), "glColor4f(%.2f, %.2f, %.2f, %.2f)", r, g, b, a);
    glColor4f(r, g, b, a);
}
static void t_glAlphaFunc(GLenum f, GLclampf ref) { ONCE(H3(18, f, (int)(ref * 255)), "glAlphaFunc(0x%x, %.2f)", f, ref); glAlphaFunc(f, ref); }
static void t_glTexParameterf(GLenum target, GLenum pname, GLfloat v) {
    union { GLfloat f; uint32_t u; } bits = { .f = v };
    ONCE(H3(19, pname, bits.u), "glTexParameterf(0x%x, %.1f, raw bits 0x%08x)", pname, v, bits.u);
    glTexParameterf(target, pname, v);
}

/* Framebuffer objects: forwarded to whatever vitaGL exports for the OES names. */
static void *vgl(const char *name) { return vglGetProcAddress(name); }

static void t_glGenFramebuffersOES(GLsizei n, GLuint *ids) {
    ((void (*)(GLsizei, GLuint *))vgl("glGenFramebuffersOES"))(n, ids);
    l_info("[gl] glGenFramebuffersOES -> %u", ids[0]);
}
static void t_glFramebufferTexture2DOES(GLenum target, GLenum att, GLenum textarget, GLuint tex, GLint level) {
    l_info("[gl] glFramebufferTexture2DOES(att 0x%x, textarget 0x%x, tex %u)", att, textarget, tex);
    ((void (*)(GLenum, GLenum, GLenum, GLuint, GLint))vgl("glFramebufferTexture2DOES"))(target, att, textarget, tex, level);
}
static void t_glFramebufferRenderbufferOES(GLenum target, GLenum att, GLenum rbt, GLuint rb) {
    l_info("[gl] glFramebufferRenderbufferOES(att 0x%x, rb %u)", att, rb);
    ((void (*)(GLenum, GLenum, GLenum, GLuint))vgl("glFramebufferRenderbufferOES"))(target, att, rbt, rb);
}
static void t_glRenderbufferStorageOES(GLenum target, GLenum fmt, GLsizei w, GLsizei h) {
    l_info("[gl] glRenderbufferStorageOES(fmt 0x%x, %dx%d)", fmt, w, h);
    ((void (*)(GLenum, GLenum, GLsizei, GLsizei))vgl("glRenderbufferStorageOES"))(target, fmt, w, h);
}
static GLenum t_glCheckFramebufferStatusOES(GLenum target) {
    GLenum r = ((GLenum (*)(GLenum))vgl("glCheckFramebufferStatusOES"))(target);
    l_info("[gl] glCheckFramebufferStatusOES -> 0x%x", r);
    return r;
}
static void t_glBindTexture(GLenum target, GLuint tex) {
    ONCE(H2(20, tex), "glBindTexture(%u)", tex);
    glBindTexture(target, tex);
}
static void t_glTexEnvf(GLenum t, GLenum p, GLfloat v) { ONCE(H3(21, p, (int)v), "glTexEnvf(0x%x, %.0f)", p, v); glTexEnvf(t, p, v); }
static void t_glTexEnvi(GLenum t, GLenum p, GLint v) { ONCE(H3(22, p, v), "glTexEnvi(0x%x, 0x%x)", p, v); glTexEnvi(t, p, v); }
static void t_glEnableClientState(GLenum a) { ONCE(H2(23, a), "glEnableClientState(0x%x)", a); glEnableClientState(a); }
static void t_glDisableClientState(GLenum a) { ONCE(H2(24, a), "glDisableClientState(0x%x)", a); glDisableClientState(a); }
static void t_glClear(GLbitfield m) {
    ONCE(H2(25, m), "glClear(0x%x)", m);
    glClear(m);
}

#define T(name) { #name, (uintptr_t)&t_##name }
so_default_dynlib gltrace_dynlib[] = {
    T(glVertexPointer), T(glTexCoordPointer), T(glColorPointer),
    T(glTexImage2D), T(glCompressedTexImage2D), T(glTexSubImage2D),
    T(glDrawArrays), T(glDrawElements), T(glViewport), T(glOrthof), T(glFrustumf),
    T(glEnable), T(glDisable), T(glBlendFunc), T(glBindFramebufferOES),
    T(glClearColor), T(glColor4f), T(glAlphaFunc), T(glTexParameterf),
    T(glGenFramebuffersOES), T(glFramebufferTexture2DOES), T(glFramebufferRenderbufferOES),
    T(glRenderbufferStorageOES), T(glCheckFramebufferStatusOES), T(glBindTexture),
    T(glTexEnvf), T(glTexEnvi), T(glEnableClientState), T(glDisableClientState), T(glClear),
};
const int gltrace_dynlib_size = sizeof(gltrace_dynlib);

#endif
