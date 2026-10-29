/* Drawing into the 1-bpp framebuffer.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_GFX_H
#define IRL2FA_GFX_H

#include <stdbool.h>
#include <stdint.h>

#include "fonts.h"
#include "hal.h"

typedef enum { ARROW_UP, ARROW_DOWN, ARROW_LEFT, ARROW_RIGHT, ARROW_DOT } arrow_t;

/* Inline joystick symbols for use in strings, e.g. GFX_UP GFX_DOWN " move". */
#define GFX_UP "\x18"
#define GFX_DOWN "\x19"
#define GFX_RIGHT "\x1a"
#define GFX_LEFT "\x1b"
#define GFX_DOT "\x07"

extern uint8_t gfx_fb[DISPLAY_BYTES];

void gfx_clear(void);
void gfx_pixel(int x, int y, bool black);
void gfx_fill(int x, int y, int w, int h, bool black);
void gfx_rect(int x, int y, int w, int h);
void gfx_hline(int x, int y, int w);
void gfx_invert(int x, int y, int w, int h);

/* Draws text with its top-left at (x, y); returns the x after the text. */
int gfx_text(const font_t *f, int x, int y, const char *s);
int gfx_text_width(const font_t *f, const char *s);
void gfx_text_centered(const font_t *f, int cx, int y, const char *s);
void gfx_text_right(const font_t *f, int right, int y, const char *s);
/* Truncates with "..." to fit `max_w`. */
void gfx_text_fit(const font_t *f, int x, int y, int max_w, const char *s);

/* Arrow / dot symbol filling a size x size box at (x, y). */
void gfx_arrow(arrow_t a, int x, int y, int size);

#endif
