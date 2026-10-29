/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "store.h"

#include <string.h>

#include "hal.h"
#include "third_party/monocypher/monocypher.h"

#define META_PAGE 0u
#define JOURNAL_HDR_PAGE 1u
#define JOURNAL_DATA_PAGE 2u
#define PINLOG_PAGE 3u
#define SLOT_PAGES (STORE_MAX_SLOTS / STORE_SLOTS_PER_PAGE)

#define META_MAGIC 0x534c5249u    /* "IRLS" */
#define JOURNAL_MAGIC 0x4a4c5249u /* "IRLJ" */
#define PINLOG_TRY 0x00000000f0f0f0f0ull
#define PINLOG_OK 0x000000000f0f0f0full

/* Slot: type, 3 reserved, nonce, mac, ciphertext. */
#define SLOT_NONCE 4
#define SLOT_MAC 28
#define SLOT_CT 44
#define SLOT_PT_LEN (STORE_SLOT_SIZE - SLOT_CT) /* 84 */

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t pin_set;
    int8_t tz_quarters;
    uint8_t sleep_s;
    uint8_t salt[16];
    uint8_t nonce[24];
    uint8_t mac[16];
    uint8_t mk[32]; /* master key, wrapped when pin_set */
} meta_t;          /* 96 bytes */

typedef struct {
    uint32_t magic;
    uint32_t target;
    uint32_t crc;
    uint32_t inv_crc;
} journal_t;

_Static_assert(sizeof(totp_rec_t) <= SLOT_PT_LEN, "TOTP record too large");
_Static_assert(sizeof(contact_rec_t) <= SLOT_PT_LEN, "contact record too large");
_Static_assert(sizeof(meta_t) % 8 == 0, "meta must be double-word sized");
_Static_assert(STORE_MAX_SLOTS % STORE_SLOTS_PER_PAGE == 0, "whole pages of slots");

static uint8_t page_buf[HAL_FLASH_PAGE];
static meta_t meta;
static settings_t settings;
static uint8_t master_key[32];
static bool unlocked;
static uint8_t slot_types[STORE_MAX_SLOTS];

static uint32_t crc32(const uint8_t *d, size_t n)
{
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static uint32_t page_off(uint32_t page)
{
    return page * HAL_FLASH_PAGE;
}

/* Atomically replaces `page` with page_buf: data and a header naming the
 * target go to the journal first, so a power cut at any point leaves either
 * the old page or a journal that store_mount() replays. */
static void commit_page(uint32_t page)
{
    journal_t j = {JOURNAL_MAGIC, page, crc32(page_buf, HAL_FLASH_PAGE), 0};
    j.inv_crc = ~j.crc;
    hal_flash_erase(JOURNAL_HDR_PAGE);
    hal_flash_erase(JOURNAL_DATA_PAGE);
    hal_flash_write(page_off(JOURNAL_DATA_PAGE), page_buf, HAL_FLASH_PAGE);
    hal_flash_write(page_off(JOURNAL_HDR_PAGE), &j, sizeof(j));
    hal_flash_erase(page);
    hal_flash_write(page_off(page), page_buf, HAL_FLASH_PAGE);
    hal_flash_erase(JOURNAL_HDR_PAGE);
}

static void replay_journal(void)
{
    journal_t j;
    hal_flash_read(page_off(JOURNAL_HDR_PAGE), &j, sizeof(j));
    if (j.magic != JOURNAL_MAGIC || j.inv_crc != ~j.crc) return;
    if (j.target == JOURNAL_HDR_PAGE || j.target == JOURNAL_DATA_PAGE ||
        j.target >= STORE_FIRST_SLOT_PAGE + SLOT_PAGES) {
        hal_flash_erase(JOURNAL_HDR_PAGE);
        return;
    }
    hal_flash_read(page_off(JOURNAL_DATA_PAGE), page_buf, HAL_FLASH_PAGE);
    if (crc32(page_buf, HAL_FLASH_PAGE) != j.crc) {
        hal_flash_erase(JOURNAL_HDR_PAGE); /* torn journal: old page is intact */
        return;
    }
    hal_flash_erase(j.target);
    hal_flash_write(page_off(j.target), page_buf, HAL_FLASH_PAGE);
    hal_flash_erase(JOURNAL_HDR_PAGE);
}

static void write_meta(void)
{
    memset(page_buf, 0xff, sizeof(page_buf));
    memcpy(page_buf, &meta, sizeof(meta));
    commit_page(META_PAGE);
}

static void scan_types(void)
{
    for (uint16_t i = 0; i < STORE_MAX_SLOTS; i++) {
        uint32_t off = page_off(STORE_FIRST_SLOT_PAGE + i / STORE_SLOTS_PER_PAGE) +
                       (i % STORE_SLOTS_PER_PAGE) * STORE_SLOT_SIZE;
        uint8_t t;
        hal_flash_read(off, &t, 1);
        slot_types[i] = (t == REC_TOTP || t == REC_CONTACT) ? t : REC_EMPTY;
    }
}

static void load_settings(void)
{
    settings.tz_quarters = meta.tz_quarters;
    settings.sleep_s = meta.sleep_s;
    settings.pin_set = meta.pin_set;
    settings.pin_fails = store_pin_fails();
}

static void fresh_store(void)
{
    for (uint32_t p = 0; p < STORE_FIRST_SLOT_PAGE + SLOT_PAGES; p++) hal_flash_erase(p);
    memset(&meta, 0, sizeof(meta));
    meta.magic = META_MAGIC;
    meta.version = 1;
    hal_random(meta.mk, sizeof(meta.mk));
    memcpy(master_key, meta.mk, sizeof(master_key));
    write_meta();
    unlocked = true;
}

store_status_t store_mount(void)
{
    replay_journal();
    hal_flash_read(page_off(META_PAGE), &meta, sizeof(meta));
    if (meta.magic != META_MAGIC || meta.version != 1) fresh_store();
    scan_types();
    if (meta.pin_set) {
        unlocked = false;
        crypto_wipe(master_key, sizeof(master_key));
    } else {
        memcpy(master_key, meta.mk, sizeof(master_key));
        unlocked = true;
    }
    load_settings();
    return unlocked ? STORE_OK : STORE_LOCKED;
}

bool store_unlocked(void)
{
    return unlocked;
}

void store_lock(void)
{
    if (!meta.pin_set) return;
    crypto_wipe(master_key, sizeof(master_key));
    unlocked = false;
}

void store_wipe(void)
{
    crypto_wipe(master_key, sizeof(master_key));
    fresh_store();
    scan_types();
    load_settings();
}

/* ---- PIN ---- */

static void pin_kek(const uint8_t *pin, int len, const uint8_t salt[16], uint8_t kek[32])
{
    /* 8 KiB, 4 passes: about a second on the device. The real protection
     * against offline guessing is the MCU's flash readout protection; the
     * KDF and the attempt limit protect against someone holding the device. */
    static uint8_t work[8 * 1024];
    crypto_argon2_config cfg = {
        .algorithm = CRYPTO_ARGON2_I, .nb_blocks = 8, .nb_passes = 4, .nb_lanes = 1};
    crypto_argon2_inputs in = {
        .pass = pin, .salt = salt, .pass_size = (uint32_t)len, .salt_size = 16};
    crypto_argon2(kek, 32, work, cfg, in, crypto_argon2_no_extras);
    crypto_wipe(work, sizeof(work));
}

/* The attempt log is a page of 8-byte entries: TRY before every attempt,
 * OK after a correct PIN. Failures = TRYs after the last OK. Writing TRY
 * before checking means pulling the battery mid-attempt still counts. */
static int pinlog_used(void)
{
    for (int i = 0; i < (int)(HAL_FLASH_PAGE / 8); i++) {
        uint64_t v;
        hal_flash_read(page_off(PINLOG_PAGE) + (uint32_t)i * 8, &v, 8);
        if (v == UINT64_MAX) return i;
    }
    return HAL_FLASH_PAGE / 8;
}

static void pinlog_append(uint64_t v)
{
    int n = pinlog_used();
    if (n >= (int)(HAL_FLASH_PAGE / 8) - 1) {
        /* Compact: keep only the current failure count. */
        uint8_t fails = store_pin_fails();
        hal_flash_erase(PINLOG_PAGE);
        n = 0;
        uint64_t t = PINLOG_TRY;
        for (uint8_t k = 0; k < fails; k++, n++)
            hal_flash_write(page_off(PINLOG_PAGE) + (uint32_t)n * 8, &t, 8);
    }
    hal_flash_write(page_off(PINLOG_PAGE) + (uint32_t)n * 8, &v, 8);
}

uint8_t store_pin_fails(void)
{
    uint8_t fails = 0;
    int n = pinlog_used();
    for (int i = 0; i < n; i++) {
        uint64_t v;
        hal_flash_read(page_off(PINLOG_PAGE) + (uint32_t)i * 8, &v, 8);
        if (v == PINLOG_OK) fails = 0;
        else if (fails < 255) fails++;
    }
    return fails;
}

store_status_t store_unlock(const uint8_t *pin, int len)
{
    if (!meta.pin_set || unlocked) return STORE_OK;
    pinlog_append(PINLOG_TRY);
    uint8_t kek[32];
    pin_kek(pin, len, meta.salt, kek);
    int bad = crypto_aead_unlock(master_key, meta.mac, kek, meta.nonce, NULL, 0, meta.mk, 32);
    crypto_wipe(kek, sizeof(kek));
    if (bad) {
        crypto_wipe(master_key, sizeof(master_key));
        settings.pin_fails = store_pin_fails();
        if (settings.pin_fails >= STORE_PIN_TRIES) {
            store_wipe();
            return STORE_WIPED;
        }
        return STORE_BAD_PIN;
    }
    pinlog_append(PINLOG_OK);
    settings.pin_fails = 0;
    unlocked = true;
    return STORE_OK;
}

store_status_t store_set_pin(const uint8_t *pin, int len)
{
    if (!unlocked) return STORE_LOCKED;
    if (len == 0) {
        meta.pin_set = 0;
        memcpy(meta.mk, master_key, 32);
        memset(meta.salt, 0, sizeof(meta.salt));
        memset(meta.nonce, 0, sizeof(meta.nonce));
        memset(meta.mac, 0, sizeof(meta.mac));
    } else {
        if (len < STORE_PIN_MIN || len > STORE_PIN_MAX) return STORE_ERR;
        uint8_t kek[32];
        hal_random(meta.salt, sizeof(meta.salt));
        hal_random(meta.nonce, sizeof(meta.nonce));
        pin_kek(pin, len, meta.salt, kek);
        crypto_aead_lock(meta.mk, meta.mac, kek, meta.nonce, NULL, 0, master_key, 32);
        crypto_wipe(kek, sizeof(kek));
        meta.pin_set = 1;
    }
    write_meta();
    hal_flash_erase(PINLOG_PAGE);
    load_settings();
    return STORE_OK;
}

/* ---- settings ---- */

const settings_t *store_settings(void)
{
    return &settings;
}

store_status_t store_save_settings(const settings_t *s)
{
    meta.tz_quarters = s->tz_quarters;
    meta.sleep_s = s->sleep_s;
    write_meta();
    load_settings();
    return STORE_OK;
}

/* ---- slots ---- */

rec_type_t store_slot_type(uint16_t i)
{
    return i < STORE_MAX_SLOTS ? (rec_type_t)slot_types[i] : REC_EMPTY;
}

int store_count(rec_type_t t)
{
    int n = 0;
    for (uint16_t i = 0; i < STORE_MAX_SLOTS; i++) n += slot_types[i] == t;
    return n;
}

int store_nth(rec_type_t t, int n)
{
    for (uint16_t i = 0; i < STORE_MAX_SLOTS; i++) {
        if (slot_types[i] == t && n-- == 0) return i;
    }
    return -1;
}

int store_rank(uint16_t i)
{
    int r = 0;
    for (uint16_t k = 0; k < i && k < STORE_MAX_SLOTS; k++) r += slot_types[k] == slot_types[i];
    return r;
}

static void slot_ad(uint16_t i, uint8_t type, uint8_t ad[3])
{
    ad[0] = (uint8_t)(i >> 8);
    ad[1] = (uint8_t)i;
    ad[2] = type;
}

static bool read_slot(uint16_t i, rec_type_t want, void *out, size_t len)
{
    if (!unlocked || i >= STORE_MAX_SLOTS || slot_types[i] != want) return false;
    uint8_t raw[STORE_SLOT_SIZE], pt[SLOT_PT_LEN], ad[3];
    uint32_t off = page_off(STORE_FIRST_SLOT_PAGE + i / STORE_SLOTS_PER_PAGE) +
                   (i % STORE_SLOTS_PER_PAGE) * STORE_SLOT_SIZE;
    hal_flash_read(off, raw, sizeof(raw));
    slot_ad(i, raw[0], ad);
    if (crypto_aead_unlock(pt, raw + SLOT_MAC, master_key, raw + SLOT_NONCE, ad, sizeof(ad),
                           raw + SLOT_CT, SLOT_PT_LEN) != 0)
        return false;
    memcpy(out, pt, len);
    crypto_wipe(pt, sizeof(pt));
    return true;
}

static int write_slot(int i, rec_type_t type, const void *rec, size_t len)
{
    if (!unlocked) return -1;
    if (i < 0) {
        for (uint16_t k = 0; k < STORE_MAX_SLOTS; k++) {
            if (slot_types[k] == REC_EMPTY) {
                i = k;
                break;
            }
        }
        if (i < 0) return -1;
    }
    if (i >= (int)STORE_MAX_SLOTS) return -1;

    uint32_t page = STORE_FIRST_SLOT_PAGE + (uint32_t)i / STORE_SLOTS_PER_PAGE;
    uint8_t *slot = page_buf + (i % STORE_SLOTS_PER_PAGE) * STORE_SLOT_SIZE;
    hal_flash_read(page_off(page), page_buf, HAL_FLASH_PAGE);

    uint8_t pt[SLOT_PT_LEN], ad[3];
    memset(pt, 0, sizeof(pt));
    memcpy(pt, rec, len);
    memset(slot, 0, STORE_SLOT_SIZE);
    slot[0] = (uint8_t)type;
    hal_random(slot + SLOT_NONCE, 24);
    slot_ad((uint16_t)i, (uint8_t)type, ad);
    crypto_aead_lock(slot + SLOT_CT, slot + SLOT_MAC, master_key, slot + SLOT_NONCE, ad,
                     sizeof(ad), pt, sizeof(pt));
    crypto_wipe(pt, sizeof(pt));
    commit_page(page);
    slot_types[i] = (uint8_t)type;
    return i;
}

bool store_read_totp(uint16_t i, totp_rec_t *out)
{
    return read_slot(i, REC_TOTP, out, sizeof(*out));
}

bool store_read_contact(uint16_t i, contact_rec_t *out)
{
    return read_slot(i, REC_CONTACT, out, sizeof(*out));
}

int store_write_totp(int i, const totp_rec_t *rec)
{
    return write_slot(i, REC_TOTP, rec, sizeof(*rec));
}

int store_write_contact(int i, const contact_rec_t *rec)
{
    return write_slot(i, REC_CONTACT, rec, sizeof(*rec));
}

bool store_delete(uint16_t i)
{
    if (!unlocked || i >= STORE_MAX_SLOTS) return false;
    uint32_t page = STORE_FIRST_SLOT_PAGE + i / STORE_SLOTS_PER_PAGE;
    hal_flash_read(page_off(page), page_buf, HAL_FLASH_PAGE);
    memset(page_buf + (i % STORE_SLOTS_PER_PAGE) * STORE_SLOT_SIZE, 0xff, STORE_SLOT_SIZE);
    commit_page(page);
    slot_types[i] = REC_EMPTY;
    return true;
}
