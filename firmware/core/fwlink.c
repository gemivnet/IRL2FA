/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "fwlink.h"

#include <string.h>

#include "pairing.h" /* pairing_crc16: the same framing as pairing */

#define HDR 4u
#define CRC 2u

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

bool fwl_parse(fwl_parser_t *p, uint8_t c, uint8_t *type, const uint8_t **pl, size_t *len)
{
    if (p->len == 0 && c != 'I') return false;
    if (p->len == 1 && c != '2') {
        p->len = (c == 'I') ? 1 : 0;
        return false;
    }
    p->buf[p->len++] = c;
    if (p->len < HDR) return false;
    size_t plen = p->buf[3];
    if (plen > FWL_FRAME_MAX - HDR - CRC) {
        p->len = 0;
        return false;
    }
    if (p->len < HDR + plen + CRC) return false;
    p->len = 0;
    uint16_t want = (uint16_t)((p->buf[HDR + plen] << 8) | p->buf[HDR + plen + 1]);
    if (pairing_crc16(p->buf + 2, 2 + plen) != want) return false;
    *type = p->buf[2];
    *pl = p->buf + HDR;
    *len = plen;
    return true;
}

size_t fwl_frame(uint8_t *out, size_t cap, uint8_t type, const uint8_t *pl, size_t len)
{
    if (len > 255 || cap < HDR + len + CRC) return 0;
    out[0] = 'I';
    out[1] = '2';
    out[2] = type;
    out[3] = (uint8_t)len;
    if (len) memcpy(out + HDR, pl, len);
    uint16_t crc = pairing_crc16(out + 2, 2 + len);
    out[HDR + len] = (uint8_t)(crc >> 8);
    out[HDR + len + 1] = (uint8_t)crc;
    return HDR + len + CRC;
}

/* ------------------------------------------------------------------ */
/* receiver                                                           */

void fwrx_init(fwrx_t *rx, const fw_flash_t *fl, const fw_layout_t *lay, const fw_policy_t *pol)
{
    memset(rx, 0, sizeof(*rx));
    rx->fl = fl;
    rx->lay = lay;
    rx->pol = *pol;
    rx->state = FWRX_LISTEN;
}

static void fail(fwrx_t *rx, fw_err_t e)
{
    rx->state = FWRX_FAILED;
    rx->err = (uint8_t)e;
}

/* Payload bytes [c, c + n) are in the staging slot: readable and with no
 * double-word still erased. (A chunk of real 0xFF bytes reads as missing
 * and is fetched again: harmless.) */
static bool chunk_complete(const fwrx_t *rx, uint32_t c, uint32_t n)
{
    uint8_t b[FWL_CHUNK];
    uint32_t n8 = (n + 7u) & ~7u;
    if (!rx->fl->read(rx->fl->ctx, rx->lay->staging_addr + FW_HDR_SIZE + c, b, n8)) return false;
    for (uint32_t i = 0; i < n8; i += 8) {
        bool erased = true;
        for (uint32_t k = 0; k < 8; k++) erased = erased && b[i + k] == 0xff;
        if (erased) return false;
    }
    return true;
}

static void start(fwrx_t *rx, const uint8_t *hdr)
{
    const fw_flash_t *fl = rx->fl;
    fw_header_t h;
    fw_err_t e = fw_header_parse(hdr, &h);
    if (e == FW_OK) e = fw_header_check(&h, &rx->pol, rx->lay);
    if (e == FW_OK && (h.flags & FW_FLAG_UNSIGNED)) e = FW_E_UNSIGNED;
    if (e == FW_OK && rx->pol.pubkey == NULL) e = FW_E_NOKEY;
    if (e != FW_OK) {
        fail(rx, e);
        return;
    }
    memcpy(rx->hdr, hdr, FW_HDR_SIZE);
    rx->payload_size = h.payload_size;
    rx->err = FW_OK;

    /* The same image as the one partly in staging: carry on from the start
     * of the page holding the first incomplete chunk. */
    uint8_t have[FW_HDR_SIZE];
    if (fl->read(fl->ctx, rx->lay->staging_addr, have, FW_HDR_SIZE) &&
        memcmp(have, hdr, FW_HDR_SIZE) == 0) {
        uint32_t c = 0;
        while (c < rx->payload_size) {
            uint32_t n = rx->payload_size - c;
            if (n > FWL_CHUNK) n = FWL_CHUNK;
            if (!chunk_complete(rx, c, n)) break;
            c += n;
        }
        if (c >= rx->payload_size) {
            rx->next = rx->payload_size;
            rx->state = FWRX_COMPLETE;
            return;
        }
        uint32_t page = (FW_HDR_SIZE + c) / fl->page_size * fl->page_size;
        if (page > 0) {
            rx->next = page - FW_HDR_SIZE;
            rx->state = FWRX_RECEIVING;
            return;
        }
    }
    if (!fl->erase(fl->ctx, rx->lay->staging_addr) ||
        !fl->program(fl->ctx, rx->lay->staging_addr, hdr, FW_HDR_SIZE)) {
        fail(rx, FW_E_FLASH);
        return;
    }
    rx->next = 0;
    rx->state = FWRX_RECEIVING;
}

static void data(fwrx_t *rx, const uint8_t *pl, size_t len)
{
    if (rx->state != FWRX_RECEIVING || len <= 4) return;
    const fw_flash_t *fl = rx->fl;
    uint32_t off = get32(pl);
    uint32_t n = (uint32_t)len - 4u;
    if (off != rx->next || n > FWL_CHUNK || n > rx->payload_size - off) return;
    if (n != FWL_CHUNK && off + n != rx->payload_size) return;
    uint32_t rel = FW_HDR_SIZE + off;
    uint32_t addr = rx->lay->staging_addr + rel;
    if (rel % fl->page_size == 0 && !fl->erase(fl->ctx, addr)) {
        fail(rx, FW_E_FLASH);
        return;
    }
    uint8_t b[FWL_CHUNK];
    memset(b, 0xff, sizeof(b));
    memcpy(b, pl + 4, n);
    if (!fl->program(fl->ctx, addr, b, (n + 7u) & ~7u)) {
        fail(rx, FW_E_FLASH);
        return;
    }
    rx->next += n;
    if (rx->next == rx->payload_size) rx->state = FWRX_COMPLETE;
}

bool fwrx_rx(fwrx_t *rx, const uint8_t *buf, size_t len)
{
    bool any = false;
    for (size_t i = 0; i < len; i++) {
        uint8_t type;
        const uint8_t *pl;
        size_t n;
        if (!fwl_parse(&rx->p, buf[i], &type, &pl, &n)) continue;
        if (type == FWL_OFFER && n == FW_HDR_SIZE) {
            any = true;
            if (rx->state == FWRX_LISTEN || rx->state == FWRX_FAILED ||
                (rx->state == FWRX_RECEIVING && memcmp(rx->hdr, pl, FW_HDR_SIZE) != 0))
                start(rx, pl);
        } else if (type == FWL_DATA && n > 4) {
            any = true;
            data(rx, pl, n);
        }
    }
    if (any) rx->reply = true;
    return any;
}

size_t fwrx_status(fwrx_t *rx, uint8_t *out, size_t cap)
{
    uint8_t pl[FWL_STATUS_LEN];
    pl[0] = rx->state;
    pl[1] = rx->err;
    put32(pl + 2, rx->next);
    put32(pl + 6, rx->pol.floor);
    rx->reply = false;
    return fwl_frame(out, cap, FWL_STATUS, pl, sizeof(pl));
}

void fwrx_finish(fwrx_t *rx, fw_err_t result)
{
    if (result == FW_OK) {
        rx->state = FWRX_DONE;
        rx->err = FW_OK;
        return;
    }
    rx->fl->erase(rx->fl->ctx, rx->lay->staging_addr);
    fail(rx, result);
}

/* ------------------------------------------------------------------ */
/* sender                                                             */

void fwtx_start(fwtx_t *tx, const uint8_t hdr[FW_HDR_SIZE], fwtx_read_fn read, void *ctx,
                uint32_t now)
{
    memset(tx, 0, sizeof(*tx));
    memcpy(tx->hdr, hdr, FW_HDR_SIZE);
    tx->payload_size = get32(hdr + 24);
    tx->read = read;
    tx->ctx = ctx;
    tx->state = FWTX_OFFERING;
    tx->last_tx = now - FWTX_OFFER_MS;
    tx->last_rx = now;
    tx->phase_start = now;
}

static void to(fwtx_t *tx, uint8_t state, uint32_t now)
{
    if (tx->state != state) tx->phase_start = now;
    tx->state = state;
}

void fwtx_rx(fwtx_t *tx, const uint8_t *buf, size_t len, uint32_t now)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t type;
        const uint8_t *pl;
        size_t n;
        if (!fwl_parse(&tx->p, buf[i], &type, &pl, &n)) continue;
        if (type != FWL_STATUS || n != FWL_STATUS_LEN) continue;
        if (tx->state == FWTX_DONE || tx->state == FWTX_FAILED) continue;
        tx->rx_state = pl[0];
        tx->next = get32(pl + 2);
        tx->rx_floor = get32(pl + 6);
        tx->answered = true;
        tx->last_rx = now;
        switch (pl[0]) {
        case FWRX_LISTEN: to(tx, FWTX_OFFERING, now); break;
        case FWRX_RECEIVING:
            if (tx->next > tx->payload_size) to(tx, FWTX_OFFERING, now);
            else to(tx, FWTX_SENDING, now);
            break;
        case FWRX_COMPLETE: to(tx, FWTX_INSTALLING, now); break;
        case FWRX_DONE: to(tx, FWTX_DONE, now); break;
        default:
            tx->err = pl[1] ? pl[1] : (uint8_t)FW_E_FORMAT;
            to(tx, FWTX_FAILED, now);
            break;
        }
    }
}

static size_t data_frame(fwtx_t *tx, uint32_t off, uint8_t *out, size_t cap)
{
    uint8_t pl[4 + FWL_CHUNK];
    uint32_t n = tx->payload_size - off;
    if (n > FWL_CHUNK) n = FWL_CHUNK;
    put32(pl, off);
    if (!tx->read(tx->ctx, off, pl + 4, n)) {
        tx->err = FW_E_FLASH;
        tx->state = FWTX_FAILED;
        return 0;
    }
    return fwl_frame(out, cap, FWL_DATA, pl, 4 + n);
}

size_t fwtx_poll(fwtx_t *tx, uint32_t now, uint8_t *out, size_t cap)
{
    if (tx->state == FWTX_DONE || tx->state == FWTX_FAILED) return 0;
    bool timeout = tx->state == FWTX_INSTALLING ? now - tx->phase_start > FWTX_INSTALL_MS
                                                : now - tx->last_rx > FWTX_SILENCE_MS;
    if (timeout) {
        tx->err = FW_E_TIMEOUT;
        tx->state = FWTX_FAILED;
        return 0;
    }
    size_t n = 0;
    uint32_t since = now - tx->last_tx;
    switch (tx->state) {
    case FWTX_OFFERING:
        if (since >= FWTX_OFFER_MS) n = fwl_frame(out, cap, FWL_OFFER, tx->hdr, FW_HDR_SIZE);
        break;
    case FWTX_SENDING:
        if (tx->answered || since >= FWTX_RETRY_MS) n = data_frame(tx, tx->next, out, cap);
        break;
    default: /* FWTX_INSTALLING: any DATA gets a STATUS back */
        if (since >= FWTX_POLL_MS) {
            uint32_t last = tx->payload_size > FWL_CHUNK ? tx->payload_size - FWL_CHUNK : 0;
            n = data_frame(tx, last, out, cap);
        }
        break;
    }
    if (n) {
        tx->last_tx = now;
        tx->answered = false;
    }
    return n;
}

unsigned fwtx_permille(const fwtx_t *tx)
{
    if (tx->state == FWTX_DONE || tx->state == FWTX_INSTALLING) return 1000;
    if (tx->payload_size == 0 || tx->state != FWTX_SENDING) return 0;
    return (unsigned)((uint64_t)tx->next * 1000u / tx->payload_size);
}
