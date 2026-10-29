/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "words.h"

#include <string.h>

#include "hash.h"

void words_pair(const uint8_t key[32], bool is_low, uint64_t window,
                uint16_t *give, uint16_t *receive)
{
    static const char LABEL[] = "IRL2FA words v1";
    uint8_t msg[sizeof(LABEL) - 1 + 8];
    uint8_t mac[32];
    memcpy(msg, LABEL, sizeof(LABEL) - 1);
    for (int i = 7; i >= 0; i--) {
        msg[sizeof(LABEL) - 1 + i] = (uint8_t)window;
        window >>= 8;
    }
    hmac(HASH_SHA256, key, 32, msg, sizeof(msg), mac);

    /* Word for the low side to give: bits 0..10; for the high side: bits
     * 11..21. If they collide, walk forward through the MAC 11 bits at a
     * time until they differ, so the two words of a pair are never equal. */
    uint16_t a = (uint16_t)(((mac[0] << 3) | (mac[1] >> 5)) & 0x7ff);
    uint16_t b = (uint16_t)(((mac[1] << 6) | (mac[2] >> 2)) & 0x7ff);
    for (int i = 3; b == a && i + 1 < 32; i += 2) {
        b = (uint16_t)(((mac[i] << 3) | (mac[i + 1] >> 5)) & 0x7ff);
    }
    if (b == a) b = (uint16_t)((a + 1) & 0x7ff);

    *give = is_low ? a : b;
    *receive = is_low ? b : a;
}
