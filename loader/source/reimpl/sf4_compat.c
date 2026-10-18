/*
 * sf4-vita: bionic imports used by libGltest2jni.so / libc++_shared.so that
 * the boilerplate's dynlib table doesn't cover. See docs/imports.md for the
 * full import list and why each one needs special handling.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/sf4_compat.h"

#include <errno.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>
#include <sys/socket.h>
#include <unistd.h>

#include <psp2/kernel/threadmgr.h>
#include <so_util/so_util.h>
#include <vitaGL.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

extern const SLInterfaceID SL_IID_OUTPUTMIXEXT;  // exported, but not in the headers
extern so_default_dynlib default_dynlib[];
extern const int default_dynlib_size;

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include "utils/dialog.h"
#include "utils/utils.h"
#include "utils/logger.h"

extern so_module so_mod, cxx_mod;

/* ---- C++ exception unwinding ------------------------------------------- */

// libc++_shared.so's unwinder asks which module's .ARM.exidx table covers a PC.
// Without this every C++ throw in the game would terminate.
uintptr_t dl_unwind_find_exidx_sf4(uintptr_t pc, int *pcount) {
    so_module *mods[] = { &so_mod, &cxx_mod };
    for (size_t i = 0; i < sizeof(mods) / sizeof(mods[0]); i++) {
        so_module *m = mods[i];
        if (m->exidx_base && pc >= m->text_base && pc < m->text_base + m->text_size) {
            *pcount = (int)(m->exidx_size / 8);
            return m->exidx_base;
        }
    }
    *pcount = 0;
    return 0;
}

/* ---- FORTIFY (_chk) variants: forward to the plain function ------------ */

char *__strcpy_chk_sf4(char *dst, const char *src, size_t dstlen) { return strcpy(dst, src); }
char *__strncpy_chk_sf4(char *dst, const char *src, size_t n, size_t dstlen) { return strncpy(dst, src, n); }
char *__strncpy_chk2_sf4(char *dst, const char *src, size_t n, size_t dstlen, size_t srclen) { return strncpy(dst, src, n); }
size_t __strlen_chk_sf4(const char *s, size_t slen) { return strlen(s); }
char *__strchr_chk_sf4(const char *s, int c, size_t slen) { return strchr(s, c); }
char *__strrchr_chk_sf4(const char *s, int c, size_t slen) { return strrchr(s, c); }
void *__memcpy_chk_sf4(void *d, const void *s, size_t n, size_t dlen) { return memcpy(d, s, n); }
void *__memmove_chk_sf4(void *d, const void *s, size_t n, size_t dlen) { return memmove(d, s, n); }
void *__memset_chk_sf4(void *d, int c, size_t n, size_t dlen) { return memset(d, c, n); }

int __vsnprintf_chk_sf4(char *buf, size_t len, int flags, size_t slen, const char *fmt, va_list ap) {
    return vsnprintf(buf, len, fmt, ap);
}
int __vsprintf_chk_sf4(char *buf, int flags, size_t slen, const char *fmt, va_list ap) {
    return vsprintf(buf, fmt, ap);
}

size_t __fread_chk_sf4(void *buf, size_t size, size_t count, FILE *fp, size_t buflen) {
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fread(buf, size, count, fp);
#else
    return fread(buf, size, count, fp);
#endif
}
ssize_t __read_chk_sf4(int fd, void *buf, size_t count, size_t buflen) { return read(fd, buf, count); }

ssize_t __sendto_chk_sf4(int fd, const void *buf, size_t len, size_t buflen, int flags,
                         const struct sockaddr *addr, socklen_t addrlen) {
    return sendto(fd, buf, len, flags, addr, addrlen);
}
ssize_t __recvfrom_chk_sf4(int fd, void *buf, size_t len, size_t buflen, int flags,
                           struct sockaddr *addr, socklen_t *addrlen) {
    return recvfrom(fd, buf, len, flags, addr, addrlen);
}

// bionic's fd_set is a plain bit array of 32-bit words. Online play is out of
// scope, but keep the bit operations correct.
void __FD_SET_chk_sf4(int fd, uint32_t *set, size_t setsize) { set[fd / 32] |= 1u << (fd % 32); }
int __FD_ISSET_chk_sf4(int fd, const uint32_t *set, size_t setsize) { return (set[fd / 32] >> (fd % 32)) & 1; }

/* ---- Misc bionic --------------------------------------------------------- */

char *__gnu_strerror_r_sf4(int errnum, char *buf, size_t n) {
    if (n) {
        strncpy(buf, strerror(errnum), n - 1);
        buf[n - 1] = '\0';
    }
    return buf;
}

int gettid_sf4(void) { return sceKernelGetThreadId(); }

// Linux ARM AT_HWCAP bits for a Cortex-A9 with NEON/VFPv3 (the Vita):
// SWP|HALF|THUMB|FAST_MULT|VFP|EDSP|NEON|VFPv3|TLS.
#define AT_HWCAP  16
#define VITA_HWCAP (1 | 2 | 4 | 16 | 64 | 128 | 4096 | 8192 | 32768)
unsigned long getauxval_sf4(unsigned long type) { return type == AT_HWCAP ? VITA_HWCAP : 0; }

// bionic EAI_FAIL. The port is offline-only.
int getnameinfo_sf4(void) { return 4; }

void syslog_sf4(int prio, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    l_warn("[syslog %d] %s", prio, buf);
}

void android_set_abort_message_sf4(const char *msg) { l_fatal("abort message: %s", msg); }

/* ---- GLES1 scalar forms vitaGL lacks --------------------------------------- */

// By the GL spec the scalar form is the vector form with one value.
void glLightf_sf4(GLenum light, GLenum pname, GLfloat param) { glLightfv(light, pname, &param); }
void glLightModelf_sf4(GLenum pname, GLfloat param) { glLightModelfv(pname, &param); }

/* ---- Locale (*_l) variants: we only have the "C" locale ------------------ */

#define WCTYPE_L(name) int name##_l_sf4(wint_t wc, void *loc) { return name(wc); }
WCTYPE_L(iswalpha) WCTYPE_L(iswblank) WCTYPE_L(iswcntrl) WCTYPE_L(iswdigit) WCTYPE_L(iswlower)
WCTYPE_L(iswprint) WCTYPE_L(iswpunct) WCTYPE_L(iswspace) WCTYPE_L(iswupper) WCTYPE_L(iswxdigit)
wint_t towlower_l_sf4(wint_t wc, void *loc) { return towlower(wc); }
wint_t towupper_l_sf4(wint_t wc, void *loc) { return towupper(wc); }

long long strtoll_l_sf4(const char *s, char **end, int base, void *loc) { return strtoll(s, end, base); }
unsigned long long strtoull_l_sf4(const char *s, char **end, int base, void *loc) { return strtoull(s, end, base); }
long double strtold_l_sf4(const char *s, char **end, void *loc) { return strtold(s, end); }
long double wcstold_sf4(const wchar_t *s, wchar_t **end) { return wcstod(s, end); }

size_t strftime_l_sf4(char *s, size_t max, const char *fmt, const struct tm *tm, void *loc) {
    return strftime(s, max, fmt, tm);
}
int strcoll_l_sf4(const char *a, const char *b, void *loc) { return strcoll(a, b); }
size_t strxfrm_l_sf4(char *d, const char *s, size_t n, void *loc) { return strxfrm(d, s, n); }
int wcscoll_l_sf4(const wchar_t *a, const wchar_t *b, void *loc) { return wcscoll(a, b); }
size_t wcsxfrm_l_sf4(wchar_t *d, const wchar_t *s, size_t n, void *loc) { return wcsxfrm(d, s, n); }

size_t __ctype_get_mb_cur_max_sf4(void) { return 1; }  // "C" locale

// bigalloc.c
void *malloc_sf4(size_t), *calloc_sf4(size_t, size_t), *realloc_sf4(void *, size_t), *memalign_sf4(size_t, size_t);
void free_sf4(void *);
int posix_memalign_sf4(void **, size_t, size_t);

/* ---- Textures ----------------------------------------------------------------
 * (Conversion buffers use malloc_sf4/free_sf4, so multi-MB temporaries take
 * their own memory blocks instead of fragmenting the heap; see bigalloc.c.)
 * vitaGL stores GL_RGBA + GL_UNSIGNED_SHORT_5_5_5_1 (and 4_4_4_4) data
 * unconverted as GXM U5U5U5U1_RGBA (U4U4U4U4_RGBA), which reads the channels
 * as B, G, R, A from the low bits up: alpha at the top instead of the bottom,
 * and red and blue swapped.
 * The game uploads its character sprites (palette already expanded) and the
 * character-select world map this way, so the transparent magenta key 0xF83E
 * came out as opaque (247, 8, 247): the pink outlines, magenta tiles and
 * speckled map. Re-pack such data into the layout vitaGL really stores, which
 * keeps it 16-bit (expanding to RGBA8888 instead ran vitaGL out of memory).
 *
 * Paletted (OES_compressed_paletted_texture): the other sprite loader
 * (COglCore::NEW_loadIndexObmData, with 1P/2P palettes) uploads PALETTE4/8
 * textures. Like Android's GL drivers, expand them on upload (to the
 * palette's own 16-bit format where possible) instead of using GXM paletted
 * textures. One code path, and glCompressedTexSubImage2D (vitaGL has none;
 * COglCore::Texture_loadObmData uses it, though not during fights) can then
 * write into them.
 *
 * ETC1: the Vita's GPU decodes ETC1 and vitaGL passes it through, but
 * Vita3K's renderer crashes on it ("Unknown format 0x84000000"). With
 * ETC1_DECODE (the default for KUBRIDGE=OFF builds) decode it to RGB565.
 */
// From the low bits up, vitaGL's stored layout is B, G, R, A (checked against
// reference footage: with R in the low bits, skin came out blue).
static inline uint16_t gxm5551(unsigned c) {  // GL RGBA5551 -> as vitaGL stores it
    return ((c >> 1) & 31) | ((c >> 6) & 31) << 5 | (c >> 11) << 10 | (c & 1) << 15;
}
static inline uint16_t gxm4444(unsigned c) {  // GL RGBA4444 -> as vitaGL stores it
    return ((c >> 4) & 15) | ((c >> 8) & 15) << 4 | (c >> 12) << 8 | (c & 15) << 12;
}

#define IS_REPACKED(fmt, type) ((fmt) == GL_RGBA &&     ((type) == GL_UNSIGNED_SHORT_5_5_5_1 || (type) == GL_UNSIGNED_SHORT_4_4_4_4))

static uint16_t *repack16(GLenum type, GLsizei w, GLsizei h, const uint16_t *src) {
    uint16_t *px = malloc_sf4((size_t)w * h * 2);
    for (size_t i = 0, n = (size_t)w * h; i < n; i++)
        px[i] = type == GL_UNSIGNED_SHORT_5_5_5_1 ? gxm5551(src[i]) : gxm4444(src[i]);
    return px;
}

void glTexImage2D_sf4(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border,
                      GLenum fmt, GLenum type, const void *data) {
    if (data && IS_REPACKED(fmt, type)) {
        uint16_t *px = repack16(type, w, h, data);
        glTexImage2D(target, level, GL_RGBA, w, h, border, fmt, type, px);
        free_sf4(px);
    } else {
        glTexImage2D(target, level, ifmt, w, h, border, fmt, type, data);
    }
}

void glTexSubImage2D_sf4(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                         GLenum fmt, GLenum type, const void *data) {
    if (data && IS_REPACKED(fmt, type)) {
        uint16_t *px = repack16(type, w, h, data);
        glTexSubImage2D(target, level, x, y, w, h, fmt, type, px);
        free_sf4(px);
    } else {
        glTexSubImage2D(target, level, x, y, w, h, fmt, type, data);
    }
}

#define GL_PALETTE4_RGB8_OES_SF4    0x8B90  // ..0x8B94: RGB8, RGBA8, R5_G6_B5, RGBA4, RGB5_A1
#define GL_PALETTE8_RGB8_OES_SF4    0x8B95  // ..0x8B99: the same with 8-bit indices
#define IS_PALETTED(f) ((f) >= GL_PALETTE4_RGB8_OES_SF4 && (f) <= GL_PALETTE4_RGB8_OES_SF4 + 9)
#define GL_ETC1_RGB8_OES_SF4 0x8D64

// Palette (16 or 256 entries) followed by 4- or 8-bit indices, rows packed,
// first pixel in the high nibble. Expands to w*h pixels in the upload format
// returned in *fmt/*type (RGB8 -> RGB888, RGBA8 -> RGBA8888, 16-bit palettes
// stay 16-bit, pre-packed for vitaGL as above).
static void *depalettize(GLenum pal_fmt, GLsizei w, GLsizei h, const void *data, GLenum *fmt, GLenum *type) {
    int p4 = pal_fmt < GL_PALETTE8_RGB8_OES_SF4, kind = (pal_fmt - GL_PALETTE4_RGB8_OES_SF4) % 5;
    int entries = p4 ? 16 : 256, bpp = kind == 0 ? 3 : kind == 1 ? 4 : 2;
    static const GLenum fmts[5] = { GL_RGB, GL_RGBA, GL_RGB, GL_RGBA, GL_RGBA };
    static const GLenum types[5] = { GL_UNSIGNED_BYTE, GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT_5_6_5,
                                     GL_UNSIGNED_SHORT_4_4_4_4, GL_UNSIGNED_SHORT_5_5_5_1 };
    *fmt = fmts[kind];
    *type = types[kind];

    uint8_t pal[256 * 4];
    const uint8_t *src = data;
    for (int i = 0; i < entries; i++) {
        const uint8_t *e = src + i * bpp;
        unsigned c = e[0] | e[1] << 8;
        uint16_t v = kind == 3 ? gxm4444(c) : kind == 4 ? gxm5551(c) : c;
        if (bpp == 2)
            memcpy(&pal[i * 2], &v, 2);
        else
            memcpy(&pal[i * bpp], e, bpp);
    }
    const uint8_t *idx = src + entries * bpp;
    uint8_t *px = malloc_sf4((size_t)w * h * bpp), *d = px;
    for (size_t i = 0, n = (size_t)w * h; i < n; i++, d += bpp)
        memcpy(d, &pal[(p4 ? (i & 1 ? idx[i >> 1] & 15 : idx[i >> 1] >> 4) : idx[i]) * bpp], bpp);
    return px;
}

#ifdef ETC1_DECODE
static int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// One 8-byte ETC1 block (big-endian) to 4x4 RGB565 pixels at out[y * stride + x].
static void etc1_block(const uint8_t *b, uint16_t *out, int stride, int w, int h) {
    static const int mod[8][4] = {
        { 2, 8, -2, -8 }, { 5, 17, -5, -17 }, { 9, 29, -9, -29 }, { 13, 42, -13, -42 },
        { 18, 60, -18, -60 }, { 24, 80, -24, -80 }, { 33, 106, -33, -106 }, { 47, 183, -47, -183 },
    };
    int base[2][3];
    if (b[3] & 2) {  // differential mode: 5-bit colour + signed 3-bit delta
        for (int c = 0; c < 3; c++) {
            int c1 = b[c] >> 3, d = b[c] & 7;
            int c2 = c1 + (d >= 4 ? d - 8 : d);
            base[0][c] = (c1 << 3) | (c1 >> 2);
            base[1][c] = ((c2 & 31) << 3) | ((c2 & 31) >> 2);
        }
    } else {  // individual mode: two 4-bit colours
        for (int c = 0; c < 3; c++) {
            base[0][c] = (b[c] >> 4) * 17;
            base[1][c] = (b[c] & 15) * 17;
        }
    }
    const int *table[2] = { mod[b[3] >> 5], mod[(b[3] >> 2) & 7] };
    int flip = b[3] & 1;
    unsigned msb = b[4] << 8 | b[5], lsb = b[6] << 8 | b[7];
    for (int x = 0; x < 4 && x < w; x++)
        for (int y = 0; y < 4 && y < h; y++) {
            int i = x * 4 + y, sub = flip ? y >= 2 : x >= 2;
            int m = table[sub][((msb >> i) & 1) << 1 | ((lsb >> i) & 1)];
            int r = clamp255(base[sub][0] + m), g = clamp255(base[sub][1] + m), bl = clamp255(base[sub][2] + m);
            out[y * stride + x] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (bl >> 3));
        }
}

static uint16_t *etc1_decode(GLsizei w, GLsizei h, const void *data) {
    uint16_t *px = malloc_sf4((size_t)w * h * 2);
    const uint8_t *b = data;
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4, b += 8)
            etc1_block(b, px + by * w + bx, w, w - bx, h - by);
    return px;
}
#endif

void glCompressedTexImage2D_sf4(GLenum target, GLint level, GLenum ifmt, GLsizei w, GLsizei h,
                                GLint border, GLsizei size, const void *data) {
    if (data && IS_PALETTED(ifmt)) {
        // A negative level means "this many extra mip levels follow"; the game
        // only sends level 0, so only the base level is kept.
        GLenum fmt, type;
        void *px = depalettize(ifmt, w, h, data, &fmt, &type);
        glTexImage2D(target, 0, fmt, w, h, border, fmt, type, px);
        free_sf4(px);
        return;
    }
#ifdef ETC1_DECODE
    if (data && ifmt == GL_ETC1_RGB8_OES_SF4) {
        uint16_t *px = etc1_decode(w, h, data);
        glTexImage2D(target, level, GL_RGB, w, h, border, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, px);
        free_sf4(px);
        return;
    }
#endif
    glCompressedTexImage2D(target, level, ifmt, w, h, border, size, data);
}

void glCompressedTexSubImage2D_sf4(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                   GLenum fmt, GLsizei size, const void *data) {
    void *px = NULL;
    GLenum gl_fmt = GL_RGBA, type = GL_UNSIGNED_BYTE;
    if (IS_PALETTED(fmt))
        px = depalettize(fmt, w, h, data, &gl_fmt, &type);
#ifdef ETC1_DECODE
    else if (fmt == GL_ETC1_RGB8_OES_SF4) {
        px = etc1_decode(w, h, data);
        gl_fmt = GL_RGB;
        type = GL_UNSIGNED_SHORT_5_6_5;
    }
#endif
    if (!px) {
        static int warned;
        if (!warned++)
            l_warn("glCompressedTexSubImage2D: format 0x%x not supported, ignored", fmt);
        return;
    }
    glTexSubImage2D(target, level, x, y, w, h, gl_fmt, type, px);
    free_sf4(px);
}

/* ---- Deliberately unimplemented ------------------------------------------ */

// std::filesystem and similar. Nothing is known to call these; if something
// does, stop with a clear message instead of so_util's generic stub.
#define UNIMPL(name) void name##_unimpl_sf4(void) { fatal_error("Unimplemented import called: " #name); }
UNIMPL(symlink) UNIMPL(link) UNIMPL(fchmod) UNIMPL(sendfile) UNIMPL(readlink) UNIMPL(pathconf)
UNIMPL(utimensat) UNIMPL(fchmodat) UNIMPL(openat) UNIMPL(fdopendir) UNIMPL(unlinkat) UNIMPL(statvfs)
UNIMPL(fputwc)

/* ---- dlsym ---------------------------------------------------------------
 * Cricket Audio dlopen()s libOpenSLES.so and looks everything up with dlsym.
 * The boilerplate's dlsym returns the address of the *table slot*, not the
 * symbol, so search both tables here and return the symbol address itself.
 */
void *dlsym_sf4(void *handle, const char *symbol) {
    for (int i = 0; i < sf4_dynlib_size / (int)sizeof(so_default_dynlib); i++)
        if (strcmp(symbol, sf4_dynlib[i].symbol) == 0)
            return (void *)sf4_dynlib[i].func;
    for (int i = 0; i < default_dynlib_size / (int)sizeof(so_default_dynlib); i++)
        if (strcmp(symbol, default_dynlib[i].symbol) == 0)
            return (void *)default_dynlib[i].func;
    l_error("dlsym: unknown symbol \"%s\"", symbol);
    return NULL;
}

// sl_vita.c
SLresult slCreateEngine_vita(SLObjectItf *, SLuint32, const SLEngineOption *, SLuint32,
                             const SLInterfaceID *, const SLboolean *);

/* ---- Table ----------------------------------------------------------------- */

// bionic exports stdin/stdout/stderr as FILE* variables; this is the game's
// `stderr`. dynlib.c points it at its fake __sF[2] before resolving.
FILE *sf4_stderr;

#define E(sym, fn) { sym, (uintptr_t)&fn }
#define U(sym) { #sym, (uintptr_t)&sym##_unimpl_sf4 }

so_default_dynlib sf4_dynlib[] = {
    E("stderr", sf4_stderr),
    E("dl_unwind_find_exidx", dl_unwind_find_exidx_sf4),
    E("dlsym", dlsym_sf4),

    // OpenSL ES (Vita implementation from the `opensles` package). The
    // interface IDs are data: the entry is the address of the SLInterfaceID.
#define SL(sym) { #sym, (uintptr_t)&sym }
    E("slCreateEngine", slCreateEngine_vita),  // our own OpenSL ES (sl_vita.c)
    SL(slQueryNumSupportedEngineInterfaces),
    SL(slQuerySupportedEngineInterfaces),
    SL(SL_IID_3DCOMMIT),
    SL(SL_IID_3DDOPPLER),
    SL(SL_IID_3DGROUPING),
    SL(SL_IID_3DLOCATION),
    SL(SL_IID_3DMACROSCOPIC),
    SL(SL_IID_3DSOURCE),
    SL(SL_IID_ANDROIDCONFIGURATION),
    SL(SL_IID_ANDROIDEFFECT),
    SL(SL_IID_ANDROIDEFFECTCAPABILITIES),
    SL(SL_IID_ANDROIDEFFECTSEND),
    SL(SL_IID_ANDROIDSIMPLEBUFFERQUEUE),
    SL(SL_IID_AUDIODECODERCAPABILITIES),
    SL(SL_IID_AUDIOENCODER),
    SL(SL_IID_AUDIOENCODERCAPABILITIES),
    SL(SL_IID_AUDIOIODEVICECAPABILITIES),
    SL(SL_IID_BASSBOOST),
    SL(SL_IID_BUFFERQUEUE),
    SL(SL_IID_DEVICEVOLUME),
    SL(SL_IID_DYNAMICINTERFACEMANAGEMENT),
    SL(SL_IID_DYNAMICSOURCE),
    SL(SL_IID_EFFECTSEND),
    SL(SL_IID_ENGINE),
    SL(SL_IID_ENGINECAPABILITIES),
    SL(SL_IID_ENVIRONMENTALREVERB),
    SL(SL_IID_EQUALIZER),
    SL(SL_IID_LED),
    SL(SL_IID_METADATAEXTRACTION),
    SL(SL_IID_METADATATRAVERSAL),
    SL(SL_IID_MIDIMESSAGE),
    SL(SL_IID_MIDIMUTESOLO),
    SL(SL_IID_MIDITEMPO),
    SL(SL_IID_MIDITIME),
    SL(SL_IID_MUTESOLO),
    SL(SL_IID_NULL),
    SL(SL_IID_OBJECT),
    SL(SL_IID_OUTPUTMIX),
    SL(SL_IID_OUTPUTMIXEXT),
    SL(SL_IID_PITCH),
    SL(SL_IID_PLAY),
    SL(SL_IID_PLAYBACKRATE),
    SL(SL_IID_PREFETCHSTATUS),
    SL(SL_IID_PRESETREVERB),
    SL(SL_IID_RATEPITCH),
    SL(SL_IID_RECORD),
    SL(SL_IID_SEEK),
    SL(SL_IID_THREADSYNC),
    SL(SL_IID_VIBRA),
    SL(SL_IID_VIRTUALIZER),
    SL(SL_IID_VISUALIZATION),
    SL(SL_IID_VOLUME),
#undef SL

    // The boilerplate stubs these to ret0. The game uses fixed-function
    // lighting, so forward them to vitaGL's vector forms instead.
    // (glLogicOp stays stubbed: vitaGL lacks it. See RESEARCH.md section 4.
    // glCompressedTexSubImage2D is handled above, for the formats the game uses.)
    E("glLightf", glLightf_sf4),
    E("glLightModelf", glLightModelf_sf4),
    // Large blocks bypass the newlib heap (bigalloc.c).
    E("malloc", malloc_sf4), E("calloc", calloc_sf4), E("realloc", realloc_sf4),
    E("free", free_sf4), E("memalign", memalign_sf4),
    E("glTexImage2D", glTexImage2D_sf4),
    E("glTexSubImage2D", glTexSubImage2D_sf4),
    E("glCompressedTexImage2D", glCompressedTexImage2D_sf4),
    E("glCompressedTexSubImage2D", glCompressedTexSubImage2D_sf4),
    E("__gnu_Unwind_Find_exidx", dl_unwind_find_exidx_sf4),

    E("__strcpy_chk", __strcpy_chk_sf4),
    E("__strncpy_chk", __strncpy_chk_sf4),
    E("__strncpy_chk2", __strncpy_chk2_sf4),
    E("__strlen_chk", __strlen_chk_sf4),
    E("__strchr_chk", __strchr_chk_sf4),
    E("__strrchr_chk", __strrchr_chk_sf4),
    E("__memcpy_chk", __memcpy_chk_sf4),
    E("__memmove_chk", __memmove_chk_sf4),
    E("__memset_chk", __memset_chk_sf4),
    E("__vsnprintf_chk", __vsnprintf_chk_sf4),
    E("__vsprintf_chk", __vsprintf_chk_sf4),
    E("__fread_chk", __fread_chk_sf4),
    E("__read_chk", __read_chk_sf4),
    E("__sendto_chk", __sendto_chk_sf4),
    E("__recvfrom_chk", __recvfrom_chk_sf4),
    E("__FD_SET_chk", __FD_SET_chk_sf4),
    E("__FD_ISSET_chk", __FD_ISSET_chk_sf4),

    E("__gnu_strerror_r", __gnu_strerror_r_sf4),
    E("__register_atfork", ret0),
    E("__cxa_thread_atexit_impl", ret0),
    E("signal", ret0),           // returns SIG_DFL (0) as the "previous" handler
    E("setpriority", ret0),
    E("gettid", gettid_sf4),
    E("getauxval", getauxval_sf4),
    E("getnameinfo", getnameinfo_sf4),
    E("posix_memalign", posix_memalign_sf4),
    E("syslog", syslog_sf4),
    E("openlog", ret0),
    E("closelog", ret0),
    E("android_set_abort_message", android_set_abort_message_sf4),

    E("iswalpha_l", iswalpha_l_sf4), E("iswblank_l", iswblank_l_sf4), E("iswcntrl_l", iswcntrl_l_sf4),
    E("iswdigit_l", iswdigit_l_sf4), E("iswlower_l", iswlower_l_sf4), E("iswprint_l", iswprint_l_sf4),
    E("iswpunct_l", iswpunct_l_sf4), E("iswspace_l", iswspace_l_sf4), E("iswupper_l", iswupper_l_sf4),
    E("iswxdigit_l", iswxdigit_l_sf4),
    E("towlower_l", towlower_l_sf4), E("towupper_l", towupper_l_sf4),
    E("strtoll_l", strtoll_l_sf4), E("strtoull_l", strtoull_l_sf4), E("strtold_l", strtold_l_sf4),
    E("wcstold", wcstold_sf4),
    E("strftime_l", strftime_l_sf4), E("strcoll_l", strcoll_l_sf4), E("strxfrm_l", strxfrm_l_sf4),
    E("wcscoll_l", wcscoll_l_sf4), E("wcsxfrm_l", wcsxfrm_l_sf4),
    E("__ctype_get_mb_cur_max", __ctype_get_mb_cur_max_sf4),

    U(symlink), U(link), U(fchmod), U(sendfile), U(readlink), U(pathconf),
    U(utimensat), U(fchmodat), U(openat), U(fdopendir), U(unlinkat), U(statvfs),
    U(fputwc),
};
const int sf4_dynlib_size = sizeof(sf4_dynlib);
