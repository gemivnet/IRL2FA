/* Helpers shared by the firmware update tests (test_fwupdate.c) and the
 * application's update screens (test_app.c).
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_TEST_FW_H
#define IRL2FA_TEST_FW_H

#include <stddef.h>
#include <stdint.h>

#include "../core/fwimage.h"

#define TFW_HW_ID 0x00830001u

extern const fw_layout_t TFW_LAYOUT;
extern const uint8_t TFW_TEST_SEED[32];

void tfw_public(uint8_t pub[32], const uint8_t seed[32]);
void tfw_sign(uint8_t sig[64], const uint8_t seed[32], const uint8_t *msg, size_t len);
size_t tfw_sample_plain(uint8_t *out, size_t n);
/* Header + payload into `out`; seed NULL: an unsigned development image. */
size_t tfw_build(uint8_t *out, const uint8_t *bin, uint32_t n, const uint8_t *payload,
                 uint32_t plen, uint8_t comp, uint32_t version, const uint8_t *seed, uint32_t hw,
                 uint32_t min_boot);

#endif
