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
} settings_t;

typedef enum {
    STORE_OK = 0,
    STORE_LOCKED,      /* PIN set and not yet entered */
    STORE_BAD_PIN,
    STORE_WIPED,       /* too many wrong PINs; everything erased */
    STORE_FULL,
    STORE_ERR,
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
