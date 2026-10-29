/* libFuzzer harness: mounting and using a corrupted flash image.
 *
 * Byte 0 picks the starting image: a healthy store with records (with or
 * without a PIN, optionally with an interrupted journal), or blank flash.
 * The rest is a list of patches (3-byte offset, 1 byte XOR-ed in; or with
 * the top offset bit set, a run of bytes copied in), so the fuzzer can both
 * flip bits anywhere and write whole forged headers. Then the store is
 * mounted, unlocked, every slot listed and read, a record written, read
 * back, deleted, the PIN changed and the store remounted. Any memory error,
 * undefined behaviour, write to unerased flash (vhal traps) or broken
 * invariant is a crash.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../core/hal.h"
#include "../core/store.h"
#include "../vhal/vhal.h"

#define ASSERT(c) do { if (!(c)) abort(); } while (0)
#define FLASH_SIZE (VHAL_FLASH_PAGES * HAL_FLASH_PAGE)

static const uint8_t PIN[] = {1, 2, 3, 4};
static uint8_t images[4][FLASH_SIZE];
static int ready;

static void add_records(void)
{
    totp_rec_t t;
    memset(&t, 0, sizeof(t));
    strcpy(t.label, "Code");
    t.alg = 1;
    t.digits = 6;
    t.period = 30;
    t.secret_len = 20;
    store_write_totp(-1, &t);
    store_write_totp(20, &t);
    contact_rec_t c;
    memset(&c, 0, sizeof(c));
    strcpy(c.name, "Friend");
    store_write_contact(-1, &c);
}

static void build_images(void)
{
    /* 0: records, no PIN */
    vhal_reset(1);
    store_mount();
    add_records();
    memcpy(images[0], vhal_flash(), FLASH_SIZE);
    /* 1: records, PIN set, two failures logged */
    store_set_pin(PIN, sizeof(PIN));
    store_mount();
    static const uint8_t BAD[] = {4, 4, 4, 4};
    store_unlock(BAD, sizeof(BAD));
    store_unlock(BAD, sizeof(BAD));
    memcpy(images[1], vhal_flash(), FLASH_SIZE);
    /* 2: records, a page rewrite interrupted after its journal was written */
    vhal_reset(2);
    store_mount();
    add_records();
    vhal_fail_after(4);
    store_delete(0);
    vhal_fail_after(-1);
    memcpy(images[2], vhal_flash(), FLASH_SIZE);
    /* 3: blank */
    memset(images[3], 0xff, FLASH_SIZE);
    ready = 1;
}

static void unlock_if_needed(void)
{
    if (store_unlocked()) return;
    store_unlock(PIN, sizeof(PIN));
}

static void exercise(void)
{
    int total = store_count(REC_EMPTY) + store_count(REC_TOTP) + store_count(REC_CONTACT);
    ASSERT(total == (int)STORE_MAX_SLOTS);
    for (uint16_t i = 0; i < STORE_MAX_SLOTS; i++) {
        rec_type_t t = store_slot_type(i);
        ASSERT(t == REC_EMPTY || t == REC_TOTP || t == REC_CONTACT);
        if (t != REC_EMPTY) ASSERT(store_nth(t, store_rank(i)) == i);
        totp_rec_t r;
        contact_rec_t c;
        if (store_read_totp(i, &r)) {
            ASSERT(t == REC_TOTP);
            ASSERT(memchr(r.label, '\0', sizeof(r.label)) != NULL);
            ASSERT(r.secret_len <= STORE_SECRET_MAX);
        }
        if (store_read_contact(i, &c)) {
            ASSERT(t == REC_CONTACT);
            ASSERT(memchr(c.name, '\0', sizeof(c.name)) != NULL);
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!ready) build_images();
    if (size < 1) return 0;
    vhal_reset(3);
    memcpy(vhal_flash(), images[data[0] & 3], FLASH_SIZE);
    uint8_t *fl = vhal_flash();
    for (size_t i = 1; i + 4 <= size;) {
        uint32_t off = ((uint32_t)data[i] << 16 | (uint32_t)data[i + 1] << 8 | data[i + 2]);
        if (off & 0x800000) {
            size_t len = data[i + 3] % 64;
            off = (off & 0x7fffff) % FLASH_SIZE;
            i += 4;
            if (len > size - i) len = size - i;
            if (len > FLASH_SIZE - off) len = FLASH_SIZE - off;
            memcpy(fl + off, data + i, len);
            i += len;
        } else {
            fl[off % FLASH_SIZE] ^= data[i + 3];
            i += 4;
        }
    }

    store_status_t st = store_mount();
    ASSERT(st == STORE_OK || st == STORE_LOCKED);
    unlock_if_needed();
    exercise();
    if (!store_unlocked()) return 0;

    totp_rec_t t, back;
    memset(&t, 0, sizeof(t));
    strcpy(t.label, "Fuzz");
    t.alg = 0;
    t.digits = 8;
    t.period = 60;
    t.secret_len = 10;
    int slot = store_write_totp(-1, &t);
    if (slot >= 0) {
        ASSERT(store_read_totp((uint16_t)slot, &back));
        ASSERT(memcmp(&back, &t, sizeof(t)) == 0);
        store_mount();
        unlock_if_needed();
        ASSERT(store_read_totp((uint16_t)slot, &back));
        ASSERT(memcmp(&back, &t, sizeof(t)) == 0);
        ASSERT(store_delete((uint16_t)slot));
        ASSERT(store_slot_type((uint16_t)slot) == REC_EMPTY);
    }
    settings_t s = *store_settings();
    s.tz_quarters = 8;
    store_save_settings(&s);
    if (data[0] & 4) store_set_pin(PIN, sizeof(PIN));
    if (data[0] & 8) store_set_pin(NULL, 0);
    store_mount();
    unlock_if_needed();
    exercise();
    ASSERT(store_settings()->tz_quarters == 8 || !store_unlocked());
    return 0;
}
