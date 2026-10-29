/* Contact verification words.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_WORDS_H
#define IRL2FA_WORDS_H

#include <stdbool.h>
#include <stdint.h>

#define WORDS_PERIOD_S 15u

/* BIP-39 English word number `index` (0..2047). */
const char *words_word(uint16_t index);

/* Give/Receive word indices for the 15-second window `window`
 * (= unix_time / WORDS_PERIOD_S). `is_low` is the role fixed at pairing; the
 * two paired devices hold opposite roles, so one's Give is the other's
 * Receive. */
void words_pair(const uint8_t key[32], bool is_low, uint64_t window,
                uint16_t *give, uint16_t *receive);

#endif
