/* Words, pairing, store and whole-app tests on the virtual board.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include "../core/app.h"
#include "../core/hal.h"
#include "../core/pairing.h"
#include "../core/store.h"
#include "../core/totp.h"
#include "../core/words.h"
#include "../vhal/vhal.h"
#include "test.h"

TEST(test_words)
{
    uint8_t key[32];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;
    for (uint64_t w = 0; w < 2000; w++) {
        uint16_t g1, r1, g2, r2;
        words_pair(key, true, w, &g1, &r1);
        words_pair(key, false, w, &g2, &r2);
        CHECK(g1 == r2 && r1 == g2); /* my Give is your Receive */
        CHECK(g1 != r1);
        CHECK(g1 < 2048 && r1 < 2048);
    }
    CHECK(strcmp(words_word(0), "abandon") == 0);
    CHECK(strcmp(words_word(2047), "zoo") == 0);
    CHECK(strcmp(words_word(1000), "laptop") == 0);
}

static void rnd(uint8_t *r, uint8_t seed)
{
    for (int i = 0; i < 48; i++) r[i] = (uint8_t)(seed * 31 + i * 7);
}

/* Shuttles frames between two sessions; with drop_one_in > 0 each frame is
 * lost with probability 1/drop_one_in (fixed-seed LCG, so reproducible). */
static uint32_t lcg = 12345;
static int lost(int drop_one_in)
{
    lcg = lcg * 1103515245u + 12345u;
    return drop_one_in && (lcg >> 16) % (uint32_t)drop_one_in == 0;
}

static void shuttle(pairing_t *a, pairing_t *b, int rounds, int drop_one_in)
{
    uint8_t f[PAIR_FRAME_MAX];
    for (unsigned seq = 0; seq < (unsigned)rounds; seq++) {
        size_t n = pairing_tx(a, f, sizeof(f), seq);
        if (n && !lost(drop_one_in)) {
            pairing_rx(b, f, n);
            pairing_rx(a, f, n); /* hears its own echo */
        }
        n = pairing_tx(b, f, sizeof(f), seq + 1);
        if (n && !lost(drop_one_in)) {
            pairing_rx(a, f, n);
            pairing_rx(b, f, n);
        }
    }
}

TEST(test_pairing_clean)
{
    pairing_t a, b;
    uint8_t r[48];
    rnd(r, 1);
    pairing_start(&a, r);
    rnd(r, 2);
    pairing_start(&b, r);
    shuttle(&a, &b, 6, 0);
    CHECK_EQ_INT(a.state, PAIR_DONE);
    CHECK_EQ_INT(b.state, PAIR_DONE);
    CHECK(memcmp(a.key, b.key, 32) == 0);
    CHECK(memcmp(a.sas, b.sas, PAIR_SAS_LEN) == 0);
    CHECK(a.is_low != b.is_low);
    uint16_t ga, ra, gb, rb;
    words_pair(a.key, a.is_low, 123456, &ga, &ra);
    words_pair(b.key, b.is_low, 123456, &gb, &rb);
    CHECK(ga == rb && ra == gb);
}

TEST(test_pairing_lossy)
{
    for (int drop = 2; drop <= 5; drop++) {
        pairing_t a, b;
        uint8_t r[48];
        rnd(r, (uint8_t)(10 + drop));
        pairing_start(&a, r);
        rnd(r, (uint8_t)(20 + drop));
        pairing_start(&b, r);
        shuttle(&a, &b, 40, drop);
        CHECK_EQ_INT(a.state, PAIR_DONE);
        CHECK_EQ_INT(b.state, PAIR_DONE);
        CHECK(memcmp(a.key, b.key, 32) == 0);
    }
    /* Line noise between frames is skipped. */
    pairing_t a, b;
    uint8_t r[48], f[PAIR_FRAME_MAX], noise[] = {0x00, 'I', 0x13, 'I', '2', 9, 200};
    rnd(r, 3);
    pairing_start(&a, r);
    rnd(r, 4);
    pairing_start(&b, r);
    size_t n = pairing_tx(&a, f, sizeof(f), 0);
    pairing_rx(&b, noise, sizeof(noise));
    pairing_rx(&b, f, n);
    CHECK_EQ_INT(b.state, PAIR_REVEALING);
}

TEST(test_pairing_tamper)
{
    pairing_t a, b;
    uint8_t r[48], f[PAIR_FRAME_MAX];
    rnd(r, 5);
    pairing_start(&a, r);
    rnd(r, 6);
    pairing_start(&b, r);
    size_t n = pairing_tx(&a, f, sizeof(f), 0);
    pairing_rx(&b, f, n); /* b has a's commit */
    n = pairing_tx(&b, f, sizeof(f), 1);
    pairing_rx(&a, f, n); /* a has b's commit (seq 1 = COMMIT) */
    /* A REVEAL whose key does not match the commitment fails the session. */
    n = pairing_tx(&a, f, sizeof(f), 0);
    f[4 + 5] ^= 1; /* flip a bit of the public key */
    uint16_t crc = pairing_crc16(f + 2, 2 + f[3]);
    f[4 + f[3]] = (uint8_t)(crc >> 8);
    f[4 + f[3] + 1] = (uint8_t)crc;
    pairing_rx(&b, f, n);
    CHECK_EQ_INT(b.state, PAIR_FAILED);
    /* A frame with a bad CRC is ignored. */
    pairing_t c;
    rnd(r, 7);
    pairing_start(&c, r);
    n = pairing_tx(&a, f, sizeof(f), 1);
    f[n - 1] ^= 0xff;
    pairing_rx(&c, f, n);
    CHECK_EQ_INT(c.state, PAIR_SEARCHING);
}

TEST(test_store_roundtrip)
{
    vhal_reset(1);
    CHECK_EQ_INT(store_mount(), STORE_OK);
    totp_rec_t t = {0};
    strcpy(t.label, "GitHub");
    t.alg = HASH_SHA1;
    t.digits = 6;
    t.period = 30;
    t.secret_len = 20;
    memcpy(t.secret, "12345678901234567890", 20);
    int s1 = store_write_totp(-1, &t);
    strcpy(t.label, "Email");
    int s2 = store_write_totp(-1, &t);
    CHECK_EQ_INT(s1, 0);
    CHECK_EQ_INT(s2, 1);
    CHECK_EQ_INT(store_count(REC_TOTP), 2);
    /* Labels and secrets are not in flash in clear text. */
    uint8_t *fl = vhal_flash();
    int found = 0;
    for (size_t i = 0; i + 6 < VHAL_FLASH_PAGES * HAL_FLASH_PAGE; i++)
        if (memcmp(fl + i, "GitHub", 6) == 0 || memcmp(fl + i, "1234567890", 10) == 0) found = 1;
    CHECK(!found);
    /* Remount (reboot) and read back. */
    CHECK_EQ_INT(store_mount(), STORE_OK);
    totp_rec_t back;
    CHECK(store_read_totp(1, &back));
    CHECK(strcmp(back.label, "Email") == 0);
    CHECK(store_delete(0));
    CHECK_EQ_INT(store_nth(REC_TOTP, 0), 1);
    /* Fill the store. */
    int n = 0;
    while (store_write_totp(-1, &t) >= 0) n++;
    CHECK_EQ_INT(n, STORE_MAX_SLOTS - 1);
    CHECK_EQ_INT(store_count(REC_EMPTY), 0);
    /* A slot copied to another index fails authentication. */
    memcpy(fl + STORE_FIRST_SLOT_PAGE * HAL_FLASH_PAGE + 2 * STORE_SLOT_SIZE + 4,
           fl + STORE_FIRST_SLOT_PAGE * HAL_FLASH_PAGE + 3 * STORE_SLOT_SIZE + 4,
           STORE_SLOT_SIZE - 4);
    CHECK(!store_read_totp(2, &back));
}

TEST(test_store_pin)
{
    vhal_reset(2);
    store_mount();
    totp_rec_t t = {0};
    strcpy(t.label, "Bank");
    t.secret_len = 10;
    store_write_totp(-1, &t);
    const uint8_t pin[] = {1, 2, 3, 4, 1, 2};
    const uint8_t bad[] = {4, 3, 2, 1};
    CHECK_EQ_INT(store_set_pin(pin, sizeof(pin)), STORE_OK);
    CHECK_EQ_INT(store_mount(), STORE_LOCKED);
    totp_rec_t back;
    CHECK(!store_read_totp(0, &back));
    CHECK_EQ_INT(store_unlock(bad, sizeof(bad)), STORE_BAD_PIN);
    CHECK_EQ_INT(store_pin_fails(), 1);
    /* The failure count survives a reboot. */
    CHECK_EQ_INT(store_mount(), STORE_LOCKED);
    CHECK_EQ_INT(store_pin_fails(), 1);
    CHECK_EQ_INT(store_unlock(pin, sizeof(pin)), STORE_OK);
    CHECK_EQ_INT(store_pin_fails(), 0);
    CHECK(store_read_totp(0, &back) && strcmp(back.label, "Bank") == 0);
    /* Ten wrong PINs erase everything. */
    store_lock();
    store_status_t st = STORE_OK;
    for (int i = 0; i < STORE_PIN_TRIES; i++) st = store_unlock(bad, sizeof(bad));
    CHECK_EQ_INT(st, STORE_WIPED);
    CHECK_EQ_INT(store_count(REC_TOTP), 0);
    CHECK(!store_settings()->pin_set);
    /* Removing a PIN. */
    store_set_pin(pin, sizeof(pin));
    store_mount();
    store_unlock(pin, sizeof(pin));
    CHECK_EQ_INT(store_set_pin(NULL, 0), STORE_OK);
    CHECK_EQ_INT(store_mount(), STORE_OK);
}

TEST(test_store_powercut)
{
    /* Cut the power after every possible flash operation of a write, then
     * reboot: the slot's page is always either fully old or fully new. */
    for (int cut = 0; cut < 8; cut++) {
        vhal_reset(3);
        store_mount();
        totp_rec_t t = {0};
        strcpy(t.label, "Old");
        t.secret_len = 10;
        store_write_totp(-1, &t);
        strcpy(t.label, "Neighbour");
        store_write_totp(-1, &t);
        strcpy(t.label, "New");
        vhal_fail_after(cut);
        store_write_totp(0, &t);
        vhal_fail_after(-1);
        store_mount();
        totp_rec_t a, b;
        CHECK(store_read_totp(0, &a));
        CHECK(strcmp(a.label, "Old") == 0 || strcmp(a.label, "New") == 0);
        CHECK(store_read_totp(1, &b) && strcmp(b.label, "Neighbour") == 0);
    }
}

/* ---- whole app ---- */

static void press(uint8_t b)
{
    vhal_set_buttons(b);
    app_poll();
    vhal_advance(30);
    vhal_set_buttons(0);
    app_poll();
    vhal_advance(30);
}

static void type_text(const char *charset, const char *s)
{
    /* The wheel starts at the character last deleted or at index 0; we
     * always start from 0 here because text begins empty. */
    int cur = 0;
    int n = (int)strlen(charset);
    for (; *s; s++) {
        int want = (int)(strchr(charset, *s) - charset);
        int d = (want - cur + n) % n;
        for (int i = 0; i < d; i++) press(BTN_DOWN);
        press(BTN_RIGHT);
        cur = want;
    }
}

TEST(test_app_add_code)
{
    vhal_reset(4);
    vhal_set_unix(1800000000);
    app_init();
    CHECK(strcmp(app_screen_name(), "menu") == 0);
    press(BTN_DOWN);
    press(BTN_DOWN);
    press(BTN_DOWN); /* "Add a code" */
    press(BTN_CENTER);
    CHECK(strcmp(app_screen_name(), "text") == 0);
    type_text("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_.@+&'#", "Git");
    press(BTN_CENTER);
    type_text("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ");
    press(BTN_CENTER);
    CHECK(strcmp(app_screen_name(), "code_opts") == 0);
    press(BTN_CENTER);
    CHECK(strcmp(app_screen_name(), "code") == 0);
    CHECK_EQ_INT(store_count(REC_TOTP), 1);
    totp_rec_t r;
    CHECK(store_read_totp(0, &r));
    CHECK(strcmp(r.label, "Git") == 0);
    CHECK_EQ_INT(r.secret_len, 20);
    CHECK_EQ_INT(totp(HASH_SHA1, r.secret, r.secret_len, 1800000000, 30, 6),
                 totp(HASH_SHA1, (const uint8_t *)"12345678901234567890", 20, 1800000000, 30, 6));
    CHECK(vhal_refreshes(0) + vhal_refreshes(1) > 0);
}

TEST(test_app_sleep_wake)
{
    vhal_reset(5);
    vhal_set_unix(1800000000);
    app_init();
    uint32_t r = 0;
    for (int i = 0; i < 700 && r != APP_SLEEP; i++) {
        r = app_poll();
        vhal_advance(100);
    }
    CHECK_EQ_INT(r, APP_SLEEP);
    vhal_set_buttons(BTN_CENTER);
    app_wake();
    vhal_set_buttons(0);
    CHECK(app_poll() != APP_SLEEP);
    CHECK(strcmp(app_screen_name(), "menu") == 0);
}
