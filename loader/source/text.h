/*
 * sf4-vita: text drawing for the GlTest2Renderer Text* methods, see text.c.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef SF4_TEXT_H
#define SF4_TEXT_H

void text_init(int w, int h);
void text_begin(void);
void text_draw(const char *str, int x, int y, int r, int g, int b, int a, int size, float scale_x);
void text_end(void);

// Measurements x10, like the Java side.
int text_width10(const char *str, int size);
int text_height10(const char *str, int size);
int text_size10(const char *str, int size);  // (width << 16) | height

// Per-call text logging. The game draws its text every frame, so these lines
// flood the log (and, on a real Vita, the memory card); build with
// -DTEXT_TRACE to see them.
#ifdef TEXT_TRACE
#define text_trace(...) l_debug(__VA_ARGS__)
#else
#define text_trace(...) do {} while (0)
#endif

#endif
