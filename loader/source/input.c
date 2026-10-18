/*
 * sf4-vita: Vita input -> the game's touch table and gamepad bitmask.
 *
 * Touch follows GlTest2Renderer.TouchSet: 5 slots of
 * {startX, startY, curX, curY, prevX, prevY, phase, active}, coordinates in
 * screen pixels x10, phase 1 = down, 2 = move, 3 = up. Moves are ignored until
 * a frame has been drawn after the touch went down (m_touch_move_ok_flag).
 *
 * The gamepad bitmask uses the bits GCControllerLib derives from Android key
 * codes (docs/java-side.md); which Vita button sets which bit is decided in
 * buttons.c.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "input.h"

#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/touch.h>

#include "buttons.h"
#include "ime.h"
#include "utils/logger.h"

#define SLOTS 5

static short *table;
static int move_ok;
static struct { int active, report_id, x10, y10; } slot[SLOTS];

static void touch_set(int i, int x10, int y10, int phase) {
    short *s = &table[i * 8];
    if (phase != 2)
        l_debug("touch slot %d %s at (%d, %d)", i, phase == 1 ? "down" : "up", x10 / 10, y10 / 10);
    switch (phase) {
    case 1:  // down
        s[0] = s[2] = s[4] = (short)x10;
        s[1] = s[3] = s[5] = (short)y10;
        s[6] = 1;
        s[7] = 1;
        move_ok = 0;
        break;
    case 2:  // move
        if (!move_ok || s[7] == 0 || (s[2] == x10 && s[3] == y10))
            return;
        s[4] = s[2]; s[5] = s[3];
        s[2] = (short)x10; s[3] = (short)y10;
        s[6] = 2;
        s[7] = 1;
        break;
    case 3:  // up
        s[4] = s[2]; s[5] = s[3];
        s[2] = (short)x10; s[3] = (short)y10;
        s[6] = 3;
        s[7] = 0;
        break;
    }
}

void input_init(short *touch_table) {
    table = touch_table;
    memset(slot, 0, sizeof(slot));
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
}

void input_poll_touch(void) {
    SceTouchData td;
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1) < 1 || ime_running())
        td.reportNum = 0;  // the IME dialog has the touch screen

#ifdef AUTO_TAP
    // Test hook: -DAUTO_TAP="x,y,ms[,x,y,ms...]" presses each (x, y), in
    // screen pixels, for 150 ms starting that many ms after the first frame.
    {
        static const int tap[] = { AUTO_TAP };
        static SceUInt64 start;
        if (!start)
            start = sceKernelGetProcessTimeWide();
        int ms = (int)((sceKernelGetProcessTimeWide() - start) / 1000);
        for (size_t i = 0; i + 2 < sizeof(tap) / sizeof(tap[0]); i += 3) {
            int t = ms - tap[i + 2];
            if (t >= 0 && t < 150 && td.reportNum == 0) {
                td.reportNum = 1;
                td.report[0].id = 99;
                td.report[0].x = tap[i] * 2;
                td.report[0].y = tap[i + 1] * 2;
            }
        }
    }
#endif

    int seen[SLOTS] = { 0 };
    for (int r = 0; r < td.reportNum && r < SLOTS; r++) {
        // Front panel is 1920x1088; the game was told 960x544.
        int x10 = td.report[r].x * 10 / 2, y10 = td.report[r].y * 10 / 2;
        int id = td.report[r].id, i;
        for (i = 0; i < SLOTS && !(slot[i].active && slot[i].report_id == id); i++) {}
        if (i < SLOTS) {
            touch_set(i, x10, y10, 2);
        } else {
            for (i = 0; i < SLOTS && slot[i].active; i++) {}
            if (i == SLOTS) continue;
            slot[i].active = 1;
            slot[i].report_id = id;
            touch_set(i, x10, y10, 1);
        }
        slot[i].x10 = x10;
        slot[i].y10 = y10;
        seen[i] = 1;
    }
    for (int i = 0; i < SLOTS; i++)
        if (slot[i].active && !seen[i]) {
            touch_set(i, slot[i].x10, slot[i].y10, 3);
            slot[i].active = 0;
        }
}

void input_frame_drawn(void) { move_ok = 1; }

/* ---- gamepad ----------------------------------------------------------------- */

static SceCtrlData pad;

void input_poll_pad(void) {
    sceCtrlPeekBufferPositive(0, &pad, 1);
    if (ime_running()) {  // the IME dialog has the buttons
        pad.buttons = 0;
        pad.lx = pad.ly = pad.rx = pad.ry = 128;
    }
}

int input_gamepad_bits(void) { return buttons_bits(pad.buttons); }  // mapping in buttons.c

static float axis(unsigned char v) {
    float f = (v - 128) / 127.0f;
    if (f > -0.15f && f < 0.15f) return 0.0f;  // dead zone
    return f > 1.0f ? 1.0f : (f < -1.0f ? -1.0f : f);
}

float input_stick(int which) {
    switch (which) {
    case 0: return axis(pad.lx);
    case 1: return axis(pad.ly);
    case 2: return axis(pad.rx);
    default: return axis(pad.ry);
    }
}
