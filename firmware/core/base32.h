/* RFC 4648 base32 decoding, tolerant of the ways people write TOTP secrets.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_BASE32_H
#define IRL2FA_BASE32_H

#include <stddef.h>
#include <stdint.h>

/* Decodes `in` (NUL-terminated). Lower case is accepted; spaces, dashes and
 * '=' padding are ignored. Returns the number of bytes written, or -1 on an
 * invalid character or if the output would exceed `out_cap`. */
int base32_decode(const char *in, uint8_t *out, size_t out_cap);

/* Encodes without padding. `out` needs ceil(len*8/5)+1 bytes. Returns length. */
size_t base32_encode(const uint8_t *in, size_t len, char *out);

/* 1 if c is a character the secret-entry wheel offers (A-Z, 2-7). */
int base32_is_alpha(char c);

#endif
