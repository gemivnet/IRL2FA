/* 1-bpp proportional bitmap fonts (generated into fonts.c).
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_FONTS_H
#define IRL2FA_FONTS_H

#include <stdint.h>

typedef struct {
    uint8_t height;          /* line height in pixels */
    uint8_t ascent;          /* baseline offset from the top */
    uint8_t count;           /* number of glyphs */
    const char *chars;       /* glyph i renders chars[i] */
    const uint8_t *bits;     /* row-major, each row (width+7)/8 bytes, MSB first */
    const uint16_t *offsets; /* byte offset of glyph i in bits */
    const uint8_t *widths;   /* bitmap width of glyph i */
    const uint8_t *advances; /* cursor advance of glyph i */
} font_t;

extern const font_t font_small;  /* 11px status text */
extern const font_t font_medium; /* 14px bold menus */
extern const font_t font_large;  /* 22px bold words */
extern const font_t font_mono;   /* 15px bold mono text entry */
extern const font_t font_digits; /* 44px bold mono, digits only */
extern const font_t font_huge;   /* 34px bold, lower case a-z: simple mode's word */

#endif
