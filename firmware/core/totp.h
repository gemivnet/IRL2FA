/* HOTP (RFC 4226) and TOTP (RFC 6238).
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_TOTP_H
#define IRL2FA_TOTP_H

#include <stddef.h>
#include <stdint.h>

#include "hash.h"

uint32_t hotp(hash_alg_t alg, const uint8_t *key, size_t key_len,
              uint64_t counter, int digits);

/* `unix_time` in seconds since the epoch (UTC). period is usually 30. */
uint32_t totp(hash_alg_t alg, const uint8_t *key, size_t key_len,
              uint64_t unix_time, uint32_t period, int digits);

/* Writes the code zero-padded to `digits` characters plus NUL. */
void totp_format(uint32_t code, int digits, char *out);

#endif
