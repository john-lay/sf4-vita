/*
 * sf4-vita: replacement for GlTest2Renderer's Android Canvas text drawing.
 *
 * The game draws strings into one ARGB canvas (TextInit/TextBegin/TextDraw)
 * and uploads it to GL texture 2234 (TextEnd). We do the same with the Vita's
 * system fonts through ScePvf, so no font has to be shipped: ltn0.pvf for Latin
 * text, jpn0.pvf for everything else.
 *
 * Behaviour mirrors the Java side (docs/java-side.md): the canvas holds
 * premultiplied RGBA like an Android Bitmap, (x, y) is the top-left of the text
 * (Java draws at baseline y - ascent), and measurements are returned x10.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "text.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/pvf.h>
#include <vitaGL.h>

#include "utils/logger.h"

#define TEXT_TEXTURE 2234

static uint32_t *canvas;  // premultiplied RGBA, as on Android
static int canvas_w, canvas_h;

static ScePvfLibId pvf_lib;
static ScePvfFontId font_latin, font_cjk;

/* ---- fonts ------------------------------------------------------------------- */

static ScePvfPointer pvf_alloc(ScePvfPointer u, ScePvfU32 size) { return malloc(size); }
static ScePvfPointer pvf_realloc(ScePvfPointer u, ScePvfPointer p, ScePvfU32 size) { return realloc(p, size); }
static void pvf_free(ScePvfPointer u, ScePvfPointer p) { free(p); }

static ScePvfFontId open_font(const char *path) {
    ScePvfError err = 0;
    ScePvfFontId f = scePvfOpenUserFile(pvf_lib, (ScePvfPointer)path, 1, &err);
    if (err || !f) {
        l_error("text: can't open %s (0x%x)", path, err);
        return NULL;
    }
    return f;
}

static int fonts_ready(void) {
    if (pvf_lib)
        return font_latin != NULL;

    // libpvf is a system module; it's already loaded for apps that import ScePvf.
    ScePvfInitRec init = {
        .userData = NULL, .maxNumFonts = 4, .cache = NULL, .reserved = NULL,
        .allocFunc = pvf_alloc, .reallocFunc = pvf_realloc, .freeFunc = pvf_free,
    };
    ScePvfError err = 0;
    pvf_lib = scePvfNewLib(&init, &err);
    if (err || !pvf_lib) {
        l_error("text: scePvfNewLib failed (0x%x)", err);
        return 0;
    }
    // 72 dpi makes the char size in points equal to pixels, like Paint.setTextSize.
    scePvfSetResolution(pvf_lib, 72.0f, 72.0f);
    font_latin = open_font("sa0:data/font/pvf/ltn0.pvf");
    font_cjk = open_font("sa0:data/font/pvf/jpn0.pvf");
    if (font_latin)
        l_success("text: system fonts opened");
    return font_latin != NULL;
}

static ScePvfFontId font_for(uint16_t c) {
    return (c < 0x2000 || !font_cjk) ? font_latin : font_cjk;
}

/* ---- UTF-8 -> UCS-2 ---------------------------------------------------------- */

static int decode_utf8(const char *s, uint16_t *out, int max) {
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && n < max) {
        uint32_t c = *p++;
        if (c >= 0xF0)      { c = ((c & 0x07) << 18) | ((p[0] & 0x3F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
        else if (c >= 0xE0) { c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
        else if (c >= 0xC0) { c = ((c & 0x1F) << 6) | (p[0] & 0x3F); p += 1; }
        out[n++] = c > 0xFFFF ? '?' : (uint16_t)c;
    }
    return n;
}

/* ---- metrics ------------------------------------------------------------------ */

typedef struct { float ascent, descent; } line_metrics;

static int cur_size;  // last size passed to set_size

static void set_size(int size) {
    cur_size = size;
    scePvfSetCharSize(font_latin, (float)size, (float)size);
    if (font_cjk)
        scePvfSetCharSize(font_cjk, (float)size, (float)size);
}

// (cur_size is set by set_size)
// Line metrics of Android's default font (Roboto), which is what the game's
// layout was written against: Paint.getFontMetrics() ascent -0.928 x size,
// descent 0.244 x size. (scePvfGetFontInfo also returns zeros under Vita3K.)
static line_metrics metrics(void) {
    line_metrics m = { 0.928f * cur_size, 0.244f * cur_size };
    return m;
}

static float advance(uint16_t c) {
    ScePvfCharInfo ci;
    if (scePvfGetCharInfo(font_for(c), c, &ci) != 0)
        return 0;
    return ci.glyphMetrics.horizontalAdvance64 / 64.0f;
}

static float measure(const char *str, int size) {
    uint16_t buf[512];
    int n = decode_utf8(str, buf, 512);
    set_size(size);
    float w = 0;
    for (int i = 0; i < n; i++)
        w += advance(buf[i]);
    return w;
}

/* ---- drawing ------------------------------------------------------------------- */

static void blend_glyph(const uint8_t *cov, int gw, int gh, int pitch, int dx, int dy,
                        float scale_x, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int out_w = (int)(gw * scale_x + 0.5f);
    for (int y = 0; y < gh; y++) {
        int cy = dy + y;
        if (cy < 0 || cy >= canvas_h) continue;
        for (int x = 0; x < out_w; x++) {
            int cx = dx + x;
            if (cx < 0 || cx >= canvas_w) continue;
            int sx = (int)(x / scale_x);
            if (sx >= gw) sx = gw - 1;
            uint32_t k = cov[y * pitch + sx] * a / 255;  // coverage x paint alpha
            if (!k) continue;
            uint32_t *d = &canvas[cy * canvas_w + cx];
            uint32_t dv = *d, inv = 255 - k;
            // premultiplied source-over, RGBA in memory (little-endian 0xAABBGGRR)
            uint32_t rr = (r * k) / 255 + ((dv & 0xff) * inv) / 255;
            uint32_t gg = (g * k) / 255 + (((dv >> 8) & 0xff) * inv) / 255;
            uint32_t bb = (b * k) / 255 + (((dv >> 16) & 0xff) * inv) / 255;
            uint32_t aa = k + ((dv >> 24) * inv) / 255;
            *d = rr | (gg << 8) | (bb << 16) | (aa << 24);
        }
    }
}

void text_init(int w, int h) {
    free(canvas);
    canvas_w = w;
    canvas_h = h;
    canvas = calloc((size_t)w * h, 4);
    l_info("text: canvas %dx%d", w, h);
    fonts_ready();
    text_begin();
    text_end();
}

void text_begin(void) {
    if (canvas)
        memset(canvas, 0, (size_t)canvas_w * canvas_h * 4);
}

void text_draw(const char *str, int x, int y, int r, int g, int b, int a, int size, float scale_x) {
    static char last[128];
    if (strncmp(str, last, sizeof(last) - 1) != 0) {
        text_trace("text_draw(\"%s\", %d,%d, rgba %d,%d,%d,%d, size %d, scale %.2f)", str, x, y, r, g, b, a, size, scale_x);
        strncpy(last, str, sizeof(last) - 1);
    }
    if (!canvas || !fonts_ready() || size <= 0)
        return;
    if (scale_x <= 0)
        scale_x = 1.0f;

    uint16_t buf[512];
    int n = decode_utf8(str, buf, 512);
    set_size(size);
    line_metrics m = metrics();
    float pen = x;
    float baseline = y + m.ascent;

    static uint8_t cov[256 * 256];
    for (int i = 0; i < n; i++) {
        ScePvfFontId f = font_for(buf[i]);
        ScePvfCharInfo ci;
        ScePvfIrect rect;
        if (scePvfGetCharInfo(f, buf[i], &ci) != 0 || scePvfGetCharImageRect(f, buf[i], &rect) != 0)
            continue;
        // CharInfo's bitmap fields come back as 0 (seen in Vita3K), so take the
        // size from GetCharImageRect and the placement from the glyph metrics.
        int gw = rect.width + 1, gh = rect.height + 1;
        int bx64 = ci.glyphMetrics.horizontalBearingX64, by64 = ci.glyphMetrics.horizontalBearingY64;
        if (rect.width > 0 && rect.height > 0 && gw <= 256 && gh <= 256) {
            memset(cov, 0, (size_t)gw * gh);
            ScePvfUserImageBufferRec img = {
                .pixelFormat = SCE_PVF_USERIMAGE_DIRECT8,
                .xPos64 = -bx64,  // glyph origin, so the bitmap's top-left is at (0, 0)
                .yPos64 = by64,
                .rect = { .width = gw, .height = gh },
                .bytesPerLine = gw,
                .buffer = cov,
            };
            if (scePvfGetCharGlyphImage(f, buf[i], &img) == 0)
                blend_glyph(cov, gw, gh, gw, (int)(pen + bx64 / 64.0f * scale_x),
                            (int)(baseline - by64 / 64.0f), scale_x, r, g, b, a);
        }
        pen += ci.glyphMetrics.horizontalAdvance64 / 64.0f * scale_x;
    }

}

void text_end(void) {
    if (!canvas)
        return;
    // Same GL calls as GlTest2Renderer.TextEnd. Like the Java side, this
    // leaves texture 2234 bound.
    glBindTexture(GL_TEXTURE_2D, TEXT_TEXTURE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, canvas_w, canvas_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, canvas);
}

int text_width10(const char *str, int size) {
    if (!fonts_ready()) return 0;
    return (int)(measure(str, size) * 10.0f);
}

int text_height10(const char *str, int size) {
    if (!fonts_ready()) return 0;
    set_size(size);
    line_metrics m = metrics();
    return (int)((m.ascent + m.descent) * 10.0f);
}

int text_size10(const char *str, int size) {
    return (text_width10(str, size) << 16) | (text_height10(str, size) & 0xffff);
}
