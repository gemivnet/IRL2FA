/* libFuzzer harness: base32 decoding of a typed TOTP secret.
 *
 * Input byte 0 picks the output capacity; the rest is the text. Checks: the
 * decoder never writes past the capacity, rejects exactly what it should,
 * and decoding then re-encoding round-trips. The input is also used as raw
 * bytes for an encode -> decode round trip.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../core/base32.h"

#define ASSERT(c) do { if (!(c)) abort(); } while (0)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0 || size > 512) return 0;
    size_t cap = data[0] % 72;
    char text[513];
    memcpy(text, data + 1, size - 1);
    text[size - 1] = '\0';

    /* Exact-size heap buffer so ASan catches a write past `cap`. */
    uint8_t *out = malloc(cap ? cap : 1);
    int n = base32_decode(text, out, cap);
    ASSERT(n >= -1 && n <= (int)cap);

    /* Valid iff every character is base32, space, dash or '=', and it fits. */
    size_t sym = 0;
    int valid = 1;
    for (const char *p = text; *p; p++) {
        char c = *p;
        if (c == ' ' || c == '-' || c == '=') continue;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '2' && c <= '7')) sym++;
        else valid = 0;
    }
    if (valid && sym * 5 / 8 <= cap) ASSERT(n == (int)(sym * 5 / 8));
    else ASSERT(n == -1);

    if (n > 0) {
        char enc[72 * 8 / 5 + 2];
        uint8_t again[72];
        size_t len = base32_encode(out, (size_t)n, enc);
        ASSERT(len == ((size_t)n * 8 + 4) / 5 && enc[len] == '\0');
        ASSERT(base32_decode(enc, again, sizeof(again)) == n);
        ASSERT(memcmp(again, out, (size_t)n) == 0);
        for (size_t i = 0; i < len; i++) ASSERT(base32_is_alpha(enc[i]));
    }
    free(out);

    /* encode -> decode of arbitrary bytes */
    size_t m = size - 1 > 64 ? 64 : size - 1;
    char enc[64 * 8 / 5 + 2];
    uint8_t back[64];
    base32_encode(data + 1, m, enc);
    ASSERT(base32_decode(enc, back, sizeof(back)) == (int)m);
    ASSERT(memcmp(back, data + 1, m) == 0);
    return 0;
}
