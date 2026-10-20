/*
 * sf4-vita: the Java side of jp.gltest2.android.GlTest2Renderer (and the few
 * Android framework calls the game and Cricket Audio make), for FalsoJNI.
 * Behaviour follows the decompiled Java; see docs/java-side.md.
 *
 * FalsoJNI looks methods up by name only, and implementations receive the
 * arguments but not the receiver object.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#include <png.h>
#include <vitaGL.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>

#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_Impl.h>
#include <falso_jni/FalsoJNI_Logger.h>
#include <falso_jni/FalsoJNI_ImplBridge.h>

#include "game.h"

void *malloc_sf4(size_t);  // reimpl/bigalloc.c: large buffers outside the heap
void free_sf4(void *);
#include "ime.h"
#include "input.h"
#include "text.h"
#include "utils/logger.h"

#define SCREEN_W 960
#define SCREEN_H 544
#define SAVE_DIR DATA_PATH "save/"
#define LOCAL_FILE_MAX (1024 * 1024)  // LocalFileLoad's read buffer

// Stand-in for java.io.File objects (filesDir, external storage). Both map
// to the data directory, since getPath can't tell which File it was called on.
#define FILE_OBJECT ((jobject)0x46494c45)

/* ---- helpers -------------------------------------------------------------- */

static jstring jstr(const char *s) { return jni->NewStringUTF(&jni, s); }

// Copies a Java string argument into buf (empty on null).
static const char *arg_str(va_list *args, char *buf, size_t n) {
    jstring s = va_arg(*args, jstring);
    buf[0] = '\0';
    if (s) {
        const char *c = jni->GetStringUTFChars(&jni, s, NULL);
        if (c) {
            strncpy(buf, c, n - 1);
            buf[n - 1] = '\0';
            jni->ReleaseStringUTFChars(&jni, s, (char *)c);
        }
    }
    return buf;
}

static jstring ret_empty_string(jmethodID id, va_list args) { return jstr(""); }
static jint ret_int0(jmethodID id, va_list args) { return 0; }
static jlong ret_long0(jmethodID id, va_list args) { return 0; }
static jboolean ret_false(jmethodID id, va_list args) { return JNI_FALSE; }
static jboolean ret_true(jmethodID id, va_list args) { return JNI_TRUE; }
static void ret_void(jmethodID id, va_list args) {}

/* ---- File loading (FileLoad / LocalFileLoad / LocalFileSave) -------------- */

// FileLoad and LocalFileLoad cache the last name + size (s_oldFileName/Size).
static char old_name[256];
static int old_size;

static void resetOldDataFileInfo(jmethodID id, va_list args) {
    old_name[0] = '\0';
    old_size = 0;
}

// Files are read with sceIo, whole, in as few calls as possible. Through
// newlib's fopen/fread they came in at 3-4 MB/s on the Vita, the biggest part
// of the load times (RESEARCH.md, Findings 2026-10-20).
static SceUID open_file(const char *path) {
    return sceIoOpen(path, SCE_O_RDONLY, 0);
}

// Reads up to limit bytes and closes fd. Returns the number of bytes read.
static int read_fd(SceUID fd, void *buf, int limit) {
    int total = 0;
    while (total < limit) {
        int r = sceIoRead(fd, (char *)buf + total, limit - total);
        if (r <= 0)
            break;
        total += r;
    }
    sceIoClose(fd);
    return total;
}

static int dir_exists(const char *path) {
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0 && SCE_S_ISDIR(st.st_mode);
}

static SceUID open_asset(const char *name) {
    char path[512];
    SceUID fd;

    // FileLoadCheck's search order, with every APK asset flattened into assets/.
    // extract_game.py makes no External/ folder, so skip that failed open
    // unless the folder is there.
    static int has_external = -1;
    if (has_external < 0)
        has_external = dir_exists(DATA_PATH "assets/External");
    if (has_external) {
        snprintf(path, sizeof(path), DATA_PATH "assets/External/%s", name);
        if ((fd = open_file(path)) >= 0) return fd;
    }
    snprintf(path, sizeof(path), DATA_PATH "assets/%s", name);
    if ((fd = open_file(path)) >= 0) return fd;
    if (strstr(name, "dds")) {
        char upper[256];
        strncpy(upper, name, sizeof(upper) - 1);
        upper[sizeof(upper) - 1] = '\0';
        for (char *p = strstr(upper, "dds"); p; p = strstr(p + 3, "dds"))
            memcpy(p, "DDS", 3);
        snprintf(path, sizeof(path), DATA_PATH "assets/SF_IV_DATA/%s", upper);
        if ((fd = open_file(path)) >= 0) return fd;
    }
    snprintf(path, sizeof(path), DATA_PATH "resource/%s", name);
    return open_file(path);
}

static int read_into_data_buffer(SceUID fd, int limit) {
    int cap;
    char *buf = sf4_data_buffer(&cap);
    if (limit > cap)
        limit = cap;
    return read_fd(fd, buf, limit);
}

// The character select loads <char>_sel_chara.obm for all 34 characters, and
// for the ones not in the APK (the Champion Pack's downloadable data) it
// still uploads a texture from its decode buffer: whatever the last file left
// there, usually another 1024x1024 portrait. That was 28 textures
// of 4 MB (112 MB) nobody draws (locked characters show a silhouette icon),
// which filled vitaGL's memory on the way to the VS screen: 4 s stalls and
// textures left empty (garbled). (The data comes from its own decode
// buffer, so the file has to look present.) So for a missing portrait, return
// a transparent 8x8 RGBA .obm instead: the game makes an 8x8 texture.
static int blank_obm(const char *name) {
    size_t len = strlen(name);
    if (len < 14 || strcmp(name + len - 14, "_sel_chara.obm") != 0)
        return 0;
    int cap;
    unsigned char *buf = (unsigned char *)sf4_data_buffer(&cap);
    static const unsigned char header[8] = { 'O', 'I', 0x01, 32, 8, 0, 8, 0 };  // RGBA8888, 8x8
    memcpy(buf, header, sizeof(header));
    memset(buf + sizeof(header), 0, 8 * 8 * 4);
    return sizeof(header) + 8 * 8 * 4;
}

static jint FileLoad(jmethodID id, va_list args) {
    char name[256];
    arg_str(&args, name, sizeof(name));
    if (name[0] && strcmp(name, old_name) == 0)
        return old_size;

    unsigned long long t0 = prof_now();
    SceUID fd = open_asset(name);
    if (fd < 0) {
        l_warn("FileLoad(%s): not found", name);
        int n = blank_obm(name);
        if (n)
            old_name[0] = '\0';
        prof_add(PROF_FILE, t0, 0);
        return n;
    }
    int n = read_into_data_buffer(fd, 0x7fffffff);
    prof_add(PROF_FILE, t0, n);
    strcpy(old_name, name);
    old_size = n;
    l_debug("FileLoad(%s): %d bytes", name, n);
    return n;
}

static jint LocalFileLoad(jmethodID id, va_list args) {
    char name[256], path[512];
    arg_str(&args, name, sizeof(name));
    if (name[0] && strcmp(name, old_name) == 0)
        return old_size;

    unsigned long long t0 = prof_now();
    snprintf(path, sizeof(path), SAVE_DIR "%s", name);
    SceUID fd = open_file(path);
    if (fd < 0) {
        resetOldDataFileInfo(id, args);
        prof_add(PROF_FILE, t0, 0);
        return 0;
    }
    int n = read_into_data_buffer(fd, LOCAL_FILE_MAX);
    prof_add(PROF_FILE, t0, n);
    strcpy(old_name, name);
    old_size = n;
    l_debug("LocalFileLoad(%s): %d bytes", name, n);
    return n;
}

static jint LocalFileSave(jmethodID id, va_list args) {
    char name[256], path[512];
    arg_str(&args, name, sizeof(name));
    jint size = va_arg(args, jint);

    mkdir(SAVE_DIR, 0777);
    snprintf(path, sizeof(path), SAVE_DIR "%s", name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        l_error("LocalFileSave(%s): cannot open for writing", name);
        return 0;
    }
    fwrite(sf4_data_buffer(NULL), 1, size, f);
    fclose(f);
    l_debug("LocalFileSave(%s): %d bytes", name, size);
    return size;
}

// BitmapFactory.decodeStream + GLUtils.texImage2D: decode the PNG and upload it
// to the bound texture as premultiplied RGBA, the way Android bitmaps are.
// Returns (w << 16) | h, or 0 on failure.
static jint png_upload(SceUID fd, const char *name) {
    int n = (int)sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    unsigned char *file = malloc_sf4(n > 0 ? n : 1);
    n = read_fd(fd, file, n);

    png_image img;
    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;
    unsigned char *px = NULL;
    if (png_image_begin_read_from_memory(&img, file, n)) {
        img.format = PNG_FORMAT_RGBA;
        px = malloc_sf4(PNG_IMAGE_SIZE(img));
        if (!png_image_finish_read(&img, NULL, px, 0, NULL)) {
            free_sf4(px);
            px = NULL;
        }
    }
    free_sf4(file);
    if (!px) {
        l_error("PngFileLoad(%s): %s", name, img.message);
        png_image_free(&img);
        return 0;
    }

    // Premultiply: c * a / 255 rounded, without a division (the Cortex-A9 has
    // no divide instruction): t = c * a + 128, (t + (t >> 8)) >> 8 is exact.
    for (size_t i = 0, count = (size_t)img.width * img.height; i < count; i++) {
        unsigned char *p = &px[i * 4];
        unsigned a = p[3];
        if (a != 255) {
            for (int c = 0; c < 3; c++) {
                unsigned t = p[c] * a + 128;
                p[c] = (unsigned char)((t + (t >> 8)) >> 8);
            }
        }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    free_sf4(px);
    l_debug("PngFileLoad(%s): %ux%u", name, img.width, img.height);
    return (jint)(img.width << 16 | img.height);
}

static jint PngFileLoad(jmethodID id, va_list args) {
    char name[256];
    arg_str(&args, name, sizeof(name));
    unsigned long long t0 = prof_now();
    SceUID fd = open_asset(name);
    if (fd < 0) {
        l_warn("PngFileLoad(%s): not found", name);
        return 0;
    }
    jint ret = png_upload(fd, name);
    prof_add(PROF_PNG, t0, 0);
    return ret;
}

static jint LocalPngFileLoad(jmethodID id, va_list args) {
    char name[256], path[512];
    arg_str(&args, name, sizeof(name));
    snprintf(path, sizeof(path), SAVE_DIR "%s", name);
    SceUID fd = open_file(path);
    return fd >= 0 ? png_upload(fd, name) : 0;
}

// getExternalFullPath(name, external): <files>/resource/<name>. Android copies
// the asset there first (CopyToResourceDir); here the asset is used in place
// unless a resource/ copy exists. Cricket Audio opens its .ckb banks this way.
static jstring getExternalFullPath(jmethodID id, va_list args) {
    char name[256], path[512];
    struct stat st;
    arg_str(&args, name, sizeof(name));
    snprintf(path, sizeof(path), DATA_PATH "resource/%s", name);
    if (stat(path, &st) != 0)
        snprintf(path, sizeof(path), DATA_PATH "assets/%s", name);
    return jstr(path);
}

/* ---- Frame timing and animation ------------------------------------------- */

static void FrameIntervalSet(jmethodID id, va_list args) {
    sf4_frame_interval = va_arg(args, jint);
    l_debug("FrameIntervalSet → %d ms", sf4_frame_interval);
}

static void setAnimationInterval(jmethodID id, va_list args) {
    sf4_frame_interval = va_arg(args, jint) / 10;  // units of 1/10000 s → ms
    l_debug("setAnimationInterval → %d ms", sf4_frame_interval);
}

static void restartAnimation(jmethodID id, va_list args) { sf4_animation = 1; }
static void stopAnimation(jmethodID id, va_list args) { sf4_animation = 0; }
static jboolean isAnimationEnable(jmethodID id, va_list args) { return sf4_animation ? JNI_TRUE : JNI_FALSE; }

/* ---- Time ----------------------------------------------------------------- */

static jlong TimeGet(jmethodID id, va_list args) {
    if (va_arg(args, jint) == 0)  // SystemClock.uptimeMillis()
        return (jlong)(sceKernelGetProcessTimeWide() / 1000);

    // Milliseconds since 2001-01-01 00:00:00 (the iOS reference date).
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((jlong)tv.tv_sec - 978307200LL) * 1000 + tv.tv_usec / 1000;
}

static jdouble CalenderGet(jmethodID id, va_list args) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    return (tm->tm_year + 1900) * 1e10 + (tm->tm_mon + 1) * 1e8 + tm->tm_mday * 1e6
         + tm->tm_hour * 1e4 + tm->tm_min * 1e2 + tm->tm_sec;
}

/* ---- Device and display --------------------------------------------------- */

static jint getDisplaySize(jmethodID id, va_list args) { return (SCREEN_W << 16) | SCREEN_H; }

// 0 = JP, 1 = EN, 2 = FR, 3 = KR. TODO: follow the Vita's system language.
static jint LanguageGet(jmethodID id, va_list args) { return 1; }

static jint getVersionCode(jmethodID id, va_list args) { return 156; }
static jstring getVersionName(jmethodID id, va_list args) { return jstr("1.08.03"); }
static jint getDeviceApiLevel(jmethodID id, va_list args) { return 26; }
static jstring getDeviceModelName(jmethodID id, va_list args) { return jstr("PS Vita"); }
static jstring getDeviceOSVersion(jmethodID id, va_list args) { return jstr("8.0.0"); }
static jstring getDeviceId(jmethodID id, va_list args) { return jstr("0000000000000000"); }
static jstring getLocaleCountry(jmethodID id, va_list args) { return jstr("US"); }
static jint getUniqueID(jmethodID id, va_list args) { return 0x5f345649; }
static jint isMountedSD(jmethodID id, va_list args) { return 1; }
static jint getSdCardFreeBlocks(jmethodID id, va_list args) { return 0x100000; }
static jint getAssetPackDownloadProgress(jmethodID id, va_list args) { return 100; }

static jfloat getBatteryLevel(jmethodID id, va_list args) {
    return scePowerGetBatteryLifePercent() / 100.0f;
}
static jfloat ret_float0(jmethodID id, va_list args) { return 0.0f; }

static void DPrint(jmethodID id, va_list args) {
    char msg[512];
    l_info("[DPrint] %s", arg_str(&args, msg, sizeof(msg)));
}

static void exitSystem(jmethodID id, va_list args) {
    l_info("Game asked to exit");
    sceKernelExitProcess(0);
}

/* ---- Strings ---------------------------------------------------------------- */

// Java strings are UTF-16; the game asks for lengths and code units. Our
// FalsoJNI strings come from UTF-8, so this is exact for ASCII. TODO: decode
// UTF-8 properly once we see non-ASCII text.
static jint getCharLength(jmethodID id, va_list args) {
    char s[1024];
    return (jint)strlen(arg_str(&args, s, sizeof(s)));
}

static jint getCharacter(jmethodID id, va_list args) {
    char s[1024];
    arg_str(&args, s, sizeof(s));
    jint i = va_arg(args, jint);
    return (i >= 0 && i < (jint)strlen(s)) ? (unsigned char)s[i] : 0;
}

/* ---- Text entry (TextBoxActivity -> ime.c) ------------------------------------ */

// KeyBordBase(id, posX, posY, width, height, charMaxLength, fontSize, defaultText).
// id 2 is a numeric field. The box's position and font size don't matter to
// the IME dialog.
static void KeyBordBase(jmethodID id, va_list args) {
    jint type = va_arg(args, jint);
    for (int i = 0; i < 4; i++) (void)va_arg(args, jint);
    jint max_chars = va_arg(args, jint);
    (void)va_arg(args, jint);
    char def[256];
    ime_open(type, max_chars, arg_str(&args, def, sizeof(def)));
}

static jint CharGet(jmethodID id, va_list args) { return ime_char_get(va_arg(args, jint)); }
static jint CharNumGet(jmethodID id, va_list args) { return ime_char_num(); }
static jint CharFinishGet(jmethodID id, va_list args) { return ime_char_finish(); }

// Re-shows the keyboard when the game regains focus; the IME dialog stays up
// by itself, so there is nothing to do.
static void setKeyboardFocus(jmethodID id, va_list args) { l_debug("setKeyboardFocus"); }

/* ---- Text (Android Canvas -> text.c) ----------------------------------------- */

static void TextInit(jmethodID id, va_list args) {
    jint w = va_arg(args, jint), h = va_arg(args, jint);
    text_init(w, h);
}
static void TextBegin(jmethodID id, va_list args) { text_trace("TextBegin"); text_begin(); }
static void TextEnd(jmethodID id, va_list args) { text_trace("TextEnd"); text_end(); }

// TextDraw(String, x, y, r, g, b, a, size, scaleX, unused). Floats reach a
// va_list promoted to double.
static void TextDraw(jmethodID id, va_list args) {
    char s[1024];
    arg_str(&args, s, sizeof(s));
    jint x = va_arg(args, jint), y = va_arg(args, jint);
    jint r = va_arg(args, jint), g = va_arg(args, jint), b = va_arg(args, jint), a = va_arg(args, jint);
    jint size = va_arg(args, jint);
    double scale_x = va_arg(args, double);
    text_draw(s, x, y, r, g, b, a, size, (float)scale_x);
}

static jint TextSizeGet(jmethodID id, va_list args) {
    char s[1024];
    arg_str(&args, s, sizeof(s));
    jint size = va_arg(args, jint);
    jint r = text_size10(s, size);
    text_trace("TextSizeGet(\"%.40s\", %d) = %dx%d", s, size, r >> 16, r & 0xffff);
    return r;
}
static jint GetTextWidth(jmethodID id, va_list args) {
    char s[1024];
    arg_str(&args, s, sizeof(s));
    jint size = va_arg(args, jint), r = text_width10(s, size);
    text_trace("GetTextWidth(\"%.40s\", %d) = %d", s, size, r);
    return r;
}
static jint GetTextHeight(jmethodID id, va_list args) {
    char s[1024];
    arg_str(&args, s, sizeof(s));
    jint size = va_arg(args, jint), r = text_height10(s, size);
    text_trace("GetTextHeight(\"%.40s\", %d) = %d", s, size, r);
    return r;
}

/* ---- Gamepad (GCControllerLib) -------------------------------------------------- */

static jint getKeyDownData(jmethodID id, va_list args) { return input_gamepad_bits(); }
static jfloat getLeftStickX(jmethodID id, va_list args) { return input_stick(0); }
static jfloat getLeftStickY(jmethodID id, va_list args) { return input_stick(1); }
static jfloat getRightStickX(jmethodID id, va_list args) { return input_stick(2); }
static jfloat getRightStickY(jmethodID id, va_list args) { return input_stick(3); }

static jstring decodeURLEncode(jmethodID id, va_list args) {
    char s[1024];
    return jstr(arg_str(&args, s, sizeof(s)));
}

/* ---- java.lang.String ---------------------------------------------------------
 * The game builds its strings (file names included) with
 * new String(byte[], charset) and converts back with getBytes(charset).
 * We treat every charset as UTF-8, which is exact for ASCII file names.
 * TODO: handle Shift_JIS if Japanese text turns up here.
 */

static jobject String_init(jmethodID id, va_list args) {
    JavaDynArray *bytes = (JavaDynArray *)va_arg(args, jbyteArray);
    char charset[32];
    arg_str(&args, charset, sizeof(charset));
    if (!bytes)
        return NULL;

    char *s = malloc(bytes->len + 1);
    memcpy(s, bytes->array, bytes->len);
    s[bytes->len] = 0;
    jstring ret = jstr(s);
    free(s);
    return ret;
}

static jobject String_getBytes(jmethodID id, va_list args) {
    jstring self = (jstring)sf4_jni_this;
    if (!self)
        return NULL;
    const char *c = jni->GetStringUTFChars(&jni, self, NULL);
    jsize n = (jsize)strlen(c);
    JavaDynArray *out = jda_alloc(n, FIELD_TYPE_BYTE);
    memcpy(out->array, c, n);
    jni->ReleaseStringUTFChars(&jni, self, (char *)c);
    return (jobject)out;
}

static JavaDynArray *byte_array(const void *data, size_t n) {
    JavaDynArray *out = jda_alloc((jsize)n, FIELD_TYPE_BYTE);
    memcpy(out->array, data, n);
    return out;
}

// Logs each charset the game asks for once, since everything is UTF-8 here.
static void note_charset(const char *where, const char *charset) {
    static char seen[8][32];
    for (int i = 0; i < 8; i++) {
        if (!strcmp(seen[i], charset))
            return;
        if (!seen[i][0]) {
            strncpy(seen[i], charset, sizeof(seen[i]) - 1);
            l_info("%s: charset \"%s\" (treated as UTF-8)", where, charset);
            return;
        }
    }
}

// convertString(String, charset): String.getBytes(charset)
static jobject convertString(jmethodID id, va_list args) {
    char str[4096], charset[32];
    arg_str(&args, str, sizeof(str));
    note_charset("convertString", arg_str(&args, charset, sizeof(charset)));
    return (jobject)byte_array(str, strlen(str));
}

// convertStringFromBytes(byte[] utf16le, charset): re-encodes UTF-16LE text,
// every code unit including NULs, as in new String(b, "UTF-16LE").getBytes(cs).
static jobject convertStringFromBytes(jmethodID id, va_list args) {
    JavaDynArray *in = (JavaDynArray *)va_arg(args, jbyteArray);
    char charset[32];
    note_charset("convertStringFromBytes", arg_str(&args, charset, sizeof(charset)));
    if (!in)
        return NULL;

    const unsigned char *b = in->array;
    int units = in->len / 2;
    unsigned char *out = malloc(units * 3 + 1), *o = out;
    for (int i = 0; i < units; i++) {
        unsigned c = b[2 * i] | b[2 * i + 1] << 8;
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < units) {
            unsigned lo = b[2 * i + 2] | b[2 * i + 3] << 8;
            if (lo >= 0xdc00 && lo < 0xe000) {
                c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
                i++;
            }
        }
        if (c < 0x80) {
            *o++ = c;
        } else if (c < 0x800) {
            *o++ = 0xc0 | c >> 6;
            *o++ = 0x80 | (c & 0x3f);
        } else if (c < 0x10000) {
            *o++ = 0xe0 | c >> 12;
            *o++ = 0x80 | (c >> 6 & 0x3f);
            *o++ = 0x80 | (c & 0x3f);
        } else {
            *o++ = 0xf0 | c >> 18;
            *o++ = 0x80 | (c >> 12 & 0x3f);
            *o++ = 0x80 | (c >> 6 & 0x3f);
            *o++ = 0x80 | (c & 0x3f);
        }
    }
    JavaDynArray *ret = byte_array(out, o - out);
    free(out);
    return (jobject)ret;
}

/* ---- Android framework calls (Cricket Audio, Build) ------------------------ */

static jobject getFile(jmethodID id, va_list args) { return FILE_OBJECT; }

static jstring File_getPath(jmethodID id, va_list args) {
    // DATA_PATH without its trailing '/', like java.io.File.getPath()
    char path[] = DATA_PATH;
    path[sizeof(path) - 2] = '\0';
    return jstr(path);
}

/* ---- android.media.AudioTrack (Cricket queries it even when using OpenSL) -- */

static jint getNativeOutputSampleRate(jmethodID id, va_list args) { return 48000; }
static jint getMinBufferSize(jmethodID id, va_list args) { return 4096; }

/* ---- Network (offline) --------------------------------------------------------------- */

static void connectHTTP(jmethodID id, va_list args) {
    char url[512], tag[256];
    arg_str(&args, url, sizeof(url));
    arg_str(&args, tag, sizeof(tag));
    l_debug("connectHTTP(%s, %s): offline, failing it", url, tag);
    sf4_http_fail();
}
static jint isNetworkEnable(jmethodID id, va_list args) {
    l_debug("isNetworkEnable() = 0");
    return 0;
}

/* ---- Runtime permissions ------------------------------------------------------------
 * On first boot the game asks for Android 13's notification permission and
 * waits until getRequestPermissionStatus() reports 2 (answered), then reads
 * getRequestPermissionResultCode(): 1 granted, -1 denied. There is no prompt
 * on the Vita, so answer immediately with "granted" (notifications are no-ops).
 */

static int permission_status, permission_result;

static void requestPermission(jmethodID id, va_list args) {
    permission_status = 2;
    permission_result = 1;
}
static jint getRequestPermissionStatus(jmethodID id, va_list args) { return permission_status; }
static jint getRequestPermissionResultCode(jmethodID id, va_list args) { return permission_result; }
static void cleanRequestPermissionData(jmethodID id, va_list args) { permission_status = permission_result = 0; }

/* ---- Logged stubs for everything else (java_stubs.h) ---------------------------- */

extern NameToMethodID nameToMethodId[];
static const char *method_name(jmethodID id);

static void stub_log(jmethodID id) {
    static unsigned char calls[256];
    int i = (int)id - 1000;
    if (i < 0 || i >= 256 || calls[i] < 3) {
        if (i >= 0 && i < 256) calls[i]++;
        l_warn("Java method %s() called: not implemented, returning default", method_name(id));
    }
}
static void stub_void(jmethodID id, va_list args) { stub_log(id); }
static jint stub_int(jmethodID id, va_list args) { stub_log(id); return 0; }
static jboolean stub_boolean(jmethodID id, va_list args) { stub_log(id); return JNI_FALSE; }
static jobject stub_object(jmethodID id, va_list args) { stub_log(id); return jstr(""); }

#include "java_stubs.h"

/* ---- Method table ------------------------------------------------------------ */

NameToMethodID nameToMethodId[] = {
    // files
    { 1, "FileLoad", METHOD_TYPE_INT },
    { 2, "LocalFileLoad", METHOD_TYPE_INT },
    { 3, "LocalFileSave", METHOD_TYPE_INT },
    { 4, "PngFileLoad", METHOD_TYPE_INT },
    { 5, "LocalPngFileLoad", METHOD_TYPE_INT },
    { 6, "resetOldDataFileInfo", METHOD_TYPE_VOID },
    { 7, "getExternalFullPath", METHOD_TYPE_OBJECT },
    { 8, "isMountedSD", METHOD_TYPE_INT },
    { 9, "getSdCardFreeBlocks", METHOD_TYPE_INT },
    { 10, "getFolderSize", METHOD_TYPE_INT },
    { 11, "getSdCardFileCount", METHOD_TYPE_INT },
    { 12, "deleteFile", METHOD_TYPE_BOOLEAN },
    { 13, "deleteFolder", METHOD_TYPE_VOID },
    { 14, "getResVer", METHOD_TYPE_INT },
    { 15, "writeResVer", METHOD_TYPE_INT },
    { 16, "checkPermissionWriteExtenalStorage", METHOD_TYPE_BOOLEAN },
    { 17, "isDownLoadedAssetPack", METHOD_TYPE_BOOLEAN },
    { 18, "isAssetPackNowCheck", METHOD_TYPE_BOOLEAN },
    { 19, "getAssetPackDownloadProgress", METHOD_TYPE_INT },
    { 20, "getAssetPackDownloadByteMax", METHOD_TYPE_LONG },
    { 21, "getAssetPackDownloadedByte", METHOD_TYPE_LONG },

    // timing / lifecycle
    { 30, "FrameIntervalSet", METHOD_TYPE_VOID },
    { 31, "setAnimationInterval", METHOD_TYPE_VOID },
    { 32, "restartAnimation", METHOD_TYPE_VOID },
    { 33, "stopAnimation", METHOD_TYPE_VOID },
    { 34, "isAnimationEnable", METHOD_TYPE_BOOLEAN },
    { 35, "TimeGet", METHOD_TYPE_LONG },
    { 36, "CalenderGet", METHOD_TYPE_DOUBLE },
    { 37, "exitSystem", METHOD_TYPE_VOID },
    { 38, "ApplicationQuit", METHOD_TYPE_VOID },
    { 39, "selfFinish", METHOD_TYPE_VOID },
    { 40, "DPrint", METHOD_TYPE_VOID },
    { 41, "callGC", METHOD_TYPE_VOID },

    // device / display
    { 50, "getRealDisplaySize", METHOD_TYPE_INT },
    { 51, "getApplicationDisplaySize", METHOD_TYPE_INT },
    { 52, "LanguageGet", METHOD_TYPE_INT },
    { 53, "getVersionCode", METHOD_TYPE_INT },
    { 54, "getVersionName", METHOD_TYPE_OBJECT },
    { 55, "getDeviceApiLevel", METHOD_TYPE_INT },
    { 56, "getDeviceModelName", METHOD_TYPE_OBJECT },
    { 57, "getDeviceOSVersion", METHOD_TYPE_OBJECT },
    { 58, "getDeviceAndroidId", METHOD_TYPE_OBJECT },
    { 59, "getDeviceSerial", METHOD_TYPE_OBJECT },
    { 60, "getDeviceToken", METHOD_TYPE_OBJECT },
    { 61, "getLocaleCountry", METHOD_TYPE_OBJECT },
    { 62, "getLocaleCurrencyCode", METHOD_TYPE_OBJECT },
    { 63, "getUniqueID", METHOD_TYPE_INT },
    { 64, "getBatteryLevel", METHOD_TYPE_FLOAT },
    { 65, "getBatteryState", METHOD_TYPE_INT },
    { 66, "getDisplayCutoutTop", METHOD_TYPE_INT },
    { 67, "getDisplayCutoutBottom", METHOD_TYPE_INT },
    { 68, "getDisplayCutoutLeft", METHOD_TYPE_INT },
    { 69, "getDisplayCutoutRight", METHOD_TYPE_INT },
    { 70, "getDisplayRoundedCornerTop", METHOD_TYPE_INT },
    { 71, "getDisplayRoundedCornerBottom", METHOD_TYPE_INT },
    { 72, "getDisplayRoundedCornerLeft", METHOD_TYPE_INT },
    { 73, "getDisplayRoundedCornerRight", METHOD_TYPE_INT },
    { 74, "StatusBarSetting", METHOD_TYPE_VOID },

    // input
    { 80, "getInitFrg_GamceController", METHOD_TYPE_BOOLEAN },
    { 81, "initKeyData_GamceController", METHOD_TYPE_VOID },
    { 82, "getKeyDownData_GamceController", METHOD_TYPE_INT },
    { 83, "getLeftStickValueX_GamceController", METHOD_TYPE_FLOAT },
    { 84, "getLeftStickValueY_GamceController", METHOD_TYPE_FLOAT },
    { 85, "getRightStickValueX_GamceController", METHOD_TYPE_FLOAT },
    { 86, "getRightStickValueY_GamceController", METHOD_TYPE_FLOAT },
    { 87, "getTouchBackKey", METHOD_TYPE_BOOLEAN },
    { 88, "resetTouchBackKey", METHOD_TYPE_VOID },

    // text (text.c)
    { 90, "TextInit", METHOD_TYPE_VOID },
    { 91, "TextBegin", METHOD_TYPE_VOID },
    { 92, "TextDraw", METHOD_TYPE_VOID },
    { 93, "TextEnd", METHOD_TYPE_VOID },
    { 94, "TextSizeGet", METHOD_TYPE_INT },
    { 95, "GetTextWidth", METHOD_TYPE_INT },
    { 96, "GetTextHeight", METHOD_TYPE_INT },
    { 97, "getCharLength", METHOD_TYPE_INT },
    { 98, "getCharacter", METHOD_TYPE_INT },
    { 99, "decodeURLEncode", METHOD_TYPE_OBJECT },
    { 100, "KeyBordBase", METHOD_TYPE_VOID },
    { 101, "CharGet", METHOD_TYPE_INT },
    { 102, "CharNumGet", METHOD_TYPE_INT },
    { 103, "CharFinishGet", METHOD_TYPE_INT },
    { 104, "setKeyboardFocus", METHOD_TYPE_VOID },
    { 105, "convertString", METHOD_TYPE_OBJECT },
    { 106, "convertStringFromBytes", METHOD_TYPE_OBJECT },

    // movie
    { 110, "isMovieExist", METHOD_TYPE_BOOLEAN },
    { 111, "isPlayMovie", METHOD_TYPE_BOOLEAN },
    { 112, "MoviePlay", METHOD_TYPE_VOID },

    // online / store / platform services: offline stubs
    { 120, "isNetworkEnable", METHOD_TYPE_INT },
    { 121, "getNetworkState", METHOD_TYPE_INT },
    { 122, "isGooglePlayGameSignConnect", METHOD_TYPE_INT },
    { 123, "GooglePlayGamesSignIn", METHOD_TYPE_VOID },
    { 124, "GooglePlayGamesAchievementUnlock", METHOD_TYPE_VOID },
    { 125, "GooglePlayGamesShowAchievement", METHOD_TYPE_BOOLEAN },
    { 126, "isEnabledBluetooth", METHOD_TYPE_BOOLEAN },
    { 127, "startBluetooth", METHOD_TYPE_INT },
    { 128, "stopBluetooth", METHOD_TYPE_VOID },
    { 129, "initPurchaseSystem", METHOD_TYPE_VOID },
    { 130, "isPurchaseDataEmpty", METHOD_TYPE_BOOLEAN },
    { 131, "getPurchaseInventory", METHOD_TYPE_VOID },
    { 132, "connectHTTP", METHOD_TYPE_VOID },
    { 133, "cancelHTTP", METHOD_TYPE_VOID },
    { 134, "openURL", METHOD_TYPE_VOID },
    { 135, "setNotificationFlag", METHOD_TYPE_VOID },
    { 136, "cleanNotification", METHOD_TYPE_VOID },
    { 137, "getLocalPushStart", METHOD_TYPE_INT },
    { 138, "encodeHmacSHA256", METHOD_TYPE_OBJECT },
    { 139, "queryKeyValue", METHOD_TYPE_OBJECT },
    { 140, "queryValueNum", METHOD_TYPE_INT },
    { 141, "isWebViewCloseButton", METHOD_TYPE_BOOLEAN },
    { 142, "isIndicator", METHOD_TYPE_BOOLEAN },
    { 143, "getRequestPermissionStatus", METHOD_TYPE_INT },
    { 144, "getRequestPermissionResultCode", METHOD_TYPE_INT },
    { 151, "requestPermissionPostNotifications", METHOD_TYPE_VOID },
    { 152, "requestPermissionWriteExternalStorage", METHOD_TYPE_VOID },
    { 153, "cleanRequestPermissionData", METHOD_TYPE_VOID },
    { 145, "isTimeOutTimerEnable", METHOD_TYPE_BOOLEAN },
    { 146, "isBattleSendTimerEnable", METHOD_TYPE_BOOLEAN },
    { 147, "isResultCPUDialogTimer", METHOD_TYPE_BOOLEAN },
    { 148, "getCommonDialogResId", METHOD_TYPE_INT },
    { 149, "getCurrencyCode", METHOD_TYPE_OBJECT },
    { 150, "getPriceCode", METHOD_TYPE_OBJECT },

    // Android framework (Cricket Audio)
    { 200, "getFilesDir", METHOD_TYPE_OBJECT },
    { 201, "getExternalStorageDirectory", METHOD_TYPE_OBJECT },
    { 202, "getPath", METHOD_TYPE_OBJECT },
    { 203, "getAbsolutePath", METHOD_TYPE_OBJECT },
    { 204, "java/lang/String/<init>", METHOD_TYPE_OBJECT },
    { 205, "getBytes", METHOD_TYPE_OBJECT },
    { 210, "getNativeOutputSampleRate", METHOD_TYPE_INT },
    { 211, "getMinBufferSize", METHOD_TYPE_INT },

    SF4_STUB_NAMES
};

static const char *method_name(jmethodID id) {
    for (size_t i = 0; i < sizeof(nameToMethodId) / sizeof(nameToMethodId[0]); i++)
        if (nameToMethodId[i].id == (int)id)
            return nameToMethodId[i].name;
    return "?";
}

MethodsBoolean methodsBoolean[] = {
    { 12, ret_true },  // deleteFile
    { 16, ret_true },  // checkPermissionWriteExtenalStorage
    { 17, ret_true },  // isDownLoadedAssetPack: everything is installed
    { 18, ret_false },
    { 34, isAnimationEnable },
    { 80, ret_false }, // getInitFrg_GamceController: "controller removed"; the Vita's buttons never are
    { 87, ret_false },
    { 110, ret_false },  // isMovieExist: skip the online promo video for now
    { 111, ret_false },
    { 125, ret_false },
    { 126, ret_false },
    { 130, ret_true },
    { 141, ret_false },
    { 142, ret_false },
    { 145, ret_false },
    { 146, ret_false },
    { 147, ret_false },
    SF4_STUBS_BOOLEAN
};
MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {
    { 36, CalenderGet },
};
MethodsFloat methodsFloat[] = {
    { 64, getBatteryLevel },
    { 83, getLeftStickX }, { 84, getLeftStickY }, { 85, getRightStickX }, { 86, getRightStickY },
};
MethodsInt methodsInt[] = {
    { 1, FileLoad },
    { 2, LocalFileLoad },
    { 3, LocalFileSave },
    { 4, PngFileLoad },
    { 5, LocalPngFileLoad },
    { 8, isMountedSD },
    { 9, getSdCardFreeBlocks },
    { 10, ret_int0 },
    { 11, ret_int0 },
    { 14, ret_int0 },
    { 15, ret_int0 },
    { 19, getAssetPackDownloadProgress },
    { 50, getDisplaySize },
    { 51, getDisplaySize },
    { 52, LanguageGet },
    { 53, getVersionCode },
    { 55, getDeviceApiLevel },
    { 63, getUniqueID },
    { 65, ret_int0 },
    { 66, ret_int0 }, { 67, ret_int0 }, { 68, ret_int0 }, { 69, ret_int0 },
    { 70, ret_int0 }, { 71, ret_int0 }, { 72, ret_int0 }, { 73, ret_int0 },
    { 82, getKeyDownData },
    { 94, TextSizeGet }, { 95, GetTextWidth }, { 96, GetTextHeight },
    { 97, getCharLength },
    { 98, getCharacter },
    { 101, CharGet }, { 102, CharNumGet }, { 103, CharFinishGet },
    { 120, isNetworkEnable },
    { 121, isNetworkEnable },
    { 122, ret_int0 },
    { 127, ret_int0 },
    { 137, ret_int0 },
    { 140, ret_int0 },
    { 143, getRequestPermissionStatus },
    { 144, getRequestPermissionResultCode },
    { 148, ret_int0 },
    { 210, getNativeOutputSampleRate },
    { 211, getMinBufferSize },
    SF4_STUBS_INT
};
MethodsLong methodsLong[] = {
    { 20, ret_long0 },
    { 21, ret_long0 },
    { 35, TimeGet },
};
MethodsObject methodsObject[] = {
    { 7, getExternalFullPath },
    { 54, getVersionName },
    { 56, getDeviceModelName },
    { 57, getDeviceOSVersion },
    { 58, getDeviceId },
    { 59, getDeviceId },
    { 60, getDeviceId },
    { 61, getLocaleCountry },
    { 62, ret_empty_string },
    { 99, decodeURLEncode },
    { 138, ret_empty_string },
    { 139, ret_empty_string },
    { 149, ret_empty_string },
    { 150, ret_empty_string },
    { 200, getFile },
    { 201, getFile },
    { 202, File_getPath },
    { 203, File_getPath },
    { 204, String_init },
    { 205, String_getBytes },
    { 105, convertString },
    { 106, convertStringFromBytes },
    SF4_STUBS_OBJECT
};
MethodsShort methodsShort[] = {};
MethodsVoid methodsVoid[] = {
    { 6, resetOldDataFileInfo },
    { 13, ret_void },
    { 30, FrameIntervalSet },
    { 31, setAnimationInterval },
    { 32, restartAnimation },
    { 33, stopAnimation },
    { 37, exitSystem },
    { 38, exitSystem },
    { 39, exitSystem },
    { 40, DPrint },
    { 41, ret_void },
    { 74, ret_void },
    { 81, ret_void },
    { 88, ret_void },
    { 90, TextInit }, { 91, TextBegin }, { 92, TextDraw }, { 93, TextEnd },
    { 100, KeyBordBase }, { 104, setKeyboardFocus },
    { 112, ret_void },
    { 123, ret_void }, { 124, ret_void }, { 128, ret_void }, { 129, ret_void },
    { 131, ret_void }, { 132, connectHTTP }, { 133, ret_void }, { 134, ret_void },
    { 135, ret_void }, { 136, ret_void },
    { 151, requestPermission }, { 152, requestPermission }, { 153, cleanRequestPermissionData },
    SF4_STUBS_VOID
};

/* ---- Fields --------------------------------------------------------------------- */

// android.os.Build / Build.VERSION. String values are filled in at runtime by
// sf4_java_init(), since a jstring can't be a static initialiser.
NameToFieldID nameToFieldId[] = {
    { 0, "WINDOW_SERVICE", FIELD_TYPE_OBJECT },
    { 1, "SDK_INT", FIELD_TYPE_INT },
    { 2, "MODEL", FIELD_TYPE_OBJECT },
    { 3, "MANUFACTURER", FIELD_TYPE_OBJECT },
    { 4, "BRAND", FIELD_TYPE_OBJECT },
    { 5, "DEVICE", FIELD_TYPE_OBJECT },
    { 6, "PRODUCT", FIELD_TYPE_OBJECT },
    { 7, "HARDWARE", FIELD_TYPE_OBJECT },
    { 8, "RELEASE", FIELD_TYPE_OBJECT },
    // android.media.AudioManager / AudioFormat / AudioTrack constants
    { 20, "STREAM_MUSIC", FIELD_TYPE_INT },
    { 21, "ENCODING_PCM_16BIT", FIELD_TYPE_INT },
    { 22, "CHANNEL_OUT_MONO", FIELD_TYPE_INT },
    { 23, "CHANNEL_OUT_STEREO", FIELD_TYPE_INT },
    { 24, "MODE_STREAM", FIELD_TYPE_INT },
    { 25, "SUCCESS", FIELD_TYPE_INT },
};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};
FieldsInt fieldsInt[] = {
    { 1, 26 },  // Android 8.0, matching getDeviceApiLevel
    { 20, 3 }, { 21, 2 }, { 22, 4 }, { 23, 12 }, { 24, 1 }, { 25, 0 },
};
FieldsObject fieldsObject[] = {
    { 0, NULL }, { 2, NULL }, { 3, NULL }, { 4, NULL },
    { 5, NULL }, { 6, NULL }, { 7, NULL }, { 8, NULL },
};
FieldsLong fieldsLong[] = {};
FieldsShort fieldsShort[] = {};

void sf4_java_init(void) {
    static const struct { int id; const char *value; } strings[] = {
        { 0, "window" }, { 2, "PS Vita" }, { 3, "Sony" }, { 4, "Sony" },
        { 5, "vita" }, { 6, "vita" }, { 7, "vita" }, { 8, "8.0.0" },
    };
    for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); i++)
        for (size_t j = 0; j < sizeof(fieldsObject) / sizeof(fieldsObject[0]); j++)
            if (fieldsObject[j].id == strings[i].id)
                fieldsObject[j].value = jstr(strings[i].value);
}

__FALSOJNI_IMPL_CONTAINER_SIZES
