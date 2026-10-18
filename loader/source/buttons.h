/*
 * sf4-vita: Vita buttons -> the game's controller actions, see buttons.c.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef SF4_BUTTONS_H
#define SF4_BUTTONS_H

void buttons_init(void);             // after NativeInit: read controls.txt, follow the touch layout type
int buttons_bits(unsigned vita);     // SceCtrlData.buttons -> GCControllerLib bitmask

#endif
