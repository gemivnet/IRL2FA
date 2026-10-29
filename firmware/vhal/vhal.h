/* Virtual board: a hal.h implementation backed by memory, driven by the
 * host. Used by the unit tests and compiled to WebAssembly for the browser
 * simulator. It also keeps the energy counters behind the simulator's
 * battery meter (see hardware/power/model.json).
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
 * erase/write calls the next one is dropped and every later one too. */
void vhal_fail_after(int n);

#endif
