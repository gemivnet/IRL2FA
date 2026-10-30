/* The bootloader's journal: the anti-rollback floor and whether an install
 * is under way, kept power-fail safe in two flash pages.
 *
 * Each page is a log of 8-byte records written once each:
 *
 *     type (1) | value (4, little endian) | 0xA5 | CRC-16/CCITT-FALSE (2, big endian)
 *
 * Double-word 0 of a page is its SEQ record, written last when a page is
 * started, so a page is only ever in use once everything copied into it is
 * there; the page with the higher SEQ is the live one. A record cut short by
 * a power loss reads as noise or as an ECC error and is skipped; the next
 * record goes after it. When the live page is full, the state (floor,
 * pending install) is copied into the other page, which then takes over.
 *
 *     INSTALL(v)  the staging slot holds a verified image v; copying it into
 *                 the application slot may have started
 *     DONE(v)     v is installed and verified: the floor rises to v
 *     CANCEL(v)   the pending install of v was given up
 *     FLOOR(v)    the floor, carried over into a new page
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_FWLOG_H
#define IRL2FA_FWLOG_H

#include <stdbool.h>
#include <stdint.h>

#include "fwimage.h"

enum { FWLOG_SEQ = 1, FWLOG_FLOOR = 2, FWLOG_INSTALL = 3, FWLOG_DONE = 4, FWLOG_CANCEL = 5 };

typedef struct {
    uint32_t floor;        /* lowest version that may be installed or booted (signed) */
    bool pending;          /* INSTALL(pending_version) without DONE/CANCEL */
    uint32_t pending_version;
    int page;              /* live page 0 or 1, -1 if neither is */
    uint32_t seq;
    uint32_t next;         /* offset of the next free record in the live page */
    uint32_t skipped;      /* torn records seen while loading */
} fwlog_t;

void fwlog_load(fwlog_t *log, const fw_flash_t *fl, const fw_layout_t *lay);
/* Appends a record (starting a new page first if needed) and applies it.
 * False if the flash refused, the state is then as before. */
bool fwlog_append(fwlog_t *log, const fw_flash_t *fl, const fw_layout_t *lay, uint8_t type,
                  uint32_t value);

#endif
