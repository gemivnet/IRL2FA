/* Simple ("vulnerable") mode: turning it on with a family PIN, the
 * one-contact screen that only up and down move, never drawing the SAY
 * word, leaving with a 10 s centre hold and the family PIN, the escalating
 * waits after wrong family PINs (never a wipe), and the store side of it.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include "../core/app.h"
#include "../core/fonts.h"
#include "../core/gfx.h"
#include "../core/hal.h"
#include "../core/store.h"
#include "../core/words.h"
#include "../vhal/vhal.h"
#include "test.h"

static const uint8_t FAMILY[] = {1, 1, 2, 2, 3, 3}; /* up up right right down down */
static const uint8_t WRONG[] = {3, 3, 3, 3, 3, 3};
static const uint8_t DEVICE_PIN[] = {1, 2, 3, 2};

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

static void enter_pin(const uint8_t *syms, int n)
{
    static const uint8_t B[5] = {0, BTN_UP, BTN_RIGHT, BTN_DOWN, BTN_LEFT};
    for (int i = 0; i < n; i++) tap(B[syms[i]]);
    tap(BTN_CENTER);
}

/* Holds a button for `ms`, polling as the main loop would. */
static void hold(uint8_t b, uint32_t ms)
{
    vhal_set_buttons(b);
    for (uint32_t t = 0; t < ms; t += 50) {
        app_poll();
        vhal_advance(50);
    }
    vhal_set_buttons(0);
    app_poll();
    vhal_advance(30);
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

static void wake(void)
{
    vhal_wake();
    app_wake();
    app_poll();
}

static const char *const NAMES[] = {"Susan", "Grandpa Joe", "Sam"};

static void contact_key(int i, uint8_t key[32])
{
    for (int k = 0; k < 32; k++) key[k] = (uint8_t)(k * 5 + 31 * i + 1);
}

static void add_contacts(int n)
{
    for (int i = 0; i < n; i++) {
        contact_rec_t c;
        memset(&c, 0, sizeof(c));
        strcpy(c.name, NAMES[i]);
        contact_key(i, c.key);
        c.is_low = (uint8_t)(i & 1);
        CHECK(store_write_contact(-1, &c) >= 0);
    }
}

/* ---- looking for text on the panel ----
 * Draws `s` with font f into the core's scratch framebuffer, takes the box
 * around its ink and looks for exactly that box (ink and paper) anywhere in
 * the last frame sent to the panel. */
static bool px(const uint8_t *fb, int x, int y)
{
    return (fb[y * DISPLAY_STRIDE + x / 8] >> (7 - (x & 7))) & 1;
}

static bool shows(const font_t *f, const char *s)
{
    gfx_clear();
    gfx_text(f, 0, 0, s);
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
    for (int inv = 0; inv < 2; inv++) /* black on white, or inverted */
        for (int oy = 0; oy + h <= DISPLAY_H; oy++)
            for (int ox = 0; ox + w <= DISPLAY_W; ox++) {
                bool match = true;
                for (int y = 0; y < h && match; y++)
                    for (int x = 0; x < w && match; x++)
                        match = (px(panel, ox + x, oy + y) ^ inv) == px(gfx_fb, x0 + x, y0 + y);
                if (match) return true;
            }
    return false;
}

static const font_t *const ALL_FONTS[] = {&font_small, &font_medium, &font_large, &font_mono,
                                           &font_huge};

static bool shows_anywhere(const char *s)
{
    for (size_t i = 0; i < sizeof(ALL_FONTS) / sizeof(ALL_FONTS[0]); i++)
        if (shows(ALL_FONTS[i], s)) return true;
    return false;
}

/* Contact i's SAY and HEAR words now. */
static void words_now(int i, const char **say, const char **hear)
{
    uint8_t key[32];
    contact_key(i, key);
    uint16_t give, recv;
    words_pair(key, (i & 1) != 0, (uint64_t)hal_rtc_get() / WORDS_PERIOD_S, &give, &recv);
    *say = words_word(give);
    *hear = words_word(recv);
}

/* The panel shows contact i's HEAR word, large, and its SAY word nowhere. */
static bool shows_contact(int i)
{
    const char *say, *hear;
    words_now(i, &say, &hear);
    bool ok = shows(&font_huge, hear) && !shows_anywhere(say);
    if (!ok) fprintf(stderr, "  contact %d: hear '%s' say '%s'\n", i, hear, say);
    return ok;
}

static void boot(uint64_t seed)
{
    vhal_reset(seed);
    vhal_set_unix(1800000001); /* 1 s into a 15 s word window and a 5 s tick */
    app_init();
    app_poll();
}

/* Settings -> Simple mode -> intro -> family PIN three times. */
static void turn_on_via_settings(void)
{
    taps(BTN_DOWN, 4);
    tap(BTN_CENTER); /* Settings */
    taps(BTN_DOWN, 4);
    tap(BTN_CENTER); /* Simple mode */
    CHECK(on("simple_intro"));
    tap(BTN_UP);     /* nothing */
    CHECK(on("simple_intro"));
    tap(BTN_RIGHT);
    CHECK(on("pin"));
    enter_pin(FAMILY, 6);
    enter_pin(FAMILY, 6);
    enter_pin(FAMILY, 6);
    CHECK(on("message"));
    CHECK(store_settings()->simple);
    tap(BTN_DOWN);
    CHECK(on("simple"));
}

TEST(test_simple_turn_on)
{
    boot(60);
    add_contacts(3);
    /* The intro says what it is; left goes back. */
    taps(BTN_DOWN, 4);
    tap(BTN_CENTER);
    taps(BTN_DOWN, 4);
    tap(BTN_CENTER);
    CHECK(on("simple_intro"));
    tap(BTN_LEFT);
    CHECK(on("menu"));
    /* Too short, then a mismatch on the second and on the third entry,
     * then backing out of an empty PIN: still off. */
    taps(BTN_DOWN, 4);
    tap(BTN_CENTER);
    tap(BTN_CENTER);
    enter_pin(FAMILY, 5); /* at least 6 */
    CHECK(on("pin"));
    enter_pin(FAMILY, 6);
    enter_pin(WRONG, 6);  /* didn't match: choose again */
    enter_pin(FAMILY, 6);
    enter_pin(FAMILY, 6);
    enter_pin(WRONG, 6);  /* the third entry must match too */
    CHECK(on("pin"));
    CHECK(!store_settings()->simple);
    tap(BTN_LEFT);        /* empty: back to settings */
    CHECK(on("menu"));
    CHECK(!store_settings()->simple);
    tap(BTN_LEFT);
    tap(BTN_LEFT);        /* home: sleep */
    CHECK_EQ_INT(sleep_now(), APP_SLEEP);
    wake();
    CHECK(on("menu"));

    turn_on_via_settings();
    CHECK(shows_contact(0));
    CHECK(shows(&font_large, "Susan"));
    CHECK(shows(&font_small, GFX_UP GFX_DOWN " 1 of 3"));
}

TEST(test_simple_only_up_down)
{
    boot(61);
    add_contacts(3);
    CHECK(store_set_simple(FAMILY, 6) == STORE_OK);
    app_init(); /* a reboot goes straight to simple mode */
    app_poll();
    CHECK(on("simple"));
    CHECK(shows_contact(0));

    /* Left, right and a centre press do nothing, not even a redraw. */
    uint32_t before = vhal_refreshes(0) + vhal_refreshes(1);
    tap(BTN_LEFT);
    tap(BTN_RIGHT);
    tap(BTN_CENTER);
    tap(BTN_UP | BTN_LEFT); /* a chord: the lowest bit, up */
    CHECK(on("simple"));
    CHECK_EQ_INT(vhal_refreshes(0) + vhal_refreshes(1), before + 1);
    CHECK(shows_contact(2)); /* up from the first wraps to the last */
    tap(BTN_DOWN);
    CHECK(shows_contact(0));
    tap(BTN_DOWN);
    CHECK(shows_contact(1));
    CHECK(shows(&font_large, "Grandpa Joe"));
    CHECK(shows(&font_small, GFX_UP GFX_DOWN " 2 of 3"));

    /* Holding down does not spin through the list. */
    hold(BTN_DOWN, 2000);
    CHECK(shows_contact(2));

    /* Over several word windows, the SAY word is never on the panel. */
    for (int k = 0; k < 6; k++) {
        vhal_advance(WORDS_PERIOD_S * 1000 - 500);
        tap(BTN_LEFT); /* does nothing, but keeps it awake */
        app_poll();
        CHECK(on("simple"));
        CHECK(shows_contact(2));
    }
    /* A long name is shortened to fit beside "will say:". */
    contact_rec_t c;
    memset(&c, 0, sizeof(c));
    strcpy(c.name, "WWWWWWWWWWWWWWW");
    CHECK(store_write_contact(-1, &c) >= 0);
    tap(BTN_DOWN);
    CHECK(shows(&font_small, GFX_UP GFX_DOWN " 4 of 4"));
    CHECK(shows(&font_medium, " will say:"));
}

TEST(test_simple_refresh_policy)
{
    /* The countdown dots move with partial refreshes that don't count
     * towards a full one; only word changes are content redraws, and the
     * screen has no clock to redraw every minute. */
    boot(62);
    add_contacts(1);
    CHECK(store_set_simple(FAMILY, 6) == STORE_OK);
    app_wake();
    app_poll();
    CHECK(on("simple"));
    CHECK(!shows(&font_small, GFX_UP GFX_DOWN " 1 of 1")); /* one person: no position */
    uint32_t full = vhal_refreshes(1), part = vhal_refreshes(0);
    for (int t = 0; t < 28500; t += 100) { /* to 29.5 s: ticks at 5, 10, 20, 25, word at 15 */
        app_poll();
        vhal_advance(100);
    }
    CHECK_EQ_INT(vhal_refreshes(1), full);
    CHECK_EQ_INT(vhal_refreshes(0), part + 5);
    CHECK(sleep_now() == APP_SLEEP); /* idle timeout as usual */
    CHECK_EQ_INT(vhal_refreshes(1), full + 1);
}

TEST(test_simple_leave_with_family_pin)
{
    boot(63);
    add_contacts(2);
    CHECK(store_set_simple(FAMILY, 6) == STORE_OK);
    app_wake();
    app_poll();

    /* 9.5 s is not enough; after 3 s it says to keep holding. */
    vhal_set_buttons(BTN_CENTER);
    for (int t = 0; t < 9500; t += 50) {
        app_poll();
        vhal_advance(50);
        if (t == 4000) CHECK(shows(&font_small, "Family settings: keep holding " GFX_DOT));
    }
    vhal_set_buttons(0);
    app_poll();
    CHECK(on("simple"));
    CHECK(shows(&font_medium, "hang up and call them back."));
    /* Ten seconds is. */
    hold(BTN_CENTER, 10100);
    CHECK(on("pin"));
    tap(BTN_CENTER); /* empty, and too short to count as a try */
    CHECK_EQ_INT(store_family_fails(), 0);
    enter_pin(FAMILY, 4);
    CHECK_EQ_INT(store_family_fails(), 0);
    tap(BTN_LEFT); /* back */
    CHECK(on("simple"));

    /* Three wrong family PINs are free; then each costs a wait that the
     * screen shows and the keys respect. */
    hold(BTN_CENTER, 10100);
    for (int i = 0; i < 3; i++) enter_pin(WRONG, 6);
    CHECK(on("pin"));
    CHECK_EQ_INT(store_family_fails(), 3);
    CHECK_EQ_INT(store_family_wait_s(hal_rtc_get()), 30);
    CHECK(shows(&font_small, "Try again in 30 s"));
    enter_pin(FAMILY, 6); /* ignored during the wait, even the right one */
    CHECK(on("pin"));
    CHECK_EQ_INT(store_family_fails(), 3);
    tap(BTN_LEFT); /* back still works */
    CHECK(on("simple"));
    /* The wait survives a reboot and sleep (it counts on the RTC). */
    app_init();
    app_poll();
    CHECK(on("simple"));
    hold(BTN_CENTER, 10100);
    CHECK(on("pin"));
    CHECK(store_family_wait_s(hal_rtc_get()) > 0);
    CHECK_EQ_INT(sleep_now(), APP_SLEEP); /* idle on the PIN screen */
    vhal_advance(5000);
    wake();
    CHECK(on("simple"));
    hold(BTN_CENTER, 10100);
    CHECK(on("pin"));
    CHECK_EQ_INT(store_family_wait_s(hal_rtc_get()), 0);
    enter_pin(WRONG, 6);
    CHECK_EQ_INT(store_family_fails(), 4);
    CHECK_EQ_INT(store_family_wait_s(hal_rtc_get()), 60);
    CHECK(shows(&font_small, "Try again in 1 min"));
    CHECK_EQ_INT(sleep_now(), APP_SLEEP);
    vhal_advance(30000);
    wake();
    hold(BTN_CENTER, 10100);
    CHECK(on("pin"));
    CHECK_EQ_INT(store_family_wait_s(hal_rtc_get()), 0);

    /* The right one: the normal menus, with a way back. */
    enter_pin(FAMILY, 6);
    CHECK(on("menu"));
    CHECK_EQ_INT(store_family_fails(), 0);
    CHECK_EQ_INT(store_count(REC_CONTACT), 2); /* nothing erased */
    taps(BTN_DOWN, 5);
    tap(BTN_CENTER); /* Back to simple mode */
    CHECK(on("simple"));

    /* In again, and sleep: waking is simple mode again. */
    hold(BTN_CENTER, 10100);
    enter_pin(FAMILY, 6);
    CHECK(on("menu"));
    tap(BTN_LEFT); /* home: sleep now */
    CHECK_EQ_INT(sleep_now(), APP_SLEEP);
    wake();
    CHECK(on("simple"));

    /* Turn it off from Settings (saying no first). */
    hold(BTN_CENTER, 10100);
    enter_pin(FAMILY, 6);
    taps(BTN_DOWN, 4);
    tap(BTN_CENTER); /* Settings */
    taps(BTN_DOWN, 4);
    tap(BTN_CENTER); /* Simple mode: on */
    CHECK(on("confirm"));
    tap(BTN_LEFT);
    CHECK(on("menu"));
    CHECK(store_settings()->simple);
    taps(BTN_DOWN, 4);
    tap(BTN_CENTER);
    tap(BTN_CENTER);
    CHECK(on("message"));
    CHECK(!store_settings()->simple);
    CHECK_EQ_INT(sleep_now(), APP_SLEEP);
    wake();
    CHECK(on("menu"));
    CHECK_EQ_INT(store_check_family_pin(FAMILY, 6, hal_rtc_get()), STORE_ERR);
}

TEST(test_simple_with_device_pin)
{
    /* A device PIN still applies on wake; after it, simple mode. */
    boot(64);
    add_contacts(1);
    CHECK(store_set_pin(DEVICE_PIN, 4) == STORE_OK);
    CHECK(store_set_simple(FAMILY, 6) == STORE_OK);
    app_init();
    app_poll();
    CHECK(on("pin"));
    enter_pin(DEVICE_PIN, 4);
    CHECK(on("simple"));
    CHECK(shows_contact(0));
    CHECK_EQ_INT(sleep_now(), APP_SLEEP);
    CHECK(!store_unlocked());
    wake();
    CHECK(on("pin"));
    enter_pin(DEVICE_PIN, 4);
    CHECK(on("simple"));
    /* The family PIN is not the device PIN, and leaving works as usual. */
    hold(BTN_CENTER, 10100);
    enter_pin(DEVICE_PIN, 4);
    CHECK(on("pin"));
    enter_pin(FAMILY, 6);
    CHECK(on("menu"));
}

TEST(test_simple_plain_messages)
{
    /* Nobody paired yet. */
    boot(65);
    CHECK(store_set_simple(FAMILY, 6) == STORE_OK);
    app_wake();
    app_poll();
    CHECK(on("simple"));
    CHECK(shows(&font_medium, "Ask your family to set this up."));
    tap(BTN_DOWN); /* nothing to move to */
    CHECK(on("simple"));
    /* A plain message does not change with time: no redraws at all. */
    uint32_t before = vhal_refreshes(0) + vhal_refreshes(1);
    for (int t = 0; t < 25000; t += 100) {
        app_poll();
        vhal_advance(100);
    }
    CHECK_EQ_INT(vhal_refreshes(0) + vhal_refreshes(1), before);
    /* Low battery, in words. */
    vhal_set_battery_mv(2450);
    app_wake();
    app_poll();
    CHECK(shows(&font_small, "Battery low: tell your family"));
    /* A clock that lost the time: no (wrong) words, a plain message. */
    add_contacts(1);
    vhal_set_unix(1000);
    app_wake();
    app_poll();
    CHECK(shows(&font_medium, "Ask your family to set it."));
    hold(BTN_CENTER, 10100); /* the family can still get in */
    enter_pin(FAMILY, 6);
    CHECK(on("menu"));
}

/* ---- the store side ---- */

TEST(test_store_family_pin)
{
    vhal_reset(66);
    store_mount();
    add_contacts(2);
    CHECK_EQ_INT(store_check_family_pin(FAMILY, 6, 1000), STORE_ERR); /* off */
    CHECK_EQ_INT(store_set_simple(FAMILY, 5), STORE_ERR);              /* too short */
    CHECK_EQ_INT(store_set_simple(FAMILY, 13), STORE_ERR);
    CHECK_EQ_INT(store_set_simple(FAMILY, 6), STORE_OK);
    CHECK(store_settings()->simple);
    store_mount();
    CHECK(store_settings()->simple);

    /* The waits: none for three, then 30 s, 1, 2, 5, 10, 30, 60 min. */
    static const uint32_t WAIT[] = {0, 0, 30, 60, 120, 300, 600, 1800, 3600, 3600, 3600};
    int64_t t = 1800000000;
    for (int i = 0; i < 11; i++) {
        CHECK_EQ_INT(store_check_family_pin(WRONG, 6, t), STORE_BAD_PIN);
        CHECK_EQ_INT(store_family_fails(), i + 1);
        CHECK_EQ_INT(store_family_wait_s(t), WAIT[i]);
        if (WAIT[i]) {
            CHECK_EQ_INT(store_check_family_pin(FAMILY, 6, t + WAIT[i] - 1), STORE_WAIT);
            CHECK_EQ_INT(store_family_wait_s(t + WAIT[i] - 1), 1);
        }
        t += WAIT[i];
    }
    /* Never a wipe: everything is still there, across a remount too. */
    store_mount();
    CHECK_EQ_INT(store_count(REC_CONTACT), 2);
    CHECK_EQ_INT(store_family_fails(), 11);
    CHECK_EQ_INT(store_pin_fails(), 0); /* the device PIN count is separate */

    /* The clock went back (battery change): the wait counts from now, it
     * is neither a lockout nor a free try. */
    CHECK_EQ_INT(store_family_wait_s(t - 100000), 3600);
    CHECK_EQ_INT(store_family_wait_s(t - 100000 + 1800), 1800);
    CHECK_EQ_INT(store_family_wait_s(t - 100000 + 3600), 0);
    t = t - 100000 + 3600;

    CHECK_EQ_INT(store_check_family_pin(FAMILY, 6, t), STORE_OK);
    CHECK_EQ_INT(store_family_fails(), 0);
    CHECK_EQ_INT(store_family_wait_s(t), 0);

    /* Many attempts compact the log; both counts survive it. */
    static const uint8_t PIN[] = {1, 2, 3, 4};
    static const uint8_t BAD[] = {4, 4, 4, 4};
    CHECK(store_set_pin(PIN, 4) == STORE_OK);
    store_check_family_pin(WRONG, 6, t);
    store_check_family_pin(WRONG, 6, t);
    store_lock();
    store_unlock(BAD, 4);
    for (int i = 0; i < 300; i++) {
        store_lock();
        CHECK(store_unlock(PIN, 4) == STORE_OK);
    }
    store_lock();
    store_unlock(BAD, 4);
    CHECK_EQ_INT(store_pin_fails(), 1);
    CHECK_EQ_INT(store_family_fails(), 2);
    CHECK(store_unlock(PIN, 4) == STORE_OK);
    CHECK_EQ_INT(store_check_family_pin(FAMILY, 6, t), STORE_OK);

    /* Turning it off forgets the verifier. */
    CHECK_EQ_INT(store_set_simple(NULL, 0), STORE_OK);
    CHECK(!store_settings()->simple);
    CHECK_EQ_INT(store_check_family_pin(FAMILY, 6, t), STORE_ERR);
    store_lock();
    CHECK_EQ_INT(store_set_simple(FAMILY, 6), STORE_LOCKED);
}

TEST(test_store_family_damaged)
{
    /* A settings page whose sealed verifier no longer opens must not lock
     * the family out: the check turns simple mode off. */
    vhal_reset(67);
    store_mount();
    CHECK_EQ_INT(store_set_simple(FAMILY, 6), STORE_OK);
    vhal_flash()[96 + 8 + 24 + 3] ^= 0x10; /* a bit of the verifier's MAC */
    store_mount();
    CHECK(store_settings()->simple);
    CHECK_EQ_INT(store_check_family_pin(WRONG, 6, 1800000000), STORE_OK);
    CHECK(!store_settings()->simple);
    store_mount();
    CHECK(!store_settings()->simple);

    /* A store written before simple mode existed (0xff after the old
     * 96-byte settings) mounts with it off. */
    vhal_reset(68);
    store_mount();
    memset(vhal_flash() + 96, 0xff, 96);
    store_mount();
    CHECK(!store_settings()->simple);
    CHECK(store_unlocked());
}
