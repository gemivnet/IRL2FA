/* Locking the debug port (readout protection): never automatic, only
 * Settings -> Lock debug port, behind the PIN (and in simple mode behind the
 * family PIN), an explanation, a confirm; the bootloader's answer shown after
 * the restart; the state in About. The bootloader's own checks (signed
 * image, cell >= 2.9 V) are in firmware/renode/update.robot.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include "../core/app.h"
#include "../core/gfx.h"
#include "../core/hal.h"
#include "../core/store.h"
#include "../vhal/vhal.h"
#include "test.h"

static void tap(uint8_t b)
{
    vhal_set_buttons(b);
    app_poll();
    vhal_advance(30);
    vhal_set_buttons(0);
    app_poll();
    vhal_advance(30);
}

static void taps(uint8_t b, int n)
{
    for (int i = 0; i < n; i++) tap(b);
}

static bool on(const char *screen)
{
    return strcmp(app_screen_name(), screen) == 0;
}

static void boot(uint64_t seed)
{
    vhal_reset(seed);
    vhal_set_unix(1800000000);
    app_init();
}

static void enter_pin(const uint8_t *syms, int n)
{
    static const uint8_t B[5] = {0, BTN_UP, BTN_RIGHT, BTN_DOWN, BTN_LEFT};
    for (int i = 0; i < n; i++) tap(B[syms[i]]);
    tap(BTN_CENTER);
}

static bool px(const uint8_t *fb, int x, int y)
{
    return (fb[y * DISPLAY_STRIDE + x / 8] >> (7 - (x & 7))) & 1;
}

/* `s` in font_small somewhere on the panel (as tests/test_simple.c). */
static bool shows(const char *s)
{
    gfx_clear();
    gfx_text(&font_small, 0, 0, s);
    int x0 = DISPLAY_W, y0 = DISPLAY_H, x1 = -1, y1 = -1;
    for (int y = 0; y < DISPLAY_H; y++)
        for (int x = 0; x < DISPLAY_W; x++)
            if (px(gfx_fb, x, y)) {
                if (x < x0) x0 = x;
                if (y < y0) y0 = y;
                if (x > x1) x1 = x;
                if (y > y1) y1 = y;
            }
    if (x1 < 0) return false;
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    const uint8_t *panel = vhal_framebuffer();
    for (int oy = 0; oy + h <= DISPLAY_H; oy++)
        for (int ox = 0; ox + w <= DISPLAY_W; ox++) {
            bool match = true;
            for (int y = 0; y < h && match; y++)
                for (int x = 0; x < w && match; x++)
                    match = px(panel, ox + x, oy + y) == px(gfx_fb, x0 + x, y0 + y);
            if (match) return true;
        }
    return false;
}

/* Settings (on its first row) -> row 6, 7 with a PIN ("Remove PIN" is extra). */
static void select_lock_row(void)
{
    taps(BTN_DOWN, store_settings()->pin_set ? 7 : 6);
    tap(BTN_CENTER);
}

/* Home -> Settings -> Lock debug port. */
static void open_lock_item(void)
{
    taps(BTN_DOWN, 4);
    tap(BTN_CENTER);
    select_lock_row();
}

static uint32_t sleep_now(void)
{
    uint32_t r = 0;
    for (int i = 0; i < 1000 && r != APP_SLEEP; i++) {
        r = app_poll();
        vhal_advance(100);
    }
    return r;
}

TEST(test_app_lock_ui)
{
    vhal_set_debug_lock(0, -1);
    int asked = vhal_debug_lock_requests();
    boot(40);
    /* Never on its own: sleeps, wakes, presses. */
    for (int i = 0; i < 5; i++) {
        CHECK_EQ_INT(sleep_now(), APP_SLEEP);
        app_wake();
        taps(BTN_DOWN, 3);
    }
    CHECK_EQ_INT(sleep_now(), APP_SLEEP);
    app_wake();
    CHECK_EQ_INT(vhal_debug_lock_requests(), asked);
    /* About says it is open. */
    taps(BTN_DOWN, 4);
    tap(BTN_CENTER);
    taps(BTN_DOWN, 3);
    tap(BTN_CENTER);
    CHECK(on("about"));
    CHECK(shows("Debug port open"));
    tap(BTN_CENTER);
    tap(BTN_LEFT);
    /* The explanation, back out of it, then the confirm, back out of that. */
    open_lock_item();
    CHECK(on("lock"));
    CHECK(shows("Updates over IR still work."));
    tap(BTN_LEFT);
    CHECK(on("menu"));
    select_lock_row();
    CHECK(on("lock"));
    tap(BTN_CENTER);
    CHECK(on("confirm"));
    tap(BTN_LEFT);
    CHECK(on("menu"));
    CHECK_EQ_INT(vhal_debug_lock_requests(), asked);
    /* Yes: the request (the virtual board has no bootloader to restart into). */
    select_lock_row();
    tap(BTN_CENTER);
    tap(BTN_CENTER);
    CHECK_EQ_INT(vhal_debug_lock_requests(), asked + 1);
    CHECK(on("message"));

    /* With a PIN: asked again; a wrong one locks the device, not the port. */
    static const uint8_t PIN[] = {1, 2, 3, 4};
    static const uint8_t BAD[] = {3, 3, 3, 3};
    boot(41);
    CHECK_EQ_INT(store_set_pin(PIN, 4), STORE_OK);
    open_lock_item();
    CHECK(on("pin"));
    tap(BTN_LEFT);
    CHECK(on("menu"));
    select_lock_row();
    enter_pin(BAD, 4);
    CHECK(!store_unlocked());
    CHECK_EQ_INT(vhal_debug_lock_requests(), asked + 1);
    enter_pin(PIN, 4);
    open_lock_item();
    enter_pin(PIN, 4);
    CHECK(on("lock"));

    /* Already locked: the item says so and does nothing. */
    vhal_set_debug_lock(1, -1);
    boot(42);
    open_lock_item();
    CHECK(on("message"));
    CHECK_EQ_INT(vhal_debug_lock_requests(), asked + 1);
    tap(BTN_CENTER);
    taps(BTN_DOWN, 3);
    tap(BTN_CENTER);
    CHECK(on("about"));
    CHECK(shows("Debug port locked"));
}

TEST(test_app_lock_result)
{
    /* The bootloader's answer, once, after the restart. */
    for (int r = 1; r <= 4; r++) {
        vhal_set_debug_lock(r == 1 || r == 3, r);
        boot(43);
        CHECK(on("message"));
        tap(BTN_CENTER);
        CHECK(on("menu"));
        boot(43);
        CHECK(on("menu"));
    }
    /* The simulator: no state, nothing to lock, says so. */
    vhal_set_debug_lock(-1, -1);
    boot(44);
    open_lock_item();
    tap(BTN_CENTER);
    tap(BTN_CENTER);
    CHECK(on("message"));
    CHECK(shows("The simulator has no debug port"));
    vhal_set_debug_lock(-1, -1);
}

TEST(test_app_lock_simple_mode)
{
    static const uint8_t FAMILY[] = {1, 1, 2, 2, 3, 3};
    vhal_set_debug_lock(0, -1);
    int asked = vhal_debug_lock_requests();
    boot(45);
    CHECK_EQ_INT(store_set_simple(FAMILY, 6), STORE_OK);
    app_wake();
    app_poll();
    CHECK(on("simple"));
    static const uint8_t KEYS[] = {BTN_DOWN, BTN_CENTER, BTN_RIGHT, BTN_UP, BTN_LEFT};
    for (int i = 0; i < 40; i++) {
        tap(KEYS[i % 5]);
        CHECK(on("simple"));
    }
    CHECK_EQ_INT(vhal_debug_lock_requests(), asked);
    vhal_set_buttons(BTN_CENTER);
    for (int t = 0; t < 10200; t += 50) {
        app_poll();
        vhal_advance(50);
    }
    vhal_set_buttons(0);
    app_poll();
    CHECK(on("pin"));
    enter_pin(FAMILY, 6);
    CHECK(on("menu"));
    open_lock_item();
    CHECK(on("lock"));
    tap(BTN_CENTER);
    tap(BTN_CENTER);
    CHECK_EQ_INT(vhal_debug_lock_requests(), asked + 1);
    vhal_set_debug_lock(-1, -1);
}
