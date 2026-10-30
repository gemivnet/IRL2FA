/* Behaviour the emulated hardware showed up: presses lost during a display
 * refresh, countdown redraws driving full refreshes, and the IR pairing
 * link. Runs the core on the virtual board.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include "../core/app.h"
#include "../core/buttons.h"
#include "../core/hal.h"
#include "../core/hash.h"
#include "../core/store.h"
#include "../vhal/vhal.h"
#include "test.h"

/* ---- the press filter ---- */

TEST(test_buttons_filter)
{
    btn_filter_t f;
    memset(&f, 0, sizeof(f));
    uint32_t edge[BTN_COUNT] = {0};
    bool again;
    const uint8_t D = BTN_DOWN, i = 1; /* BTN_DOWN is bit 1 */

    /* A press latched and already released is still reported. */
    edge[i] = 0;
    CHECK_EQ_INT(btn_filter(&f, 0, D, edge, 0, &again), D);
    CHECK(!again);
    /* Bounce: an edge 3 ms after it was seen down is the same press. */
    edge[i] = 3;
    CHECK_EQ_INT(btn_filter(&f, 0, D, edge, 420, &again), D);
    CHECK(!again);
    /* Released after the debounce time. */
    CHECK_EQ_INT(btn_filter(&f, 0, 0, edge, 450, &again), 0);

    /* The lost-press case: pressed at 1000 and seen; the core then blocks
     * in a 0.42 s refresh; released at 1050 and pressed again at 1300. */
    edge[i] = 1000;
    CHECK_EQ_INT(btn_filter(&f, D, D, edge, 1000, &again), D);
    edge[i] = 1300;
    CHECK_EQ_INT(btn_filter(&f, 0, D, edge, 1420, &again), 0); /* a release first */
    CHECK(again);
    CHECK_EQ_INT(btn_filter(&f, 0, 0, edge, 1421, &again), D); /* then the second press */
    CHECK(!again);
    CHECK_EQ_INT(btn_filter(&f, 0, 0, edge, 1460, &again), 0);
}

/* Polls the app as the board's main loop would, with no input change. */
static void run(uint32_t ms)
{
    for (uint32_t t = 0; t < ms;) {
        uint32_t r = app_poll();
        uint32_t step = vhal_buttons_pending() ? 1 : (r == APP_SLEEP ? ms - t : (r < 20 ? r : 20));
        if (step == 0) step = 1;
        vhal_advance(step);
        t += step;
    }
}

TEST(test_app_press_during_refresh)
{
    vhal_reset(9);
    vhal_set_unix(1800000000);
    app_init();
    run(200);
    CHECK(strcmp(app_screen_name(), "menu") == 0);
    /* DOWN, seen at once; the core then draws for 0.42 s and cannot poll.
     * Meanwhile the person releases and presses DOWN again. */
    vhal_set_buttons(BTN_DOWN);
    app_poll();
    vhal_advance(60);
    vhal_set_buttons(0);
    vhal_advance(200);
    vhal_set_buttons(BTN_DOWN);
    vhal_advance(60);
    vhal_set_buttons(0);
    vhal_advance(100);
    run(300);
    vhal_set_buttons(BTN_CENTER);
    run(60);
    vhal_set_buttons(0);
    run(100);
    /* Two rows down: "Pair with someone". */
    CHECK(strcmp(app_screen_name(), "pair") == 0);
}

/* Like run(), but stops when the app goes to sleep; returns whether it did. */
static bool run_until_sleep(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 20) {
        if (app_poll() == APP_SLEEP) return true;
        vhal_advance(20);
    }
    return false;
}

static void tap(uint8_t b)
{
    vhal_set_buttons(b);
    run(60);
    vhal_set_buttons(0);
    run(60);
}

/* ---- refresh policy ---- */

TEST(test_app_refresh_policy)
{
    vhal_reset(11);
    vhal_set_unix(1800000000); /* a 30 s boundary */
    app_init();
    run(100);
    totp_rec_t t;
    memset(&t, 0, sizeof(t));
    strcpy(t.label, "A");
    t.secret_len = 10;
    t.alg = HASH_SHA1;
    t.digits = 6;
    t.period = 30;
    CHECK(store_write_totp(-1, &t) >= 0);
    app_wake(); /* redraw the home menu with the new count */
    run(100);
    tap(BTN_CENTER);
    tap(BTN_CENTER);
    CHECK(strcmp(app_screen_name(), "code") == 0);
    tap(BTN_DOWN); /* one code: redraws the same one */
    tap(BTN_DOWN);
    /* Five partial refreshes since the boot screen's full one. */
    uint32_t full = vhal_refreshes(1), part = vhal_refreshes(0);
    CHECK_EQ_INT(full, 1);
    CHECK_EQ_INT(part, 5);
    /* The bar moves at 10 and 20 s: partial refreshes, even though the
     * next content change is due a full one. */
    run(24000);
    CHECK_EQ_INT(vhal_refreshes(1), full);
    CHECK_EQ_INT(vhal_refreshes(0), part + 2);
    /* At 30 s the code changes: that redraw is the full one (the presses
     * took 0.7 s, so this is 30.3 s in, before the 30 s idle timeout). */
    run(5600);
    CHECK_EQ_INT(vhal_refreshes(1), full + 1);
    /* The idle timeout: the sleep screen is a full refresh... */
    CHECK(run_until_sleep(5000));
    CHECK_EQ_INT(vhal_refreshes(1), full + 2);
    /* ...so waking can be a partial one. */
    part = vhal_refreshes(0);
    vhal_set_buttons(BTN_CENTER);
    app_wake();
    vhal_set_buttons(0);
    run(100);
    CHECK_EQ_INT(vhal_refreshes(1), full + 2);
    CHECK_EQ_INT(vhal_refreshes(0), part + 1);
}

/* ---- pairing: listen before talk, timeout ---- */

TEST(test_pair_listen_before_talk)
{
    vhal_reset(12);
    vhal_set_unix(1800000000);
    app_init();
    run(100);
    tap(BTN_DOWN);
    tap(BTN_DOWN);
    tap(BTN_CENTER);
    CHECK(strcmp(app_screen_name(), "pair") == 0);
    uint8_t buf[256];
    bool sent = false;
    for (int i = 0; i < 50 && !sent; i++) {
        run(20);
        sent = vhal_ir_take(buf, sizeof(buf)) > 0;
    }
    CHECK(sent);
    /* The other device talks for 0.6 s, a byte every 2 ms: we stay quiet. */
    size_t during = 0;
    const uint8_t noise = 0x00;
    for (int i = 0; i < 300; i++) {
        app_ir_rx(&noise, 1);
        app_poll();
        vhal_advance(2);
        during += vhal_ir_take(buf, sizeof(buf));
    }
    CHECK_EQ_INT(during, 0);
    /* Then the channel is quiet and we send again. */
    size_t after = 0;
    for (int i = 0; i < 30; i++) {
        run(20);
        after += vhal_ir_take(buf, sizeof(buf));
    }
    CHECK(after > 0);
}

TEST(test_pair_timeout)
{
    vhal_reset(13);
    vhal_set_unix(1800000000);
    app_init();
    run(100);
    tap(BTN_DOWN);
    tap(BTN_DOWN);
    /* Starting a session takes time on the device (TRNG, X25519): the
     * clock moves on inside the poll that starts it. */
    vhal_set_random_cost_ms(150);
    tap(BTN_CENTER);
    vhal_set_random_cost_ms(0);
    CHECK(strcmp(app_screen_name(), "pair") == 0);
    uint8_t buf[256];
    /* Nobody answers: still looking (and awake) at 59 s... */
    CHECK(!run_until_sleep(59000));
    while (vhal_ir_take(buf, sizeof(buf))) {
    }
    CHECK(strcmp(app_screen_name(), "pair") == 0);
    CHECK(vhal_ir_on());
    /* ...back to the menu with IR off after 60 s... */
    CHECK(!run_until_sleep(2000));
    CHECK(strcmp(app_screen_name(), "message") == 0);
    CHECK(!vhal_ir_on());
    /* ...and asleep after the idle timeout. */
    CHECK(run_until_sleep(35000));
}

/* ---- a key jammed down (in a pocket) ---- */

/* docs/firmware-hardware-review.md: a held UP/DOWN used to auto-repeat
 * every 90 ms for as long as it stayed down, each repeat a panel refresh,
 * and a held key of any kind kept the device out of sleep: a joystick
 * pressed in a pocket flattened the cell in days. Now a key held past
 * HOLD_LIMIT_MS stops repeating and stops counting as activity. */
TEST(test_app_stuck_key_sleeps)
{
    vhal_reset(12);
    vhal_set_unix(1800000000);
    app_init();
    run(200);
    CHECK(strcmp(app_screen_name(), "menu") == 0);
    uint32_t before = vhal_refreshes(0) + vhal_refreshes(1);
    vhal_set_buttons(BTN_DOWN); /* and it stays down */
    /* 20 s of repeats, then the 30 s idle timeout: asleep within a minute,
     * with the DOWN contact still closed. */
    CHECK(run_until_sleep(60000));
    uint32_t during = vhal_refreshes(0) + vhal_refreshes(1) - before;
    /* At most one refresh per repeat for 20 s, plus the sleep screen:
     * bounded, not one every 90 ms for ever. */
    CHECK(during <= 20000 / 90 + 2);
    vhal_set_buttons(0);
    vhal_wake();
    app_wake();
    run(200);
    CHECK(strcmp(app_screen_name(), "menu") == 0);

    /* CENTER held (no auto-repeat) used to keep the device awake too. */
    vhal_set_buttons(BTN_CENTER);
    CHECK(run_until_sleep(60000));
    vhal_set_buttons(0);
}
