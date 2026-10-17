/*
 * sf4-vita: text entry through the Vita IME dialog, see ime.c.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef SF4_IME_H
#define SF4_IME_H

void ime_open(int type, int max_chars, const char *default_text);  // KeyBordBase
void ime_poll(void);     // once per frame
int ime_running(void);   // dialog on screen: swap with it, keep input from the game

int ime_char_get(int i);   // CharGet
int ime_char_num(void);    // CharNumGet
int ime_char_finish(void); // CharFinishGet

#endif
