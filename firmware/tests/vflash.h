/* A whole STM32U083 flash (256 KB, 2 KB pages) for the firmware update
 * tests, as fw_flash_t. Behaves like the port's driver on silicon: reads
 * of a double-word whose ECC was left inconsistent by an interrupted
 * program or erase return zeros and fail; programming a double-word that
 * is not erased fails (and counts as a violation). A power cut can be
 * scheduled at any operation: that operation is torn and every later one
 * fails until vflash_power_on(). Writes into the protected range (the
 * record store) are counted as violations.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_VFLASH_H
#define IRL2FA_VFLASH_H

#include <stdbool.h>
#include <stdint.h>

#include "../core/fwimage.h"

#define VF_SIZE (256u * 1024u)
#define VF_PAGE 2048u

typedef enum {
    VF_CUT_DROP = 0,    /* the operation never happened */
    VF_CUT_HALF = 1,    /* half done; the double-word where it stopped is torn */
    VF_CUT_GARBAGE = 2, /* the bytes it touched are noise with bad ECC */
} vf_cut_t;

typedef struct {
    uint8_t mem[VF_SIZE];
    uint8_t torn[VF_SIZE / 8];
    long ops;          /* erase + program calls so far */
    long cut_at;       /* op index to cut, -1 none */
    vf_cut_t mode;
    bool dead;         /* power is off: every operation fails */
    uint32_t prot_lo, prot_hi;
    int violations;
    uint32_t erases;
} vflash_t;

void vflash_init(vflash_t *f);
fw_flash_t vflash_iface(vflash_t *f);
void vflash_cut(vflash_t *f, long at_op, vf_cut_t mode); /* at_op counted from now */
void vflash_power_on(vflash_t *f);                       /* ends a cut */
void vflash_tear(vflash_t *f, uint32_t addr);            /* marks one double-word torn */

#endif
