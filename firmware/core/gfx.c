/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "gfx.h"

#include <string.h>

uint8_t gfx_fb[DISPLAY_BYTES];

void gfx_clear(void)
{
    memset(gfx_fb, 0, sizeof(gfx_fb));
}

void gfx_pixel(int x, int y, bool black)
{
    if (x < 0 || y < 0 || x >= DISPLAY_W || y >= DISPLAY_H) return;
    uint8_t *p = &gfx_fb[y * DISPLAY_STRIDE + x / 8];
    uint8_t m = (uint8_t)(0x80 >> (x & 7));
    if (black) *p |= m;
    else *p &= (uint8_t)~m;
}

void gfx_fill(int x, int y, int w, int h, bool black)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) gfx_pixel(i, j, black);
}

void gfx_invert(int x, int y, int w, int h)
{
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            if (i < 0 || j < 0 || i >= DISPLAY_W || j >= DISPLAY_H) continue;
            gfx_fb[j * DISPLAY_STRIDE + i / 8] ^= (uint8_t)(0x80 >> (i & 7));
        }
    }
}

void gfx_hline(int x, int y, int w)
{
    gfx_fill(x, y, w, 1, true);
}

void gfx_rect(int x, int y, int w, int h)
{
    gfx_fill(x, y, w, 1, true);
    gfx_fill(x, y + h - 1, w, 1, true);
    gfx_fill(x, y, 1, h, true);
    gfx_fill(x + w - 1, y, 1, h, true);
}

static int glyph_index(const font_t *f, char c)
{
    for (int i = 0; i < f->count; i++)
        if (f->chars[i] == c) return i;
    for (int i = 0; i < f->count; i++)
        if (f->chars[i] == '?') return i;
    return -1;
}

/* Control characters 0x18-0x1b and 0x07 draw joystick symbols inline
 * (see GFX_UP etc. in gfx.h). */
static int symbol_size(const font_t *f)
{
    return f->ascent * 2 / 3 | 1;
}

static int symbol(const font_t *f, char c, int x, int y)
{
    static const arrow_t MAP[] = {ARROW_UP, ARROW_DOWN, ARROW_RIGHT, ARROW_LEFT};
    int sz = symbol_size(f);
    int top = y + f->ascent - sz;
    if (c == GFX_DOT[0]) gfx_arrow(ARROW_DOT, x + 1, top, sz);
    else gfx_arrow(MAP[c - 0x18], x + 1, top, sz);
    return sz + 3;
}

static bool is_symbol(char c)
{
    return (c >= 0x18 && c <= 0x1b) || c == GFX_DOT[0];
}

int gfx_text(const font_t *f, int x, int y, const char *s)
{
    for (; *s; s++) {
        if (is_symbol(*s)) {
            x += symbol(f, *s, x, y);
            continue;
        }
        int g = glyph_index(f, *s);
        if (g < 0) continue;
        int w = f->widths[g];
        int stride = (w + 7) / 8;
        const uint8_t *bits = f->bits + f->offsets[g];
        for (int row = 0; row < f->height; row++) {
            for (int col = 0; col < w; col++) {
                if (bits[row * stride + col / 8] & (0x80 >> (col & 7)))
                    gfx_pixel(x + col, y + row, true);
            }
        }
        x += f->advances[g];
    }
    return x;
}

int gfx_text_width(const font_t *f, const char *s)
{
    int w = 0;
    for (; *s; s++) {
        if (is_symbol(*s)) {
            w += symbol_size(f) + 3;
            continue;
        }
        int g = glyph_index(f, *s);
        if (g >= 0) w += f->advances[g];
    }
    return w;
}

void gfx_text_centered(const font_t *f, int cx, int y, const char *s)
{
    gfx_text(f, cx - gfx_text_width(f, s) / 2, y, s);
}

void gfx_text_right(const font_t *f, int right, int y, const char *s)
{
    gfx_text(f, right - gfx_text_width(f, s), y, s);
}

void gfx_text_fit(const font_t *f, int x, int y, int max_w, const char *s)
{
    if (gfx_text_width(f, s) <= max_w) {
        gfx_text(f, x, y, s);
        return;
    }
    char buf[48];
    size_t n = strlen(s);
    if (n > sizeof(buf) - 4) n = sizeof(buf) - 4;
    int ell = gfx_text_width(f, "...");
    while (n > 0) {
        memcpy(buf, s, n);
        buf[n] = '\0';
        if (gfx_text_width(f, buf) + ell <= max_w) break;
        n--;
    }
    memcpy(buf + n, "...", 4);
    gfx_text(f, x, y, buf);
}

void gfx_arrow(arrow_t a, int x, int y, int size)
{
    int h = size / 2;
    if (a == ARROW_DOT) {
        int r = size / 3;
        int cx = x + h, cy = y + h;
        for (int j = -r; j <= r; j++)
            for (int i = -r; i <= r; i++)
                if (i * i + j * j <= r * r) gfx_pixel(cx + i, cy + j, true);
        return;
    }
    /* Filled triangle: for each step along the arrow's axis draw a span
     * that widens from the tip. */
    for (int t = 0; t < size; t++) {
        int half = t / 2;
        for (int s = -half; s <= half; s++) {
            switch (a) {
            case ARROW_UP: gfx_pixel(x + h + s, y + t, true); break;
            case ARROW_DOWN: gfx_pixel(x + h + s, y + size - 1 - t, true); break;
            case ARROW_LEFT: gfx_pixel(x + t, y + h + s, true); break;
            case ARROW_RIGHT: gfx_pixel(x + size - 1 - t, y + h + s, true); break;
            default: break;
            }
        }
    }
}
