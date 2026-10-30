/* Signed firmware images: the header, signature check, and the copy from
 * the staging slot to the application slot. See docs/firmware-updates.md.
 *
 * An update image (.irl2fa file, and what the staging slot holds) is
 *
 *     header (128 bytes) | payload (payload_size bytes)
 *
 * where the payload is the application binary, LZMA-compressed (or stored,
 * for tests). The application slot holds the same header, padded with 0xFF
 * to 256 bytes, then the binary itself (its vector table at slot + 0x100).
 *
 * The header's first 64 bytes are its fields; the last 64 are an Ed25519
 * signature (RFC 8032, SHA-512) over SHA-512(header[0..63] || binary). A
 * development image (FW_FLAG_UNSIGNED) carries that SHA-512 digest itself
 * in place of the signature; it is booted only from a chip without readout
 * protection and never accepted as an update.
 *
 * Everything here works on a fw_flash_t, so the host tests run it against
 * a virtual flash that tears operations the way a power cut does.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_FWIMAGE_H
#define IRL2FA_FWIMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hash.h"
#include "lzma_dec.h"

#define FW_MAGIC 0x57463249u /* "I2FW" */
#define FW_HDR_VERSION 1u
#define FW_HDR_SIZE 128u
#define FW_HDR_SIGNED 64u    /* bytes of the header covered by the signature */
#define FW_APP_OFFSET 256u   /* the binary's place in the application slot */
#define FW_FLAG_UNSIGNED 1u
#define FW_COMP_STORED 0u
#define FW_COMP_LZMA 1u
#define FW_PAGE_MAX 2048u

/* Results, also the error codes of the IR protocol and of the bootloader's
 * report to the application (keep docs/firmware-updates.md in step). */
typedef enum {
    FW_OK = 0,
    FW_E_FORMAT = 1,    /* not an IRL2FA image, or a field out of range */
    FW_E_HW = 2,        /* built for other hardware */
    FW_E_BOOT = 3,      /* needs a newer bootloader */
    FW_E_ROLLBACK = 4,  /* older than the firmware already installed */
    FW_E_SIZE = 5,      /* does not fit the slot */
    FW_E_SIGNATURE = 6, /* not signed with the release key */
    FW_E_FLASH = 7,     /* flash read, erase or program failed */
    FW_E_DATA = 8,      /* payload does not decompress */
    FW_E_UNSIGNED = 9,  /* a development image offered as an update */
    FW_E_NOKEY = 10,    /* this bootloader was built without a release key */
    FW_E_CANCELLED = 11,
    FW_E_TIMEOUT = 12,
    FW_E_BATTERY = 13,  /* cell too low to rewrite the flash safely */
} fw_err_t;

/* Flash, addressed as byte offsets from the start of the flash. read()
 * returns false if any double-word it touched failed its ECC check (a
 * torn program or erase); program() takes 8-byte aligned double-words into
 * erased flash. */
typedef struct {
    bool (*read)(void *ctx, uint32_t addr, void *buf, size_t len);
    bool (*erase)(void *ctx, uint32_t addr);   /* the page holding addr */
    bool (*program)(void *ctx, uint32_t addr, const void *buf, size_t len);
    void *ctx;
    uint32_t page_size;                         /* <= FW_PAGE_MAX */
} fw_flash_t;

typedef struct {
    uint32_t app_addr, app_size;         /* application slot */
    uint32_t staging_addr, staging_size; /* staging slot */
    uint32_t log_addr;                   /* two pages: fwlog.h */
} fw_layout_t;

typedef struct {
    uint32_t hw_id;
    uint32_t boot_version;
    uint32_t floor;            /* lowest version still accepted (anti-rollback) */
    const uint8_t *pubkey;     /* 32-byte Ed25519 key; NULL: none built in */
    bool allow_unsigned;       /* application slot only: RDP level 0 */
} fw_policy_t;

typedef struct {
    uint16_t hdr_version, flags;
    uint32_t version, hw_id, min_boot, image_size, payload_size;
    uint8_t comp, props, dict_log2;
    uint8_t sig[64];
} fw_header_t;

/* Working memory for verify/install (about 24 KB; one static instance in
 * the bootloader). buf is scratch, page0 and page are pages being built. */
typedef struct {
    const fw_flash_t *fl;
    uint32_t in_addr, in_left;
    bool in_error;
    bool lz;
    lzma_dec_t dec;
    hash_ctx_t hash;
    uint8_t buf[FW_PAGE_MAX];
    uint8_t page0[FW_PAGE_MAX];
    uint8_t page[FW_PAGE_MAX];
} fw_work_t;

fw_err_t fw_header_parse(const uint8_t raw[FW_HDR_SIZE], fw_header_t *h);
void fw_header_write(const fw_header_t *h, uint8_t raw[FW_HDR_SIZE]);

/* Hardware ID, bootloader version, anti-rollback floor and sizes. The
 * floor applies to signed images only. */
fw_err_t fw_header_check(const fw_header_t *h, const fw_policy_t *pol, const fw_layout_t *lay);

/* Standard Ed25519 (RFC 8032) verification, 0 if valid. */
int fw_ed25519_check(const uint8_t sig[64], const uint8_t pub[32], const uint8_t *msg, size_t len);

/* The staging slot holds a complete image that passes fw_header_check and
 * is signed with pol->pubkey. */
fw_err_t fw_verify_staging(fw_work_t *w, const fw_flash_t *fl, const fw_layout_t *lay,
                           const fw_policy_t *pol, fw_header_t *out);

/* The application slot holds an image that passes fw_header_check and is
 * signed (or unsigned, if pol->allow_unsigned). */
fw_err_t fw_verify_app(fw_work_t *w, const fw_flash_t *fl, const fw_layout_t *lay,
                       const fw_policy_t *pol, fw_header_t *out);

/* Copies the (already verified) staging image into the application slot.
 * Idempotent: pages already holding the right bytes are left alone, so a
 * copy cut short by a power loss is finished by running it again. Page 0
 * (the header) is written last. */
fw_err_t fw_install(fw_work_t *w, const fw_flash_t *fl, const fw_layout_t *lay);

#endif
