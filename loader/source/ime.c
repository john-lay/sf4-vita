/*
 * sf4-vita: text entry (nickname registration etc.) through the Vita IME dialog.
 *
 * On Android, KeyBordBase starts TextBoxActivity, an EditText over the game.
 * When the user confirms (or backs out), TextBoxActivity.setText copies the
 * text into GlTest2Activity's char table: at most charMaxLength UTF-16 code
 * units, '\n'/'\t' dropped, and a '\n' inserted after the 25th; then it sets
 * the count and the finish flag. Native code polls CharFinishGet, CharNumGet
 * and CharGet. Backing out returns the default text.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "ime.h"

#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/ime_dialog.h>

#include "utils/logger.h"

#define CHAR_TBL 42  // GlTest2Activity.s_char_tbl

static unsigned short char_tbl[CHAR_TBL];
static int char_num, char_finish;

static int running, max_len;
static uint16_t title16[SCE_IME_DIALOG_MAX_TITLE_LENGTH];
static uint16_t default16[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
static uint16_t input16[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];

// UTF-8 -> UTF-16 (BMP only, which is all a Java char holds).
static void utf8_to_16(const char *s, uint16_t *out, int n) {
    const unsigned char *p = (const unsigned char *)s;
    int i = 0;
    while (*p && i < n - 1) {
        unsigned c = *p++;
        if (c >= 0xe0 && p[0] && p[1]) {
            c = ((c & 0x0f) << 12) | ((p[0] & 0x3f) << 6) | (p[1] & 0x3f);
            p += 2;
        } else if (c >= 0xc0 && p[0]) {
            c = ((c & 0x1f) << 6) | (p[0] & 0x3f);
            p += 1;
        }
        out[i++] = (uint16_t)c;
    }
    out[i] = 0;
}

// TextBoxActivity.setText
static void set_text(const uint16_t *s) {
    memset(char_tbl, 0, sizeof(char_tbl));
    int n = 0, pos = 0;
    for (; *s; s++) {
        if (*s == '\n' || *s == '\t')
            continue;
        if (pos >= CHAR_TBL)
            break;
        char_tbl[pos++] = *s;
        if (pos == 25 && pos < CHAR_TBL)
            char_tbl[pos++] = '\n';
        if (++n == max_len)
            break;
    }
    char_num = pos;
    char_finish = 1;
    l_debug("IME: %d chars returned", n);
}

void ime_open(int type, int max_chars, const char *default_text) {
    char_num = char_finish = 0;  // GlTest2Activity.CharInit
    max_len = max_chars;
    utf8_to_16(default_text, default16, SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1);
    l_info("KeyBordBase: type %d, max %d chars, default \"%s\"", type, max_chars, default_text);

#ifdef AUTO_IME
    // Test hook: -DAUTO_IME="\"text\"" answers at once instead of opening the dialog.
    utf8_to_16(AUTO_IME, input16, SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1);
    set_text(input16);
    return;
#endif

    if (running)
        return;
    utf8_to_16(type == 2 ? "Enter a number" : "Enter text", title16, SCE_IME_DIALOG_MAX_TITLE_LENGTH);
    memcpy(input16, default16, sizeof(input16));

    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.supportedLanguages = 0x0001FFFF;
    param.languagesForced = SCE_TRUE;
    param.type = type == 2 ? SCE_IME_TYPE_NUMBER : SCE_IME_TYPE_BASIC_LATIN;
    param.title = title16;
    param.maxTextLength = (max_chars > 0 && max_chars < SCE_IME_DIALOG_MAX_TEXT_LENGTH)
                              ? max_chars : SCE_IME_DIALOG_MAX_TEXT_LENGTH;
    param.initialText = default16;
    param.inputTextBuffer = input16;
    int ret = sceImeDialogInit(&param);
    if (ret < 0) {
        l_error("sceImeDialogInit failed: 0x%08x; keeping the default text", ret);
        set_text(default16);
        return;
    }
    running = 1;
}

void ime_poll(void) {
    if (!running || sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED)
        return;
    SceImeDialogResult result;
    memset(&result, 0, sizeof(result));
    sceImeDialogGetResult(&result);
    set_text(result.button == SCE_IME_DIALOG_BUTTON_ENTER ? input16 : default16);
    sceImeDialogTerm();
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);  // the dialog resets it
    running = 0;
}

int ime_running(void) { return running; }

int ime_char_get(int i) { return (i >= 0 && i < CHAR_TBL) ? char_tbl[i] : 0; }
int ime_char_num(void) { return char_num; }
int ime_char_finish(void) { return char_finish; }
