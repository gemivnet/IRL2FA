/* Whole-app UI flows on the virtual board: pairing with a peer through the
 * screens, PINs, settings, editing codes and contacts, first boot.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include "../core/app.h"
#include "../core/hal.h"
#include "../core/pairing.h"
#include "../core/store.h"
#include "../vhal/vhal.h"
#include "test.h"

static const char CS_LABEL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_.@+&'#";
static const char CS_BASE32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

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

/* Types s on a fresh text screen (the wheel starts at the charset's first
 * character). */
static void type(const char *charset, const char *s)
{
    int cur = 0, n = (int)strlen(charset);
    for (; *s; s++) {
        int want = (int)(strchr(charset, *s) - charset);
        int d = (want - cur + n) % n;
        if (d <= n / 2) taps(BTN_DOWN, d);
        else taps(BTN_UP, n - d);
        tap(BTN_RIGHT);
        cur = want;
    }
}

static void boot(uint64_t seed)
{
    vhal_reset(seed);
    vhal_set_unix(1800000000);
    app_init();
}

/* Runs the IR link between the app and a peer session for `ms`. */
static void ir_exchange(pairing_t *peer, unsigned *seq, int ms)
{
    uint8_t buf[256], f[PAIR_FRAME_MAX];
    for (int t = 0; t < ms; t += 100) {
        app_poll();
        size_t n = vhal_ir_take(buf, sizeof(buf));
        pairing_rx(peer, buf, n);
        n = pairing_tx(peer, f, sizeof(f), (*seq)++);
        if (vhal_ir_on()) app_ir_rx(f, n);
        vhal_advance(100);
    }
}

static void start_peer(pairing_t *peer, uint8_t seed)
{
    uint8_t r[48];
    for (int i = 0; i < 48; i++) r[i] = (uint8_t)(seed * 17 + i * 5);
    pairing_start(peer, r);
}

TEST(test_app_pair_ui)
{
    boot(10);
    pairing_t peer;
    unsigned seq = 0;
    start_peer(&peer, 1);
    taps(BTN_DOWN, 2);
    tap(BTN_CENTER); /* Pair with someone */
    CHECK(on("pair"));
    CHECK(vhal_ir_on());
    ir_exchange(&peer, &seq, 3000);
    CHECK(on("sas"));
    CHECK_EQ_INT(peer.state, PAIR_DONE);
    tap(BTN_CENTER); /* symbols match */
    CHECK(on("text"));
    tap(BTN_LEFT);   /* a pairing must be named: left on an empty name stays */
    CHECK(on("text"));
    type(CS_LABEL, "Ann");
    tap(BTN_CENTER);
    CHECK(on("contact"));
    CHECK(!vhal_ir_on());
    CHECK_EQ_INT(store_count(REC_CONTACT), 1);
    contact_rec_t c;
    CHECK(store_read_contact(0, &c));
    CHECK(strcmp(c.name, "Ann") == 0);
    CHECK(memcmp(c.key, peer.key, 32) == 0);
    CHECK(c.is_low != peer.is_low);

    /* The words move on every 15 s; the screen follows. */
    uint32_t before = vhal_refreshes(0) + vhal_refreshes(1);
    vhal_advance(15000);
    app_poll();
    CHECK(vhal_refreshes(0) + vhal_refreshes(1) > before);

    /* Rename: the old name is offered; delete a letter, add two. */
    tap(BTN_CENTER); /* contact actions */
    CHECK(on("menu"));
    tap(BTN_CENTER); /* Rename */
    CHECK(on("text"));
    tap(BTN_LEFT);   /* "An", wheel now at 'n' */
    tap(BTN_RIGHT);  /* "Ann" */
    tap(BTN_DOWN);
    tap(BTN_RIGHT);  /* "Anno" */
    tap(BTN_CENTER);
    CHECK(on("contact"));
    CHECK(store_read_contact(0, &c) && strcmp(c.name, "Anno") == 0);

    /* Delete, saying no first. */
    tap(BTN_CENTER);
    tap(BTN_DOWN);
    tap(BTN_CENTER);
    CHECK(on("confirm"));
    tap(BTN_LEFT);
    CHECK(on("contact"));
    tap(BTN_CENTER);
    tap(BTN_DOWN);
    tap(BTN_CENTER);
    tap(BTN_CENTER);
    CHECK(on("contacts"));
    CHECK_EQ_INT(store_count(REC_CONTACT), 0);
    tap(BTN_CENTER); /* empty list: centre starts pairing */
    CHECK(on("pair"));
    tap(BTN_LEFT);   /* cancel */
    CHECK(on("menu"));
    CHECK(!vhal_ir_on());
    pairing_wipe(&peer);
}

TEST(test_app_pair_reject_and_fail)
{
    boot(11);
    pairing_t peer;
    unsigned seq = 0;
    start_peer(&peer, 2);
    taps(BTN_DOWN, 2);
    tap(BTN_CENTER);
    ir_exchange(&peer, &seq, 3000);
    CHECK(on("sas"));
    tap(BTN_LEFT); /* the symbols differ */
    CHECK(on("message"));
    tap(BTN_DOWN);
    CHECK(on("menu"));
    CHECK_EQ_INT(store_count(REC_CONTACT), 0);

    /* A reveal that does not match its commitment fails the session;
     * centre retries. */
    tap(BTN_CENTER); /* "Codes" is selected after the message; back out */
    tap(BTN_LEFT);
    taps(BTN_DOWN, 2);
    tap(BTN_CENTER);
    CHECK(on("pair"));
    start_peer(&peer, 3);
    uint8_t buf[256], f[PAIR_FRAME_MAX];
    for (int k = 0; k < 20; k++) {
        app_poll();
        size_t n = vhal_ir_take(buf, sizeof(buf));
        pairing_rx(&peer, buf, n);
        n = pairing_tx(&peer, f, sizeof(f), seq++);
        if (n && f[2] == 2) { /* REVEAL: corrupt the key, fix the CRC */
            f[10] ^= 1;
            uint16_t crc = pairing_crc16(f + 2, 2 + f[3]);
            f[4 + f[3]] = (uint8_t)(crc >> 8);
            f[5 + f[3]] = (uint8_t)crc;
        }
        app_ir_rx(f, n);
        vhal_advance(300);
    }
    app_poll();
    CHECK(on("pair"));
    tap(BTN_CENTER); /* retry */
    CHECK(on("pair"));
    CHECK(vhal_ir_on());
    tap(BTN_LEFT);
    pairing_wipe(&peer);
}

static void enter_pin(const uint8_t *syms, int n)
{
    static const uint8_t B[5] = {0, BTN_UP, BTN_RIGHT, BTN_DOWN, BTN_LEFT};
    for (int i = 0; i < n; i++) tap(B[syms[i]]);
    tap(BTN_CENTER);
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

TEST(test_app_pin_ui)
{
    static const uint8_t PIN[] = {1, 2, 3, 4, 1};
    static const uint8_t OTHER[] = {3, 3, 2, 2}; /* left first would mean "back" */
    boot(12);
    taps(BTN_DOWN, 4);
    tap(BTN_CENTER); /* Settings */
    taps(BTN_DOWN, 2);
    tap(BTN_CENTER); /* Set a PIN */
    CHECK(on("pin"));
    enter_pin(PIN, 3); /* too short */
    CHECK(on("pin"));
    enter_pin(PIN, 5);
    enter_pin(OTHER, 4); /* does not match */
    CHECK(on("pin"));
    enter_pin(PIN, 5);
    enter_pin(PIN, 5);
    CHECK(on("message"));
    CHECK(store_settings()->pin_set);
    tap(BTN_CENTER);
    CHECK(on("menu"));

    /* Asleep, it locks; waking asks for the PIN. */
    CHECK_EQ_INT(sleep_now(), APP_SLEEP);
    CHECK(!store_unlocked());
    vhal_wake();
    app_wake();
    CHECK(on("pin"));
    for (int i = 0; i < 4; i++) {
        enter_pin(OTHER, 4);
        vhal_advance(70000); /* past the back-off */
        app_poll();
    }
    CHECK_EQ_INT(store_pin_fails(), 4);
    enter_pin(OTHER, 4);
    tap(BTN_UP); /* ignored during the back-off */
    CHECK_EQ_INT(store_pin_fails(), 5);
    vhal_advance(70000);
    app_poll();
    enter_pin(PIN, 5);
    CHECK(on("menu"));
    CHECK(store_unlocked());
    CHECK_EQ_INT(store_pin_fails(), 0);

    /* Change it, then remove it. */
    taps(BTN_DOWN, 4);
    tap(BTN_CENTER);
    taps(BTN_DOWN, 2);
    tap(BTN_CENTER); /* Change PIN */
    tap(BTN_LEFT);   /* back out of an empty PIN */
    CHECK(on("menu"));
    taps(BTN_DOWN, 2);
    tap(BTN_CENTER);
    enter_pin(OTHER, 4);
    enter_pin(OTHER, 4);
    tap(BTN_CENTER);
    CHECK(store_unlock(OTHER, 4) == STORE_OK);
    taps(BTN_DOWN, 3);
    tap(BTN_CENTER); /* Remove PIN */
    CHECK(on("confirm"));
    tap(BTN_LEFT);
    CHECK(on("menu"));
    taps(BTN_DOWN, 3);
    tap(BTN_CENTER);
    tap(BTN_CENTER);
    CHECK(!store_settings()->pin_set);

    /* Ten wrong PINs through the UI erase everything. */
    boot(13);
    taps(BTN_DOWN, 4);
    tap(BTN_CENTER);
    taps(BTN_DOWN, 2);
    tap(BTN_CENTER);
    enter_pin(PIN, 5);
    enter_pin(PIN, 5);
    app_init(); /* reboot */
    CHECK(on("pin"));
    for (int i = 0; i < STORE_PIN_TRIES; i++) {
        enter_pin(OTHER, 4);
        vhal_advance(70000);
        app_poll();
    }
    CHECK(on("message"));
    CHECK(!store_settings()->pin_set);
}

TEST(test_app_settings_ui)
{
    boot(14);
    taps(BTN_DOWN, 4);
    tap(BTN_CENTER);
    tap(BTN_CENTER); /* Date & time */
    CHECK(on("time"));
    tap(BTN_UP);     /* year + 1 */
    tap(BTN_RIGHT);
    tap(BTN_DOWN);   /* month - 1 */
    tap(BTN_RIGHT);
    taps(BTN_UP, 40);/* day wraps */
    tap(BTN_RIGHT);
    tap(BTN_DOWN);
    tap(BTN_RIGHT);
    tap(BTN_UP);
    tap(BTN_RIGHT);
    tap(BTN_DOWN);
    tap(BTN_RIGHT);  /* stays on the last field */
    taps(BTN_LEFT, 5);
    tap(BTN_CENTER);
    CHECK(on("message"));
    CHECK(hal_rtc_get() > 1800000000 + 300 * 86400);
    tap(BTN_CENTER);

    taps(BTN_DOWN, 4);
    tap(BTN_CENTER);
    tap(BTN_DOWN);
    tap(BTN_CENTER); /* Time zone */
    CHECK(on("tz"));
    taps(BTN_UP, 6);
    tap(BTN_DOWN);
    tap(BTN_CENTER);
    CHECK_EQ_INT(store_settings()->tz_quarters, 5);
    tap(BTN_DOWN);
    tap(BTN_CENTER);
    tap(BTN_LEFT);   /* leave without saving */
    CHECK_EQ_INT(store_settings()->tz_quarters, 5);
    CHECK(on("menu"));

    taps(BTN_DOWN, 3);
    tap(BTN_CENTER); /* About */
    CHECK(on("about"));
    tap(BTN_DOWN);
    CHECK(on("menu"));
    tap(BTN_LEFT);   /* settings -> home */
    CHECK(on("menu"));
    taps(BTN_UP, 1);
    tap(BTN_CENTER); /* wrap to Settings */
    taps(BTN_UP, 1);
    tap(BTN_CENTER); /* Erase everything */
    CHECK(on("confirm"));
    tap(BTN_LEFT);
    CHECK(on("menu"));
    taps(BTN_UP, 1);
    tap(BTN_CENTER);
    tap(BTN_CENTER);
    CHECK(on("message"));
    CHECK_EQ_INT(store_settings()->tz_quarters, 0);
    tap(BTN_LEFT);
    tap(BTN_LEFT); /* home: left sleeps now */
    CHECK_EQ_INT(app_poll(), APP_SLEEP);
}

static void add_code(const char *label, const char *secret, int opt_steps)
{
    tap(BTN_CENTER); /* from the menu selection "Add a code" */
    type(CS_LABEL, label);
    tap(BTN_CENTER);
    type(CS_BASE32, secret);
    tap(BTN_CENTER);
    CHECK(on("code_opts"));
    for (int i = 0; i < opt_steps; i++) {
        tap(BTN_RIGHT);
        tap(BTN_DOWN);
    }
    if (opt_steps) {
        tap(BTN_UP);   /* the algorithm row, which also steps back */
        tap(BTN_LEFT);
        tap(BTN_RIGHT);
    }
    tap(BTN_CENTER);
    CHECK(on("code"));
}

TEST(test_app_codes_ui)
{
    boot(15);
    tap(BTN_CENTER); /* Codes: empty list */
    CHECK(on("codes"));
    tap(BTN_CENTER); /* centre on an empty list adds one */
    CHECK(on("text"));
    tap(BTN_LEFT);   /* cancel */
    CHECK(on("menu"));

    taps(BTN_DOWN, 3);
    tap(BTN_CENTER); /* Add a code */
    type(CS_LABEL, "X");
    tap(BTN_CENTER);
    type(CS_BASE32, "ABC"); /* too short */
    tap(BTN_CENTER);
    CHECK(on("message"));
    tap(BTN_CENTER);
    CHECK(on("text"));
    taps(BTN_LEFT, 4); /* delete the three letters, then back to the label */
    CHECK(on("text"));
    tap(BTN_LEFT);
    tap(BTN_LEFT);     /* empty label: back home */
    CHECK(on("menu"));

    taps(BTN_DOWN, 3);
    add_code("Bank", "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ", 3);
    totp_rec_t r;
    CHECK(store_read_totp(0, &r));
    CHECK_EQ_INT(r.digits, 8);
    CHECK_EQ_INT(r.period, 60);
    CHECK_EQ_INT(r.alg, 1);
    tap(BTN_LEFT); /* to the list */
    tap(BTN_LEFT); /* home */
    taps(BTN_DOWN, 3);
    add_code("Mail", "MFRGGZDFMZTWQ2LK", 0);
    CHECK_EQ_INT(store_count(REC_TOTP), 2);

    /* Step between codes on the code screen and in the list. */
    tap(BTN_DOWN);
    CHECK(on("code"));
    tap(BTN_UP);
    tap(BTN_LEFT);
    CHECK(on("codes"));
    tap(BTN_DOWN);
    tap(BTN_UP);
    tap(BTN_CENTER);
    CHECK(on("code"));

    /* Rename and delete from the code menu. */
    tap(BTN_CENTER);
    tap(BTN_LEFT); /* menu: back to the code */
    CHECK(on("code"));
    tap(BTN_CENTER);
    tap(BTN_CENTER); /* Rename */
    tap(BTN_RIGHT);
    tap(BTN_CENTER);
    tap(BTN_CENTER);
    tap(BTN_DOWN);
    tap(BTN_CENTER); /* Delete */
    tap(BTN_CENTER);
    CHECK(on("codes"));
    CHECK_EQ_INT(store_count(REC_TOTP), 1);

    /* A full store says so. */
    totp_rec_t t = r;
    while (store_write_totp(-1, &t) >= 0) {
    }
    tap(BTN_LEFT);
    taps(BTN_DOWN, 3);
    tap(BTN_CENTER);
    type(CS_LABEL, "Z");
    tap(BTN_CENTER);
    type(CS_BASE32, "MFRGGZDFMZTWQ2LK");
    tap(BTN_CENTER);
    tap(BTN_CENTER);
    CHECK(on("message"));
}

TEST(test_app_first_boot)
{
    vhal_reset(16);
    vhal_set_unix(0); /* the RTC has never been set */
    app_init();
    CHECK(on("time"));
    tap(BTN_LEFT); /* cannot leave before the time is set */
    CHECK(on("time"));
    tap(BTN_CENTER);
    CHECK(on("message"));
    CHECK(hal_rtc_get() >= 1767225600);
    vhal_set_battery_mv(2500); /* low battery is drawn */
    tap(BTN_CENTER);
    CHECK(on("menu"));
}
