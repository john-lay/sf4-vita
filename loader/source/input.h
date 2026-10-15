/*
 * sf4-vita: Vita input for the game, see input.c.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef SF4_INPUT_H
#define SF4_INPUT_H

void input_init(short *touch_table);   // the short[5*8] passed to NativeUpdate
void input_poll_touch(void);           // before NativeUpdate
void input_frame_drawn(void);          // after NativeDraw (enables touch moves)
void input_poll_pad(void);
int input_gamepad_bits(void);          // getKeyDownData_GamceController
float input_stick(int which);          // 0 = LX, 1 = LY, 2 = RX, 3 = RY

#endif
