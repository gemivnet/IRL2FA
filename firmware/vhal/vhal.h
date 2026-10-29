/* Virtual board: a hal.h implementation backed by memory, driven by the
 * host. Used by the unit tests and compiled to WebAssembly for the browser
 * simulator. (The simulator's energy counters live in sim/sim_main.c.)
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_VHAL_H
#define IRL2FA_VHAL_H

#include <stddef.h>
#include <stdint.h>

#define VHAL_FLASH_PAGES 40u

void vhal_reset(uint64_t seed);           /* blank flash, time 0, seeded RNG */
void vhal_advance(uint32_t ms);           /* moves both clocks forward */
void vhal_set_unix(int64_t t);
void vhal_set_buttons(uint8_t mask);
void vhal_set_battery_mv(uint16_t mv);

const uint8_t *vhal_framebuffer(void);    /* last frame sent to the panel */
uint32_t vhal_refreshes(int full);        /* count of full (1) / fast (0) */
int vhal_ir_on(void);
size_t vhal_ir_take(uint8_t *buf, size_t cap); /* drains transmitted bytes */
int vhal_sleeping(void);
void vhal_wake(void);                     /* clears the sleeping flag */

uint8_t *vhal_flash(void);                /* VHAL_FLASH_PAGES * 2048 bytes */
uint32_t vhal_flash_erases(void);

/* Every flash operation, fail point for power-cut tests: after `n` more
 * erase/write calls the next one is cut (see vhal_fail_mode) and every
 * later one is dropped. */
void vhal_fail_after(int n);

/* What the cut operation leaves behind. */
typedef enum {
    VHAL_CUT_DROP = 0,    /* nothing: the operation never started */
    VHAL_CUT_HALF = 1,    /* the first half of the page / double-words done */
    VHAL_CUT_GARBAGE = 2, /* the affected bytes are left as noise */
} vhal_cut_t;
void vhal_fail_mode(vhal_cut_t mode);
int vhal_fail_tripped(void);              /* 1 once a cut has happened */
uint32_t vhal_flash_ops(void);            /* erase + write calls so far */

#endif
