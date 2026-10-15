/*
 * sf4-vita: Vita input -> the game's touch table and gamepad bitmask.
 *
 * Touch follows GlTest2Renderer.TouchSet: 5 slots of
 * {startX, startY, curX, curY, prevX, prevY, phase, active}, coordinates in
 * screen pixels x10, phase 1 = down, 2 = move, 3 = up. Moves are ignored until
 * a frame has been drawn after the touch went down (m_touch_move_ok_flag).
 *
 * The gamepad bitmask uses the bits GCControllerLib derives from Android key
 * codes (docs/java-side.md).
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "input.h"

#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/touch.h>

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
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1) < 1)
        td.reportNum = 0;

#ifdef AUTO_TAP
    // Test hook: -DAUTO_TAP="x,y,frame" presses (x, y) in screen pixels for
    // 6 frames starting at that frame, repeating every 600 frames.
    {
        static const int tap[3] = { AUTO_TAP };
        static int frame;
        int t = frame++ - tap[2];
        if (t >= 0 && t % 600 < 6 && td.reportNum == 0) {
            td.reportNum = 1;
            td.report[0].id = 99;
            td.report[0].x = tap[0] * 2;
            td.report[0].y = tap[1] * 2;
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

void input_poll_pad(void) { sceCtrlPeekBufferPositive(0, &pad, 1); }

int input_gamepad_bits(void) {
    static const struct { unsigned vita; int bit; } map[] = {
        { SCE_CTRL_LEFT, 0 }, { SCE_CTRL_RIGHT, 1 }, { SCE_CTRL_UP, 2 }, { SCE_CTRL_DOWN, 3 },
        { SCE_CTRL_CROSS, 5 },    // A
        { SCE_CTRL_CIRCLE, 6 },   // B
        { SCE_CTRL_SQUARE, 7 },   // X
        { SCE_CTRL_TRIANGLE, 8 }, // Y
        { SCE_CTRL_LTRIGGER, 9 }, // L1
        { SCE_CTRL_RTRIGGER, 10 },// R1
        { SCE_CTRL_START, 16 }, { SCE_CTRL_SELECT, 17 },
    };
    int bits = 0;
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (pad.buttons & map[i].vita)
            bits |= 1 << map[i].bit;
    return bits;
}

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
