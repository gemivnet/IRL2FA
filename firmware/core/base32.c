/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "base32.h"

static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static int b32_value(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

int base32_is_alpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7');
}

int base32_decode(const char *in, uint8_t *out, size_t out_cap)
{
    uint32_t acc = 0;
    int bits = 0;
    size_t n = 0;
    for (; *in; in++) {
        char c = *in;
        if (c == ' ' || c == '-' || c == '=') continue;
        int v = b32_value(c);
        if (v < 0) return -1;
        acc = (acc << 5) | (uint32_t)v;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            if (n >= out_cap) return -1;
            out[n++] = (uint8_t)(acc >> bits);
        }
    }
    return (int)n;
}

size_t base32_encode(const uint8_t *in, size_t len, char *out)
{
    uint32_t acc = 0;
    int bits = 0;
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        acc = (acc << 8) | in[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out[n++] = ALPHABET[(acc >> bits) & 31];
        }
    }
    if (bits > 0) out[n++] = ALPHABET[(acc << (5 - bits)) & 31];
    out[n] = '\0';
    return n;
}
