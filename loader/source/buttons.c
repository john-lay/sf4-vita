/*
 * sf4-vita: which Vita button does which of the game's actions.
 *
 * The game reads a physical controller as a bitmask of Android gamepad bits
 * (GCControllerLib) and, in battle, pairs each touch button with a fixed bit
 * (AppMain::GamePadInit): punch = X, kick = A, special = Y, focus = B,
 * ultra = L1, super = R1, pause = Start. We decide which Vita buttons set
 * each of those bits.
 *
 * The game's Button Configuration screen offers three touch layouts, Type A
 * (PlayStation-style diamond) and Types B/C (Nintendo-style), but they only
 * move the on-screen buttons. So by default the Vita buttons follow the
 * selected type. The game doesn't store the type (only button positions),
 * and its own Check_PadType() comparison reports "custom" even for the
 * default layout here. So we hook AppMain::Set_PadSetting(type), which the
 * settings screen's TYPE button calls (0-2 = Type A-C, 3 = custom), and
 * remember the last A/B/C choice in save/padtype.txt. Moving buttons by hand
 * keeps the mapping of the type they started from.
 *
 * ux0:data/sf4/controls.txt can fix the preset and override single actions;
 * a commented template is written there on first run.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "buttons.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>  // SO_CONTINUE: sceClibMemcpy, sceKernelSyncVMDomain
#ifndef NO_KUBRIDGE
#include <kubridge.h>             // ... or kuKernelFlushCaches
#endif
#include <so_util/so_util.h>

#include "utils/logger.h"

#define CONTROLS_FILE DATA_PATH "controls.txt"
#define PADTYPE_FILE  DATA_PATH "save/padtype.txt"  // last touch layout type picked, 0-2

extern so_module so_mod;

enum { PUNCH, KICK, SPECIAL, FOCUS, SUPER, ULTRA, PAUSE, SELECT, ACTIONS };

static const struct { const char *name; int bit; } actions[ACTIONS] = {
    [PUNCH] = { "punch", 7 },   [KICK] = { "kick", 5 },     // Android X, A
    [SPECIAL] = { "special", 8 }, [FOCUS] = { "focus", 6 }, // Y, B
    [SUPER] = { "super", 10 },  [ULTRA] = { "ultra", 9 },   // R1, L1
    [PAUSE] = { "pause", 16 },  [SELECT] = { "select", 17 },// Start, Select
};

static const struct { const char *name; unsigned mask; } vita_buttons[] = {
    { "cross", SCE_CTRL_CROSS }, { "circle", SCE_CTRL_CIRCLE },
    { "square", SCE_CTRL_SQUARE }, { "triangle", SCE_CTRL_TRIANGLE },
    { "l", SCE_CTRL_LTRIGGER }, { "r", SCE_CTRL_RTRIGGER },
    { "start", SCE_CTRL_START }, { "select", SCE_CTRL_SELECT },
    { "up", SCE_CTRL_UP }, { "down", SCE_CTRL_DOWN },
    { "left", SCE_CTRL_LEFT }, { "right", SCE_CTRL_RIGHT },
};

// The four attacks for touch layouts Type A, B and C, by button position.
static const unsigned presets[3][4] = {
    //        punch            kick             special            focus
    /* A */ { SCE_CTRL_SQUARE, SCE_CTRL_CROSS, SCE_CTRL_TRIANGLE, SCE_CTRL_CIRCLE },
    /* B */ { SCE_CTRL_CROSS, SCE_CTRL_CIRCLE, SCE_CTRL_TRIANGLE, SCE_CTRL_SQUARE },
    /* C */ { SCE_CTRL_SQUARE, SCE_CTRL_TRIANGLE, SCE_CTRL_CROSS, SCE_CTRL_CIRCLE },
};

static int fixed_preset = -1;          // -1 = follow the game's touch layout
static unsigned override[ACTIONS];     // from controls.txt; 0 = not set
static unsigned map[ACTIONS];          // in effect
static int current_type = 0;          // the game's default layout is Type A

static so_hook set_pad_setting;

static const char template_text[] =
    "# sf4-vita button mapping. Lines are \"setting = value\"; # starts a comment.\n"
    "#\n"
    "# preset = auto | a | b | c\n"
    "#   auto (default): follow the touch layout picked in the game's\n"
    "#   Options > Button configuration (Type A, B or C).\n"
    "#   a: punch = square, kick = cross, special = triangle, focus = circle\n"
    "#   b: punch = cross, kick = circle, special = triangle, focus = square\n"
    "#   c: punch = square, kick = triangle, special = cross, focus = circle\n"
    "#\n"
    "# Any action can be set to one or more buttons, overriding the preset:\n"
    "#   punch, kick, special, focus, super, ultra, pause, select\n"
    "# Buttons: cross circle square triangle l r start select up down left right\n"
    "# Defaults: super = r, ultra = l, pause = start, select = select\n"
    "#\n"
    "# Examples:\n"
    "#   preset = a\n"
    "#   super = r triangle\n"
    "\n"
    "preset = auto\n";

static void build_map(void) {
    int type = fixed_preset >= 0 ? fixed_preset : (current_type >= 0 && current_type < 3 ? current_type : 0);
    for (int i = 0; i < 4; i++)
        map[i] = presets[type][i];
    map[SUPER] = SCE_CTRL_RTRIGGER;
    map[ULTRA] = SCE_CTRL_LTRIGGER;
    map[PAUSE] = SCE_CTRL_START;
    map[SELECT] = SCE_CTRL_SELECT;
    for (int i = 0; i < ACTIONS; i++)
        if (override[i])
            map[i] = override[i];
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static void load_config(void) {
    FILE *f = fopen(CONTROLS_FILE, "r");
    if (!f) {
        if ((f = fopen(CONTROLS_FILE, "w"))) {
            fputs(template_text, f);
            fclose(f);
            l_info("buttons: wrote a template to " CONTROLS_FILE);
        }
        return;
    }
    char line[256];
    for (int n = 1; fgets(line, sizeof(line), f); n++) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *eq = strchr(line, '=');
        if (!eq) {
            if (*trim(line)) l_warn("controls.txt:%d: expected \"setting = value\"", n);
            continue;
        }
        *eq = '\0';
        char *key = trim(line), *value = trim(eq + 1);
        for (char *p = key; *p; p++) *p = (char)tolower((unsigned char)*p);
        for (char *p = value; *p; p++) *p = (char)tolower((unsigned char)*p);

        if (!strcmp(key, "preset")) {
            if (!strcmp(value, "auto")) fixed_preset = -1;
            else if (value[0] >= 'a' && value[0] <= 'c' && !value[1]) fixed_preset = value[0] - 'a';
            else l_warn("controls.txt:%d: preset must be auto, a, b or c", n);
            continue;
        }
        int a;
        for (a = 0; a < ACTIONS && strcmp(key, actions[a].name); a++) {}
        if (a == ACTIONS) {
            l_warn("controls.txt:%d: unknown setting \"%s\"", n, key);
            continue;
        }
        unsigned mask = 0;
        for (char *tok = strtok(value, " ,+\t"); tok; tok = strtok(NULL, " ,+\t")) {
            size_t b;
            for (b = 0; b < sizeof(vita_buttons) / sizeof(vita_buttons[0]) && strcmp(tok, vita_buttons[b].name); b++) {}
            if (b == sizeof(vita_buttons) / sizeof(vita_buttons[0]))
                l_warn("controls.txt:%d: unknown button \"%s\"", n, tok);
            else
                mask |= vita_buttons[b].mask;
        }
        if (mask)
            override[a] = mask;
    }
    fclose(f);
    if (fixed_preset < 0)
        l_info("buttons: loaded " CONTROLS_FILE " (preset auto)");
    else
        l_info("buttons: loaded " CONTROLS_FILE " (preset %c)", 'a' + fixed_preset);
}

// AppMain::Set_PadSetting(this, type, flag): applies a touch layout preset.
static void Set_PadSetting_hook(void *self, int type, int flag) {
    SO_CONTINUE(int, set_pad_setting, self, type, flag);  // returns void; int is harmless
    if (type < 0 || type > 2 || type == current_type)
        return;
    current_type = type;
    build_map();
    FILE *f = fopen(PADTYPE_FILE, "w");
    if (f) {
        fprintf(f, "%d\n", type);
        fclose(f);
    }
    l_info("buttons: touch layout Type %c picked; Vita buttons follow it%s", 'A' + type,
           fixed_preset >= 0 ? " (but controls.txt fixes the preset)" : "");
}

void buttons_init(void) {
    FILE *f = fopen(PADTYPE_FILE, "r");
    if (f) {
        int t;
        if (fscanf(f, "%d", &t) == 1 && t >= 0 && t <= 2)
            current_type = t;
        fclose(f);
    }
    uintptr_t fn = so_symbol(&so_mod, "_ZN7AppMain14Set_PadSettingEib");
    if (fn)
        set_pad_setting = hook_addr(fn, (uintptr_t)Set_PadSetting_hook);
    else
        l_error("buttons: AppMain::Set_PadSetting not found; Vita buttons stay on Type A");
    load_config();
    build_map();
    l_info("buttons: Vita buttons use Type %c", 'A' + (fixed_preset >= 0 ? fixed_preset : current_type));
}

int buttons_bits(unsigned vita) {
    // The D-pad always moves; actions come from the map.
    static const unsigned dpad[4] = { SCE_CTRL_LEFT, SCE_CTRL_RIGHT, SCE_CTRL_UP, SCE_CTRL_DOWN };
    int bits = 0;
    for (int i = 0; i < 4; i++)
        if (vita & dpad[i])
            bits |= 1 << i;
    for (int i = 0; i < ACTIONS; i++)
        if (vita & map[i])
            bits |= 1 << actions[i].bit;
    return bits;
}
