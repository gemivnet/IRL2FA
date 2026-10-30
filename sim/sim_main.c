/* Exported C API for the browser simulator (WebAssembly build of the
 * portable core + the virtual board). One WebAssembly instance = one device;
 * the page creates two instances for the two devices.
 *
 * Also keeps the energy counters behind the simulator's battery meter by
 * observing the virtual board's state as simulated time passes, and a few
 * sim-only helpers (demo data, word/code readback for the tests).
 * Nothing here is part of the device firmware.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include <emscripten/emscripten.h>
#include <stdint.h>
#include <string.h>

#include "../firmware/core/app.h"
#include "../firmware/core/base32.h"
#include "../firmware/core/gfx.h"
#include "../firmware/core/hal.h"
#include "../firmware/core/store.h"
#include "../firmware/core/third_party/monocypher/monocypher.h"
#include "../firmware/core/totp.h"
#include "../firmware/core/words.h"
#include "../firmware/vhal/vhal.h"

#define API EMSCRIPTEN_KEEPALIVE

/* ---- energy accounting ---- */
static struct {
    double awake_ms;
    double asleep_ms;
    double ir_ms;
    double busy_ms;                /* e-paper BUSY (refreshing) */
    uint32_t full_base, fast_base; /* refresh counts at the last energy reset */
    uint32_t wakes;
} en;

/* ---- e-paper timing and the joystick latch ----
 * On the board hal_display_show() blocks until the panel drops BUSY (3 s
 * for a full refresh, 0.42 s for a partial one, hardware/power/model.json),
 * the core waiting in WFI. The core cannot poll in that time, but a press
 * is not lost: the virtual board latches press edges like the board's EXTI
 * and filters them the same way (core/buttons.c), so it is reported at the
 * next poll, and a second press during the refresh is a second press. */
static struct {
    uint32_t full_ms, fast_ms;
    uint32_t busy_left;
} ep = {3000, 420, 0};

/* Shared scratch buffer for moving IR bytes and strings across the JS/WASM
 * boundary without malloc. */
static uint8_t io_buf[1024];
static char str_buf[128];

API void sim_set_epd_timing(uint32_t full_ms, uint32_t fast_ms)
{
    ep.full_ms = full_ms;
    ep.fast_ms = fast_ms;
}

/* ms until the panel finishes the refresh in progress (0 = idle). */
API uint32_t sim_busy_ms(void) { return ep.busy_left; }

API void sim_energy_reset(void)
{
    memset(&en, 0, sizeof(en));
    en.full_base = vhal_refreshes(1);
    en.fast_base = vhal_refreshes(0);
}

API double sim_energy_awake_ms(void) { return en.awake_ms; }
API double sim_energy_asleep_ms(void) { return en.asleep_ms; }
API double sim_energy_ir_ms(void) { return en.ir_ms; }
API double sim_energy_busy_ms(void) { return en.busy_ms; }
API uint32_t sim_energy_full(void) { return vhal_refreshes(1) - en.full_base; }
API uint32_t sim_energy_fast(void) { return vhal_refreshes(0) - en.fast_base; }
API uint32_t sim_energy_wakes(void) { return en.wakes; }

/* ---- board / app lifecycle ---- */

API void sim_reset(uint32_t seed_lo, uint32_t seed_hi)
{
    vhal_reset(((uint64_t)seed_hi << 32) | seed_lo);
    ep.busy_left = 0;
    sim_energy_reset();
}

/* Starts BUSY for the refreshes the firmware just sent to the panel. */
static void count_refreshes(uint32_t full0, uint32_t fast0)
{
    ep.busy_left += (vhal_refreshes(1) - full0) * ep.full_ms + (vhal_refreshes(0) - fast0) * ep.fast_ms;
}

API void sim_init(void) { app_init(); }

/* Returns the ms until the app next wants polling, or -1 once it has gone
 * to sleep (the board is then in hal_sleep until sim_wake). */
API int32_t sim_poll(void)
{
    if (vhal_sleeping()) return -1;
    if (ep.busy_left) return (int32_t)ep.busy_left;
    uint32_t full0 = vhal_refreshes(1), fast0 = vhal_refreshes(0);
    uint32_t r = app_poll();
    count_refreshes(full0, fast0);
    if (r == APP_SLEEP) {
        hal_sleep(app_wake_at());
        return -1;
    }
    if (vhal_buttons_pending() && r > 1) r = 1; /* a held-back press: next poll now */
    if (ep.busy_left > r) r = ep.busy_left;
    return (int32_t)r;
}

API void sim_wake(void)
{
    if (!vhal_sleeping()) return;
    vhal_wake();
    /* app_wake() takes what hal_buttons() reports, the latched press
     * included, as the press that woke us, so it is not taken as a key. */
    app_wake();
    en.wakes++;
}

API int sim_sleeping(void) { return vhal_sleeping(); }
API void sim_set_buttons(uint32_t mask)
{
    vhal_set_buttons((uint8_t)mask);
}
API void sim_set_battery_mv(uint32_t mv) { vhal_set_battery_mv((uint16_t)mv); }

/* Moves simulated time forward, charging it to whatever state the board is
 * in (awake/asleep, IR transceiver on/off). */
API void sim_advance(uint32_t ms)
{
    /* A refresh keeps the core awake (waiting in WFI), even the one that
     * draws the sleep screen just before Stop 2. */
    uint32_t busy = ms < ep.busy_left ? ms : ep.busy_left;
    ep.busy_left -= busy;
    en.busy_ms += busy;
    en.awake_ms += busy;
    if (vhal_sleeping()) en.asleep_ms += ms - busy;
    else en.awake_ms += ms - busy;
    if (vhal_ir_on()) en.ir_ms += ms;
    vhal_advance(ms);
}

/* Unix time in (fractional) seconds as a double so JS needs no BigInt. */
API void sim_set_unix(double t) { vhal_set_unix((int64_t)t); }

/* Millisecond-precise variant: whole seconds, then the remainder via the
 * board clock (not charged to the energy counters). */
API void sim_set_unix_ms(double t_ms)
{
    int64_t ms = (int64_t)t_ms;
    int64_t s = ms / 1000;
    int64_t rem = ms - s * 1000;
    if (rem < 0) {
        s -= 1;
        rem += 1000;
    }
    vhal_set_unix(s);
    if (rem) vhal_advance((uint32_t)rem);
}

API double sim_get_unix(void) { return (double)hal_rtc_get(); }
API uint32_t sim_millis(void) { return hal_millis(); }

/* ---- display ---- */
API const uint8_t *sim_framebuffer(void) { return vhal_framebuffer(); }
API uint32_t sim_refreshes(int full) { return vhal_refreshes(full); }
API int sim_display_w(void) { return DISPLAY_W; }
API int sim_display_h(void) { return DISPLAY_H; }
API int sim_display_stride(void) { return DISPLAY_STRIDE; }
API const char *sim_screen_name(void) { return app_screen_name(); }

/* ---- IR ---- */
API uint8_t *sim_io_buf(void) { return io_buf; }
API uint32_t sim_io_cap(void) { return sizeof(io_buf); }
API int sim_ir_on(void) { return vhal_ir_on(); }
API uint32_t sim_ir_take(void) { return (uint32_t)vhal_ir_take(io_buf, sizeof(io_buf)); }
API void sim_ir_rx(uint32_t len)
{
    if (len > sizeof(io_buf)) len = sizeof(io_buf);
    app_ir_rx(io_buf, len);
}

/* ---- sim-only helpers ---- */

API char *sim_str_buf(void) { return str_buf; }
API uint32_t sim_str_cap(void) { return sizeof(str_buf); }
API int sim_unlocked(void) { return store_unlocked(); }
API int sim_pin_set(void) { return store_settings()->pin_set; }
API int sim_simple_mode(void) { return store_settings()->simple; }

/* Adds a SHA1/6-digit/30 s code. Label and base32 secret are passed as
 * "label\0secret\0" in str_buf. Returns the slot, or -1 (locked, invalid
 * secret or full). Call sim_wake-like refresh from JS afterwards. */
API int sim_add_totp(void)
{
    if (!store_unlocked()) return -1;
    const char *label = str_buf;
    const char *secret = str_buf + strlen(label) + 1;
    if (secret >= str_buf + sizeof(str_buf)) return -1;
    totp_rec_t r;
    memset(&r, 0, sizeof(r));
    size_t ll = strlen(label);
    if (ll >= STORE_LABEL_LEN) ll = STORE_LABEL_LEN - 1;
    memcpy(r.label, label, ll);
    int n = base32_decode(secret, r.secret, sizeof(r.secret));
    if (n < 10) return -1;
    r.secret_len = (uint8_t)n;
    r.alg = HASH_SHA1;
    r.digits = 6;
    r.period = 30;
    int slot = store_write_totp(-1, &r);
    crypto_wipe(&r, sizeof(r));
    crypto_wipe(str_buf, sizeof(str_buf));
    return slot;
}

/* Re-enters the first screen (home, or the PIN prompt) with a full refresh,
 * e.g. after demo data changed the store under the UI. */
API void sim_refresh_ui(void)
{
    if (vhal_sleeping()) return;
    app_wake();
}

/* Moves the RTC forward without simulating the time in between (the battery
 * panel's fast-forward). The page charges the skipped time itself, at the
 * chosen usage profile, so the energy counters keep only simulated time. */
API void sim_skip_ms(double ms)
{
    if (ms <= 0) return;
    vhal_set_unix((int64_t)((double)hal_rtc_get() + ms / 1000.0));
}

API int sim_count(int type) { return store_count(type == 1 ? REC_TOTP : REC_CONTACT); }

/* TOTP code of the nth code at the board's current time, or -1. */
API int32_t sim_totp_code(int n)
{
    int slot = store_nth(REC_TOTP, n);
    totp_rec_t r;
    if (slot < 0 || !store_read_totp((uint16_t)slot, &r)) return -1;
    uint32_t c = totp((hash_alg_t)r.alg, r.secret, r.secret_len, (uint64_t)hal_rtc_get(),
                      r.period, r.digits);
    crypto_wipe(&r, sizeof(r));
    return (int32_t)c;
}

/* Copies the nth code's label or the nth contact's name into str_buf. */
API int sim_record_name(int type, int n)
{
    str_buf[0] = '\0';
    int slot = store_nth(type == 1 ? REC_TOTP : REC_CONTACT, n);
    if (slot < 0) return 0;
    if (type == 1) {
        totp_rec_t r;
        if (!store_read_totp((uint16_t)slot, &r)) return 0;
        memcpy(str_buf, r.label, STORE_LABEL_LEN);
        crypto_wipe(&r, sizeof(r));
    } else {
        contact_rec_t r;
        if (!store_read_contact((uint16_t)slot, &r)) return 0;
        memcpy(str_buf, r.name, STORE_LABEL_LEN);
        crypto_wipe(&r, sizeof(r));
    }
    return 1;
}

/* Give (low 16 bits) and receive (high 16 bits) word indices of the nth
 * contact for the 15 s window `offset` windows from now, or -1. */
API int32_t sim_contact_words(int n, int offset)
{
    int slot = store_nth(REC_CONTACT, n);
    contact_rec_t r;
    if (slot < 0 || !store_read_contact((uint16_t)slot, &r)) return -1;
    uint64_t w = (uint64_t)hal_rtc_get() / WORDS_PERIOD_S + (uint64_t)(int64_t)offset;
    uint16_t give, recv;
    words_pair(r.key, r.is_low, w, &give, &recv);
    crypto_wipe(&r, sizeof(r));
    return (int32_t)((uint32_t)give | ((uint32_t)recv << 16));
}

API const char *sim_word(int index) { return words_word((uint16_t)(index & 2047)); }

/* Draws str_buf into the core's scratch framebuffer (not the panel) with
 * one of the firmware fonts so a test can compare it with what the panel
 * shows. align: 0 left at x, 1 centred on x, 2 right edge at x.
 * font: 0 small, 1 medium, 2 large, 3 mono, 4 digits, 5 huge (simple mode). */
API const uint8_t *sim_probe_text(int font, int x, int y, int align)
{
    static const font_t *const F[6] = {&font_small, &font_medium, &font_large, &font_mono,
                                       &font_digits, &font_huge};
    const font_t *f = F[(unsigned)font % 6];
    gfx_clear();
    if (align == 1) gfx_text_centered(f, x, y, str_buf);
    else if (align == 2) gfx_text_right(f, x, y, str_buf);
    else gfx_text(f, x, y, str_buf);
    return gfx_fb;
}
