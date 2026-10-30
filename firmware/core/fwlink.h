/* Firmware transfer over the IR link: the receiver (the bootloader) and the
 * sender (another IRL2FA; tools/irl2fa_update.py speaks the same protocol).
 * Same framing as pairing (docs/pairing-protocol.md):
 *
 *     'I' '2' type len payload[len] crc16(type..payload)   CRC-16/CCITT-FALSE, big endian
 *
 * with three frame types of its own:
 *
 *     0x21 OFFER   sender -> receiver   the 128-byte image header
 *     0x22 DATA    sender -> receiver   offset (u32 LE) | up to 128 payload bytes
 *     0x23 STATUS  receiver -> sender   state (u8) | error (u8) | next (u32 LE) | floor (u32 LE)
 *
 * The receiver answers every OFFER and DATA with a STATUS saying which
 * payload offset it wants next; the sender sends exactly that chunk, and
 * resends after a silence. A lost frame in either direction costs one
 * retry, a chunk is never written twice, and an interrupted transfer of the
 * same image resumes where the staging slot stops being complete. Each side
 * hears its own LED; frames of its own direction are ignored.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_FWLINK_H
#define IRL2FA_FWLINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fwimage.h"

#define FWL_OFFER 0x21
#define FWL_DATA 0x22
#define FWL_STATUS 0x23
#define FWL_CHUNK 128u
#define FWL_FRAME_MAX (4u + 4u + FWL_CHUNK + 2u)
#define FWL_STATUS_LEN 10u

/* Receiver states, as reported in STATUS. */
enum {
    FWRX_LISTEN = 0,    /* waiting for an OFFER */
    FWRX_RECEIVING = 1, /* wants DATA at `next` */
    FWRX_COMPLETE = 2,  /* every byte is in; checking the signature, installing */
    FWRX_DONE = 3,      /* installed; about to restart into it */
    FWRX_FAILED = 4,    /* refused or failed: `error` says why */
};

typedef struct {
    uint8_t buf[FWL_FRAME_MAX];
    size_t len;
} fwl_parser_t;

/* Feeds one byte; true when it completes a frame with a good CRC, whose
 * type and payload are then returned (the payload points into p->buf). */
bool fwl_parse(fwl_parser_t *p, uint8_t c, uint8_t *type, const uint8_t **pl, size_t *len);
/* Builds a frame; returns its length, 0 if it does not fit. */
size_t fwl_frame(uint8_t *out, size_t cap, uint8_t type, const uint8_t *pl, size_t len);

/* ---- receiver ---- */

typedef struct {
    const fw_flash_t *fl;
    const fw_layout_t *lay;
    fw_policy_t pol;
    uint8_t state, err;
    uint8_t hdr[FW_HDR_SIZE];
    uint32_t payload_size;
    uint32_t next;
    bool reply;           /* a STATUS is due */
    fwl_parser_t p;
} fwrx_t;

void fwrx_init(fwrx_t *rx, const fw_flash_t *fl, const fw_layout_t *lay, const fw_policy_t *pol);
/* Feeds received bytes. True if a valid OFFER or DATA frame arrived (the
 * caller then sends fwrx_status() and restarts its inactivity timer). */
bool fwrx_rx(fwrx_t *rx, const uint8_t *data, size_t len);
size_t fwrx_status(fwrx_t *rx, uint8_t *out, size_t cap);
/* The caller's verdict on a COMPLETE image (fwboot_install_staged). A
 * failed image is invalidated, so the same offer starts from scratch. */
void fwrx_finish(fwrx_t *rx, fw_err_t result);

/* ---- sender ---- */

enum {
    FWTX_OFFERING = 0, /* sending OFFER until a receiver answers */
    FWTX_SENDING = 1,
    FWTX_INSTALLING = 2, /* receiver has everything; polling for the outcome */
    FWTX_DONE = 3,
    FWTX_FAILED = 4,
};

#define FWTX_OFFER_MS 600u        /* OFFER repeat */
#define FWTX_RETRY_MS 900u        /* no STATUS after a DATA: resend */
#define FWTX_POLL_MS 2000u        /* while the receiver installs */
#define FWTX_SILENCE_MS 20000u    /* no receiver (any more) */
#define FWTX_INSTALL_MS 120000u   /* longest verify + install */

/* Reads `n` payload bytes at `off`; false on error. */
typedef bool (*fwtx_read_fn)(void *ctx, uint32_t off, uint8_t *buf, size_t n);

typedef struct {
    uint8_t hdr[FW_HDR_SIZE];
    uint32_t payload_size;
    fwtx_read_fn read;
    void *ctx;
    uint8_t state, err;
    uint8_t rx_state;      /* receiver state, last STATUS */
    uint32_t next;         /* receiver's next offset, last STATUS */
    uint32_t rx_floor;
    bool answered;         /* STATUS since our last frame */
    uint32_t last_tx, last_rx, phase_start;
    fwl_parser_t p;
} fwtx_t;

void fwtx_start(fwtx_t *tx, const uint8_t hdr[FW_HDR_SIZE], fwtx_read_fn read, void *ctx,
                uint32_t now);
void fwtx_rx(fwtx_t *tx, const uint8_t *data, size_t len, uint32_t now);
/* The frame to send now, if any (length, 0 for none). Also runs the
 * timeouts. */
size_t fwtx_poll(fwtx_t *tx, uint32_t now, uint8_t *out, size_t cap);
/* 0..1000 of the payload acknowledged. */
unsigned fwtx_permille(const fwtx_t *tx);

#endif
