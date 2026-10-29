/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "vhal.h"

#include <string.h>

#include "../core/buttons.h"
#include "../core/hal.h"
#include "../core/third_party/monocypher/monocypher.h"

static struct {
    uint32_t ms;
    int64_t unix_ms;
    uint8_t buttons;
    uint8_t edges;                 /* press edges since hal_buttons() last ran */
    uint32_t edge_ms[BTN_COUNT];
    btn_filter_t filter;
    bool buttons_again;
    uint32_t random_cost_ms;
    uint16_t battery_mv;
    uint8_t fb[DISPLAY_BYTES];
    uint32_t refresh[2];
    int ir_on;
    uint8_t ir_buf[1024];
    size_t ir_len;
    int sleeping;
    uint8_t flash[VHAL_FLASH_PAGES * HAL_FLASH_PAGE];
    uint32_t erases;
    int fail_after;
    int fail_mode;
    int tripped;
    uint32_t ops;
    uint8_t rng_key[32];
    uint64_t rng_ctr;
} v;

void vhal_reset(uint64_t seed)
{
    memset(&v, 0, sizeof(v));
    memset(v.flash, 0xff, sizeof(v.flash));
    v.battery_mv = 3000;
    v.fail_after = -1;
    for (int i = 0; i < 8; i++) v.rng_key[i] = (uint8_t)(seed >> (8 * i));
}

void vhal_advance(uint32_t ms)
{
    v.ms += ms;
    v.unix_ms += ms;
}

void vhal_set_unix(int64_t t) { v.unix_ms = t * 1000; }
/* Like the board's EXTI latch: a press edge is remembered (with its time)
 * until hal_buttons() runs, even if the button is released first. */
void vhal_set_buttons(uint8_t mask)
{
    uint8_t pressed = (uint8_t)(mask & ~v.buttons);
    for (unsigned i = 0; i < BTN_COUNT; i++)
        if (pressed & (1u << i)) v.edge_ms[i] = v.ms;
    v.edges |= pressed;
    v.buttons = mask;
}

int vhal_buttons_pending(void) { return v.buttons_again; }
void vhal_set_battery_mv(uint16_t mv) { v.battery_mv = mv; }
const uint8_t *vhal_framebuffer(void) { return v.fb; }
uint32_t vhal_refreshes(int full) { return v.refresh[full ? 1 : 0]; }
int vhal_ir_on(void) { return v.ir_on; }
int vhal_sleeping(void) { return v.sleeping; }
void vhal_wake(void) { v.sleeping = 0; }
uint8_t *vhal_flash(void) { return v.flash; }
uint32_t vhal_flash_erases(void) { return v.erases; }
void vhal_fail_after(int n)
{
    v.fail_after = n;
    v.tripped = 0;
}
void vhal_fail_mode(vhal_cut_t mode) { v.fail_mode = (int)mode; }
int vhal_fail_tripped(void) { return v.tripped; }
uint32_t vhal_flash_ops(void) { return v.ops; }

size_t vhal_ir_take(uint8_t *buf, size_t cap)
{
    size_t n = v.ir_len < cap ? v.ir_len : cap;
    memcpy(buf, v.ir_buf, n);
    memmove(v.ir_buf, v.ir_buf + n, v.ir_len - n);
    v.ir_len -= n;
    return n;
}

/* ---- hal.h ---- */

void hal_display_show(const uint8_t *fb, display_mode_t mode)
{
    memcpy(v.fb, fb, DISPLAY_BYTES);
    v.refresh[mode == DISPLAY_FULL ? 1 : 0]++;
}

uint32_t hal_millis(void) { return v.ms; }
int64_t hal_rtc_get(void) { return v.unix_ms / 1000; }
void hal_rtc_set(int64_t t) { v.unix_ms = t * 1000 + v.unix_ms % 1000; }
uint8_t hal_buttons(void)
{
    uint8_t r = btn_filter(&v.filter, v.buttons, v.edges, v.edge_ms, v.ms, &v.buttons_again);
    v.edges = 0;
    return r;
}
void hal_ir_enable(bool on) { v.ir_on = on; }

void hal_ir_send(const uint8_t *buf, size_t len)
{
    if (!v.ir_on) return;
    if (len > sizeof(v.ir_buf) - v.ir_len) len = sizeof(v.ir_buf) - v.ir_len;
    memcpy(v.ir_buf + v.ir_len, buf, len);
    v.ir_len += len;
}

void vhal_set_random_cost_ms(uint32_t ms) { v.random_cost_ms = ms; }

void hal_random(uint8_t *buf, size_t len)
{
    vhal_advance(v.random_cost_ms);
    /* Deterministic ChaCha20 stream: reproducible tests. The simulator
     * reseeds from crypto.getRandomValues at start-up. */
    static const uint8_t nonce[8] = {0};
    memset(buf, 0, len);
    v.rng_ctr = crypto_chacha20_djb(buf, buf, len, v.rng_key, nonce, v.rng_ctr);
}

/* 1: do the operation; 0: drop it; -1: this is the operation the power
 * cut interrupts. */
static int flash_op_allowed(void)
{
    v.ops++;
    if (v.fail_after < 0) return 1;
    if (v.fail_after == 0) {
        if (v.tripped) return 0;
        v.tripped = 1;
        return -1;
    }
    v.fail_after--;
    return 1;
}

/* Leaves `len` bytes at `dst` as an interrupted operation would. */
static void torn(uint8_t *dst, const uint8_t *src, size_t len)
{
    if (v.fail_mode == VHAL_CUT_HALF) {
        size_t half = (len / 2) & ~(size_t)7;
        if (src) memcpy(dst, src, half);
        else memset(dst, 0xff, half);
    } else if (v.fail_mode == VHAL_CUT_GARBAGE) {
        uint32_t x = 0x9e3779b9u ^ v.ops;
        for (size_t i = 0; i < len; i++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            dst[i] = (uint8_t)x;
        }
    }
}

void hal_flash_read(uint32_t off, void *buf, size_t len)
{
    memcpy(buf, v.flash + off, len);
}

void hal_flash_erase(uint32_t page)
{
    if (page >= VHAL_FLASH_PAGES) return;
    int ok = flash_op_allowed();
    if (ok < 0) torn(v.flash + page * HAL_FLASH_PAGE, NULL, HAL_FLASH_PAGE);
    if (ok <= 0) return;
    memset(v.flash + page * HAL_FLASH_PAGE, 0xff, HAL_FLASH_PAGE);
    v.erases++;
}

void hal_flash_write(uint32_t off, const void *buf, size_t len)
{
    int ok = flash_op_allowed();
    if (ok < 0 && off + len <= sizeof(v.flash)) torn(v.flash + off, buf, len);
    if (ok <= 0) return;
    /* Mirror the STM32's rules: 8-byte aligned double-words, each written
     * once after an erase. */
    if ((off & 7) || (len & 7) || off + len > sizeof(v.flash)) {
        __builtin_trap();
    }
    const uint8_t *p = buf;
    for (size_t i = 0; i < len; i += 8) {
        for (int k = 0; k < 8; k++) {
            if (v.flash[off + i + k] != 0xff) __builtin_trap();
        }
        memcpy(v.flash + off + i, p + i, 8);
    }
}

uint32_t hal_flash_pages(void) { return VHAL_FLASH_PAGES; }
uint16_t hal_battery_mv(void) { return v.battery_mv; }

void hal_sleep(int64_t wake_at)
{
    (void)wake_at;
    v.sleeping = 1; /* the host wakes us by calling app_wake() */
}

void hal_device_uid(uint8_t uid[12])
{
    for (int i = 0; i < 12; i++) uid[i] = v.rng_key[i % 8] ^ (uint8_t)(0xa5 + i);
}
