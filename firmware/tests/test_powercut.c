/* Power-cut tests for every store write path.
 *
 * For each scenario the store is brought to an "old" state, then the write
 * path under test runs with the power cut after flash operation 0, 1, 2, ...
 * up to the last one, and for each cut point in three ways: the operation
 * never starts, it does half its work, or it leaves noise behind (an
 * interrupted erase or program leaves bits undefined on real flash). After
 * each cut the device "reboots" (store_mount) and the test checks that what
 * a person can observe is exactly the old state or exactly the new state,
 * and that the store still works.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include "../core/hal.h"
#include "../core/hash.h"
#include "../core/store.h"
#include "../vhal/vhal.h"
#include "test.h"

static const uint8_t PIN_A[] = {1, 2, 3, 4, 1, 2};
static const uint8_t PIN_B[] = {4, 4, 3, 3};
static const uint8_t PIN_BAD[] = {2, 2, 2, 2};
static const uint8_t FAMILY[] = {1, 1, 2, 2, 3, 3};
static const uint8_t FAMILY_BAD[] = {4, 4, 4, 4, 4, 4};
#define FAMILY_T 1900000000      /* RTC time of the family PIN tries */
#define FAMILY_CHECK_T 2100000000 /* long after any wait */

/* Everything observable after a reboot. */
typedef struct {
    int pin;           /* 0 no PIN, 1 PIN_A, 2 PIN_B, -1 locked and neither works */
    uint8_t fails;     /* wrong-PIN count before we unlock */
    int corrupt;       /* a slot that is marked used but does not decrypt */
    uint8_t digest[32];/* settings + every slot's type and plaintext */
    uint8_t ffails;    /* wrong family PINs (simple mode) */
    int family;        /* 0 simple mode off, 1 on and FAMILY leaves it */
} view_t;

static view_t view(void)
{
    view_t w;
    memset(&w, 0, sizeof(w));
    store_status_t st = store_mount();
    w.fails = store_pin_fails();
    if (st == STORE_LOCKED) {
        w.pin = -1;
        if (store_unlock(PIN_A, sizeof(PIN_A)) == STORE_OK) w.pin = 1;
        else if (store_unlock(PIN_B, sizeof(PIN_B)) == STORE_OK) w.pin = 2;
    }
    hash_ctx_t h;
    hash_init(&h, HASH_SHA256);
    const settings_t *s = store_settings();
    hash_update(&h, &s->tz_quarters, 1);
    hash_update(&h, &s->sleep_s, 1);
    hash_update(&h, &s->pin_set, 1);
    hash_update(&h, &s->simple, 1);
    for (uint16_t i = 0; i < STORE_MAX_SLOTS; i++) {
        uint8_t t = (uint8_t)store_slot_type(i);
        hash_update(&h, &t, 1);
        if (t == REC_TOTP) {
            totp_rec_t r;
            if (store_read_totp(i, &r)) hash_update(&h, &r, sizeof(r));
            else w.corrupt = 1;
        } else if (t == REC_CONTACT) {
            contact_rec_t r;
            if (store_read_contact(i, &r)) hash_update(&h, &r, sizeof(r));
            else w.corrupt = 1;
        }
    }
    hash_final(&h, w.digest);
    w.ffails = store_family_fails();
    if (store_settings()->simple && store_unlocked()) {
        /* The flag and its verifier change together: the PIN that was set
         * must open it (a verifier that does not open would turn it off). */
        bool ok = store_check_family_pin(FAMILY, sizeof(FAMILY), FAMILY_CHECK_T) == STORE_OK;
        if (ok && store_settings()->simple) w.family = 1;
        else w.corrupt = 1;
    }
    return w;
}

/* Same settings, records and PINs (the failure counts are compared apart). */
static bool same(const view_t *a, const view_t *b)
{
    return a->pin == b->pin && a->family == b->family && !a->corrupt && !b->corrupt &&
           memcmp(a->digest, b->digest, sizeof(a->digest)) == 0;
}

/* After any reboot the store must still accept, return and survive a write. */
static void check_usable(const char *name, int cut)
{
    if (!store_unlocked()) {
        if (store_unlock(PIN_A, sizeof(PIN_A)) != STORE_OK)
            store_unlock(PIN_B, sizeof(PIN_B));
    }
    totp_rec_t t = {0}, back;
    strcpy(t.label, "Probe");
    t.alg = HASH_SHA1;
    t.digits = 6;
    t.period = 30;
    t.secret_len = 10;
    int slot = store_write_totp(-1, &t);
    bool ok = slot >= 0 && store_read_totp((uint16_t)slot, &back) && strcmp(back.label, "Probe") == 0;
    if (ok) {
        store_mount();
        if (!store_unlocked()) {
            if (store_unlock(PIN_A, sizeof(PIN_A)) != STORE_OK)
                store_unlock(PIN_B, sizeof(PIN_B));
        }
        ok = store_read_totp((uint16_t)slot, &back) && strcmp(back.label, "Probe") == 0;
    }
    if (!ok) fprintf(stderr, "  %s: store unusable after a cut at op %d\n", name, cut);
    CHECK(ok);
}

static totp_rec_t totp_named(const char *label)
{
    totp_rec_t t = {0};
    strcpy(t.label, label);
    t.alg = HASH_SHA1;
    t.digits = 6;
    t.period = 30;
    t.secret_len = 20;
    memcpy(t.secret, "12345678901234567890", 20);
    return t;
}

/* ---- scenarios: setup builds the old state, op is the write path ---- */

static void setup_records(void)
{
    totp_rec_t t = totp_named("Old");
    store_write_totp(-1, &t);
    t = totp_named("Neighbour");
    store_write_totp(-1, &t);
    contact_rec_t c = {0};
    strcpy(c.name, "Alice");
    for (int i = 0; i < 32; i++) c.key[i] = (uint8_t)(i * 7);
    store_write_contact(-1, &c);
    /* a record on the second slot page too */
    t = totp_named("Far");
    store_write_totp(STORE_SLOTS_PER_PAGE + 3, &t);
    settings_t s = *store_settings();
    s.tz_quarters = 4;
    store_save_settings(&s);
}

static void setup_pin_a(void)
{
    setup_records();
    store_set_pin(PIN_A, sizeof(PIN_A));
}

/* PIN_A set, the attempt log one entry short of full, 3 failures pending. */
static void setup_log_full(void)
{
    setup_pin_a();
    for (int used = 0; used < (int)(HAL_FLASH_PAGE / 8) - 1 - 3; used += 2) {
        store_lock();
        store_unlock(PIN_A, sizeof(PIN_A)); /* TRY + OK */
    }
    store_lock();
    while (store_pin_fails() < 3) store_unlock(PIN_BAD, sizeof(PIN_BAD));
}

/* PIN_A set and nine wrong PINs already counted. */
static void setup_nine_fails(void)
{
    setup_pin_a();
    store_lock();
    for (int i = 0; i < STORE_PIN_TRIES - 1; i++) store_unlock(PIN_BAD, sizeof(PIN_BAD));
}

static void setup_simple(void)
{
    setup_records();
    store_set_simple(FAMILY, sizeof(FAMILY));
}

/* Simple mode on, two wrong family PINs counted. */
static void setup_simple_fails(void)
{
    setup_simple();
    store_check_family_pin(FAMILY_BAD, sizeof(FAMILY_BAD), FAMILY_T);
    store_check_family_pin(FAMILY_BAD, sizeof(FAMILY_BAD), FAMILY_T);
}

static int log_used(void)
{
    const uint8_t *p = vhal_flash() + 3 * HAL_FLASH_PAGE;
    int n = 0;
    while (n < (int)(HAL_FLASH_PAGE / 8)) {
        uint64_t v;
        memcpy(&v, p + n * 8, 8);
        if (v == UINT64_MAX) break;
        n++;
    }
    return n;
}

/* Simple mode on with PIN_A, three family failures and the attempt log
 * filled to `used` entries. */
static void setup_simple_log(int used)
{
    setup_pin_a();
    store_set_simple(FAMILY, sizeof(FAMILY));
    for (int i = 0; i < 3; i++)
        store_check_family_pin(FAMILY_BAD, sizeof(FAMILY_BAD), FAMILY_T);
    if ((used - log_used()) % 2) {
        store_lock();
        store_unlock(PIN_BAD, sizeof(PIN_BAD));
        store_unlock(PIN_A, sizeof(PIN_A)); /* leaves the last OK last */
    }
    while (log_used() < used) {
        store_lock();
        store_unlock(PIN_A, sizeof(PIN_A));
    }
}

/* The scenario's restore adds a TRY and an OK (unlocking with PIN_A); the
 * family PIN try that follows finds the log full and compacts it. */
static void setup_simple_log_family(void) { setup_simple_log(HAL_FLASH_PAGE / 8 - 3); }
/* Locked: the next device PIN try compacts. */
static void setup_simple_log_device(void) { setup_simple_log(HAL_FLASH_PAGE / 8 - 1); }

static void op_simple_on(void) { store_set_simple(FAMILY, sizeof(FAMILY)); }
static void op_simple_off(void) { store_set_simple(NULL, 0); }

static void op_wrong_family(void)
{
    store_check_family_pin(FAMILY_BAD, sizeof(FAMILY_BAD), FAMILY_T + 1000);
}

static void op_right_family(void)
{
    store_check_family_pin(FAMILY, sizeof(FAMILY), FAMILY_T + 1000);
}

static void op_family_clock_back(void)
{
    store_check_family_pin(FAMILY_BAD, sizeof(FAMILY_BAD), FAMILY_T + 1000);
    store_family_wait_s(FAMILY_T - 86400); /* a battery change reset the RTC */
}

static void op_add_totp(void)
{
    totp_rec_t t = totp_named("New");
    store_write_totp(-1, &t);
}

static void op_add_contact(void)
{
    contact_rec_t c = {0};
    strcpy(c.name, "Bob");
    for (int i = 0; i < 32; i++) c.key[i] = (uint8_t)(255 - i);
    store_write_contact(-1, &c);
}

static void op_rename(void)
{
    totp_rec_t t = totp_named("Renamed");
    store_write_totp(0, &t);
}

static void op_delete(void) { store_delete(1); }
static void op_delete_far(void) { store_delete(STORE_SLOTS_PER_PAGE + 3); }

static void op_settings(void)
{
    settings_t s = *store_settings();
    s.tz_quarters = -20;
    s.sleep_s = 60;
    store_save_settings(&s);
}

static void op_set_pin(void) { store_set_pin(PIN_A, sizeof(PIN_A)); }
static void op_change_pin(void) { store_set_pin(PIN_B, sizeof(PIN_B)); }
static void op_remove_pin(void) { store_set_pin(NULL, 0); }
static void op_wipe(void) { store_wipe(); }

static void op_wrong_pin(void)
{
    store_lock();
    store_unlock(PIN_BAD, sizeof(PIN_BAD));
}

static void op_right_pin(void)
{
    store_lock();
    store_unlock(PIN_A, sizeof(PIN_A));
}

typedef struct {
    const char *name;
    void (*setup)(void);
    void (*op)(void);
    /* The attempt log counts a PIN attempt before checking it, so for an
     * unlock "the attempt happened but its result was not recorded" is a
     * legitimate third state: failures = old + 1. */
    bool unlock;
    /* The same for a family PIN check: the try is logged before checking. */
    bool family;
} scenario_t;

static const scenario_t SCENARIOS[] = {
    {"add code", setup_records, op_add_totp, false, false},
    {"add contact", setup_records, op_add_contact, false, false},
    {"rename (overwrite)", setup_records, op_rename, false, false},
    {"delete", setup_records, op_delete, false, false},
    {"delete, 2nd page", setup_records, op_delete_far, false, false},
    {"settings", setup_records, op_settings, false, false},
    {"set PIN", setup_records, op_set_pin, false, false},
    {"change PIN", setup_pin_a, op_change_pin, false, false},
    {"remove PIN", setup_pin_a, op_remove_pin, false, false},
    {"wipe", setup_pin_a, op_wipe, false, false},
    {"add code with PIN", setup_pin_a, op_add_totp, false, false},
    {"wrong PIN", setup_pin_a, op_wrong_pin, true, false},
    {"right PIN", setup_pin_a, op_right_pin, true, false},
    {"wrong PIN, log compaction", setup_log_full, op_wrong_pin, true, false},
    {"right PIN, log compaction", setup_log_full, op_right_pin, true, false},
    {"10th wrong PIN wipes", setup_nine_fails, op_wrong_pin, true, false},
    {"simple mode on", setup_records, op_simple_on, false, false},
    {"simple mode on, with PIN", setup_pin_a, op_simple_on, false, false},
    {"simple mode off", setup_simple, op_simple_off, false, false},
    {"add contact in simple mode", setup_simple, op_add_contact, false, false},
    {"set PIN in simple mode", setup_simple, op_set_pin, false, false},
    {"wipe in simple mode", setup_simple, op_wipe, false, false},
    {"wrong family PIN", setup_simple, op_wrong_family, false, true},
    {"wrong family PIN, 3rd (wait)", setup_simple_fails, op_wrong_family, false, true},
    {"right family PIN", setup_simple_fails, op_right_family, false, true},
    {"family PIN, clock back", setup_simple_fails, op_family_clock_back, false, true},
    {"family PIN, log compaction", setup_simple_log_family, op_wrong_family, false, true},
    {"PIN with family fails, compaction", setup_simple_log_device, op_wrong_pin, true, false},
};

static uint8_t snapshot[VHAL_FLASH_PAGES * HAL_FLASH_PAGE];

/* Restores the old state and brings it up as the device would be just
 * before the operation: mounted, and unlocked if the scenario needs it. */
static void restore(const scenario_t *sc)
{
    memcpy(vhal_flash(), snapshot, sizeof(snapshot));
    store_mount();
    if (!sc->unlock && !store_unlocked()) store_unlock(PIN_A, sizeof(PIN_A));
}

static void run_scenario(const scenario_t *sc, int *cuts)
{
    vhal_reset(42);
    store_mount();
    sc->setup();
    store_mount();
    memcpy(snapshot, vhal_flash(), sizeof(snapshot));

    /* the old and new states, and how many flash operations the path takes */
    restore(sc);
    view_t before = view();
    restore(sc);
    uint32_t ops0 = vhal_flash_ops();
    sc->op();
    int nops = (int)(vhal_flash_ops() - ops0);
    view_t after = view();
    CHECK(!before.corrupt && !after.corrupt);
    CHECK(nops > 0);
    if (getenv("PC_DEBUG")) fprintf(stderr, "%s: %d ops, fails %u -> %u, family fails %u -> %u, pin %d -> %d\n", sc->name, nops, before.fails, after.fails, before.ffails, after.ffails, before.pin, after.pin);

    for (int cut = 0; cut < nops; cut++) {
        for (int mode = VHAL_CUT_DROP; mode <= VHAL_CUT_GARBAGE; mode++) {
            restore(sc);
            vhal_fail_mode((vhal_cut_t)mode);
            vhal_fail_after(cut);
            sc->op();
            CHECK(vhal_fail_tripped());
            vhal_fail_after(-1);
            view_t got = view();
            /* exactly the old state or exactly the new one, failure count
             * included; for an unlock also "old state, attempt counted" */
            bool ok = (same(&got, &before) && got.fails == before.fails &&
                       got.ffails == before.ffails) ||
                      (same(&got, &after) && got.fails == after.fails && got.ffails == after.ffails) ||
                      (sc->unlock && same(&got, &before) && got.fails == before.fails + 1 &&
                       got.ffails == before.ffails) ||
                      (sc->family && same(&got, &before) && got.fails == before.fails &&
                       got.ffails == before.ffails + 1);
            if (!ok)
                fprintf(stderr, "  %s: cut at op %d/%d (mode %d): pin %d fails %u ffails %u "
                        "family %d corrupt %d\n", sc->name, cut, nops, mode, got.pin, got.fails,
                        got.ffails, got.family, got.corrupt);
            CHECK(ok);
            check_usable(sc->name, cut);
            (*cuts)++;
        }
    }
}

TEST(test_store_powercut_all)
{
    int cuts = 0;
    for (size_t i = 0; i < sizeof(SCENARIOS) / sizeof(SCENARIOS[0]); i++)
        run_scenario(&SCENARIOS[i], &cuts);
    vhal_fail_mode(VHAL_CUT_DROP);
    CHECK(cuts > 300);
}

/* Regression: compacting the PIN attempt log used to erase the page and
 * then rewrite the pending failures, so a power cut between the two reset
 * the wrong-PIN count to zero. */
TEST(test_store_pinlog_compaction)
{
    vhal_reset(43);
    store_mount();
    setup_log_full();
    CHECK_EQ_INT(store_pin_fails(), 3);
    uint32_t ops0 = vhal_flash_ops();
    vhal_fail_after(1); /* let the first operation through, cut the second */
    store_unlock(PIN_BAD, sizeof(PIN_BAD));
    vhal_fail_after(-1);
    CHECK(vhal_flash_ops() > ops0);
    store_mount();
    CHECK(store_pin_fails() >= 3);
}

/* Regression: ten recorded failures must erase the store even if the power
 * was cut after the tenth attempt was logged but before the wipe ran. */
TEST(test_store_tenth_fail_cut)
{
    vhal_reset(44);
    store_mount();
    setup_nine_fails();
    CHECK_EQ_INT(store_pin_fails(), STORE_PIN_TRIES - 1);
    vhal_fail_after(1); /* the TRY entry lands, the wipe does not */
    store_unlock(PIN_BAD, sizeof(PIN_BAD));
    vhal_fail_after(-1);
    CHECK_EQ_INT(store_mount(), STORE_OK);
    CHECK_EQ_INT(store_count(REC_TOTP), 0);
    CHECK(!store_settings()->pin_set);
}
