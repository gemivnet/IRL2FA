/* Encrypted, power-fail-safe record store in internal flash.
 *
 * Layout (2 KB pages, see HAL_FLASH_PAGE):
 *   page 0          settings + (wrapped) master key
 *   pages 1, 2      journal header + data, for atomic page rewrites
 *   page 3          PIN attempt log (append-only, rarely erased)
 *   pages 4..39     16 slots of 128 bytes each
 *
 * Every slot's payload (labels included) is sealed with XChaCha20-Poly1305
 * under a random 256-bit master key. Without a PIN the master key is stored
 * as-is (the flash is protected by the MCU's readout protection); with a PIN
 * it is wrapped with a key derived from the PIN by Argon2i.
 *
 * Simple mode (docs call it "vulnerable (simple) mode", docs/ui-flows.md) is
 * a kiosk mode for someone who should only check who is calling. Its flag
 * lives in the settings page; the family PIN that leaves it is kept only as
 * an Argon2i verifier, sealed under the master key in the same page, so the
 * flag and the verifier change together in one atomic page write. The
 * family PIN does not encrypt anything and is separate from the device PIN.
 * Wrong family PINs are counted in the PIN attempt log (page 3) with the
 * RTC time of each try, and cost an escalating wait that survives resets
 * and sleep; they never erase anything.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_STORE_H
#define IRL2FA_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "hal.h"
#include "hash.h"

#define STORE_SLOT_SIZE 128u
#define STORE_SLOTS_PER_PAGE (HAL_FLASH_PAGE / STORE_SLOT_SIZE)
#define STORE_FIRST_SLOT_PAGE 4u
#define STORE_MAX_SLOTS 576u /* 512 codes + 64 contacts, or any mix */
#define STORE_LABEL_LEN 16   /* bytes including the terminating NUL */
#define STORE_SECRET_MAX 64
#define STORE_PIN_MAX 12
#define STORE_PIN_MIN 4
#define STORE_PIN_TRIES 10   /* wrong PINs before the store is wiped */
/* No wipe for the family PIN, so it is longer: 4^6 = 4096 sequences at up to
 * an hour each after the first few is months of guessing. */
#define STORE_FAMILY_PIN_MIN 6
#define STORE_FAMILY_FREE_TRIES 3 /* wrong family PINs before the waits start */

typedef enum {
    REC_EMPTY = 0xff,
    REC_TOTP = 0x01,
    REC_CONTACT = 0x02,
} rec_type_t;

typedef struct {
    char label[STORE_LABEL_LEN];
    uint8_t alg;     /* hash_alg_t */
    uint8_t digits;  /* 6..8 */
    uint8_t period;  /* seconds, usually 30 */
    uint8_t secret_len;
    uint8_t secret[STORE_SECRET_MAX];
} totp_rec_t;

typedef struct {
    char name[STORE_LABEL_LEN];
    uint8_t key[32];
    uint8_t is_low;
    uint8_t reserved[3];
    uint32_t paired_at; /* unix time, informational */
} contact_rec_t;

typedef struct {
    int8_t tz_quarters;   /* local time offset from UTC in 15 min steps */
    uint8_t sleep_s;      /* idle seconds before sleeping, 0 = default */
    uint8_t pin_set;
    uint8_t pin_fails;
    uint8_t simple;       /* simple mode on (read-only here: store_set_simple) */
} settings_t;

typedef enum {
    STORE_OK = 0,
    STORE_LOCKED,      /* PIN set and not yet entered */
    STORE_BAD_PIN,
    STORE_WIPED,       /* too many wrong PINs; everything erased */
    STORE_FULL,
    STORE_ERR,
    STORE_WAIT,        /* family PIN: too soon after the last wrong one */
} store_status_t;

/* Mounts the store, finishing any interrupted page rewrite and creating a
 * fresh master key on first boot. Returns STORE_OK or STORE_LOCKED. */
store_status_t store_mount(void);
bool store_unlocked(void);
void store_lock(void);
store_status_t store_unlock(const uint8_t *pin, int len);
/* Sets, changes or (len == 0) removes the PIN. Store must be unlocked. */
store_status_t store_set_pin(const uint8_t *pin, int len);
uint8_t store_pin_fails(void);

/* Simple mode. store_set_simple() turns it on with a family PIN of
 * STORE_FAMILY_PIN_MIN..STORE_PIN_MAX moves, or off with len == 0; the
 * store must be unlocked. store_check_family_pin() checks a family PIN at
 * RTC time `now`: STORE_OK, STORE_BAD_PIN, STORE_WAIT (store_family_wait_s
 * is not 0 yet), STORE_LOCKED, or STORE_ERR if simple mode is off. If the
 * sealed verifier does not open (a damaged settings page), simple mode is
 * turned off and STORE_OK returned: the family must never be locked out of
 * the device for good, and changing flash means the attacker has it all. */
store_status_t store_set_simple(const uint8_t *pin, int len);
store_status_t store_check_family_pin(const uint8_t *pin, int len, int64_t now);
uint8_t store_family_fails(void);
/* Seconds before the next family PIN may be tried, at RTC time `now`. */
uint32_t store_family_wait_s(int64_t now);

const settings_t *store_settings(void);
store_status_t store_save_settings(const settings_t *s);

/* Slots. Index i is 0..STORE_MAX_SLOTS-1. */
rec_type_t store_slot_type(uint16_t i);
int store_count(rec_type_t t);
/* The `n`th slot of type t in slot order, or -1. */
int store_nth(rec_type_t t, int n);
/* Position of slot i among slots of its type (inverse of store_nth). */
int store_rank(uint16_t i);

bool store_read_totp(uint16_t i, totp_rec_t *out);
bool store_read_contact(uint16_t i, contact_rec_t *out);
/* Writes to slot i, or to the first empty slot if i < 0. Returns the slot
 * index or -1 if full. */
int store_write_totp(int i, const totp_rec_t *rec);
int store_write_contact(int i, const contact_rec_t *rec);
bool store_delete(uint16_t i);

/* Erases every page and starts over with a new master key. */
void store_wipe(void);

#endif
