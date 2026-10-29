/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "totp.h"

static const uint32_t POW10[] = {1, 10, 100, 1000, 10000, 100000, 1000000,
                                 10000000, 100000000};

uint32_t hotp(hash_alg_t alg, const uint8_t *key, size_t key_len,
              uint64_t counter, int digits)
{
    uint8_t msg[8], mac[HASH_MAX_DIGEST];
    for (int i = 7; i >= 0; i--) {
        msg[i] = (uint8_t)counter;
        counter >>= 8;
    }
    hmac(alg, key, key_len, msg, sizeof(msg), mac);
    size_t dl = hash_digest_len(alg);
    int off = mac[dl - 1] & 0x0f;
    uint32_t bin = ((uint32_t)(mac[off] & 0x7f) << 24) | ((uint32_t)mac[off + 1] << 16) |
                   ((uint32_t)mac[off + 2] << 8) | mac[off + 3];
    if (digits < 1) digits = 1;
    if (digits > 8) digits = 8;
    return bin % POW10[digits];
}

uint32_t totp(hash_alg_t alg, const uint8_t *key, size_t key_len,
              uint64_t unix_time, uint32_t period, int digits)
{
    if (period == 0) period = 30;
    return hotp(alg, key, key_len, unix_time / period, digits);
}

void totp_format(uint32_t code, int digits, char *out)
{
    for (int i = digits - 1; i >= 0; i--) {
        out[i] = (char)('0' + code % 10);
        code /= 10;
    }
    out[digits] = '\0';
}
