/* Hardware abstraction layer: everything the portable core needs from the
 * board. Implemented by firmware/port/stm32u0 (the device) and by sim/
 * (the browser simulator and the host tests).
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_HAL_H
#define IRL2FA_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- display: 250 x 122, 1 bit per pixel, row-major, MSB = leftmost ---- */
#define DISPLAY_W 250
#define DISPLAY_H 122
#define DISPLAY_STRIDE ((DISPLAY_W + 7) / 8)
#define DISPLAY_BYTES (DISPLAY_STRIDE * DISPLAY_H)

typedef enum {
    DISPLAY_FULL = 0, /* global refresh: slow, flashes, clears ghosting */
    DISPLAY_FAST = 1, /* partial/fast refresh */
} display_mode_t;

/* Bit set = black pixel. Blocks until the panel has finished updating and
 * leaves the panel powered off. */
void hal_display_show(const uint8_t *fb, display_mode_t mode);

/* ---- time ---- */
uint32_t hal_millis(void);             /* monotonic ms while awake */
int64_t hal_rtc_get(void);             /* UTC seconds since the Unix epoch */
void hal_rtc_set(int64_t unix_time);

/* ---- input: the 5-way switch is the only control ---- */
typedef enum {
    BTN_NONE = 0,
    BTN_UP = 1 << 0,
    BTN_DOWN = 1 << 1,
    BTN_LEFT = 1 << 2,
    BTN_RIGHT = 1 << 3,
    BTN_CENTER = 1 << 4,
} button_t;

uint8_t hal_buttons(void); /* bitmask of currently held buttons */

/* ---- IR link (half duplex, 4800 baud plain UART, inverted so idle = LED off) ---- */
void hal_ir_enable(bool on);                 /* powers the IR transceiver */
void hal_ir_send(const uint8_t *buf, size_t len);
/* Received bytes are pushed to the core with app_ir_rx(). */

/* ---- entropy ---- */
void hal_random(uint8_t *buf, size_t len);   /* cryptographically strong */

/* ---- persistent storage (internal flash region reserved for the store) ----
 * Addresses are offsets into the region. Erased bytes read as 0xFF.
 * Writes are in multiples of 8 bytes at 8-byte aligned offsets into
 * erased flash. */
#define HAL_FLASH_PAGE 2048u
void hal_flash_read(uint32_t off, void *buf, size_t len);
void hal_flash_erase(uint32_t page);
void hal_flash_write(uint32_t off, const void *buf, size_t len);
uint32_t hal_flash_pages(void);              /* pages in the region */

/* ---- power ---- */
uint16_t hal_battery_mv(void);
/* Enters the lowest-power state. Returns when a button (or the RTC alarm, if
 * `wake_at` > 0) wakes the device; on the STM32 port RAM is retained (Stop 2). */
void hal_sleep(int64_t wake_at);

/* ---- identity ---- */
void hal_device_uid(uint8_t uid[12]);

/* ---- firmware updates (docs/firmware-updates.md) ----
 * Versions are (major << 16) | (minor << 8) | patch. */
uint32_t hal_fw_version(void);            /* the running image's, 0 if unknown */
/* Restarts into the bootloader's IR update receiver. Returns only where
 * there is none (the simulator). */
void hal_update_enter(void);
/* This device's own signed update image (the staging slot, when it holds
 * the image now running): its 128-byte header, false if there is none. */
bool hal_update_image(uint8_t hdr[128]);
bool hal_update_read(uint32_t off, uint8_t *buf, size_t len); /* its payload */
/* What the bootloader reported about the last update, once after the
 * restart: -1 nothing, else a fw_err_t (0 = installed), the version in
 * *version. */
int hal_update_result(uint32_t *version);

#endif
