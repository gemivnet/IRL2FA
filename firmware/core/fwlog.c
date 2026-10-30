/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "fwlog.h"

#include <string.h>

#include "pairing.h" /* pairing_crc16 */

#define REC 8u
#define MARK 0xA5u

static void encode(uint8_t r[REC], uint8_t type, uint32_t value)
{
    r[0] = type;
    r[1] = (uint8_t)value;
    r[2] = (uint8_t)(value >> 8);
    r[3] = (uint8_t)(value >> 16);
    r[4] = (uint8_t)(value >> 24);
    r[5] = MARK;
    uint16_t crc = pairing_crc16(r, 6);
    r[6] = (uint8_t)(crc >> 8);
    r[7] = (uint8_t)crc;
}

/* 0: erased, -1: written but not a valid record, else the type. */
static int decode(const uint8_t r[REC], bool read_ok, uint32_t *value)
{
    static const uint8_t ERASED[REC] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    if (read_ok && memcmp(r, ERASED, REC) == 0) return 0;
    if (!read_ok || r[5] != MARK || r[0] < FWLOG_SEQ || r[0] > FWLOG_CANCEL) return -1;
    if (pairing_crc16(r, 6) != (uint16_t)((r[6] << 8) | r[7])) return -1;
    *value = (uint32_t)r[1] | ((uint32_t)r[2] << 8) | ((uint32_t)r[3] << 16) | ((uint32_t)r[4] << 24);
    return r[0];
}

static uint32_t page_addr(const fw_flash_t *fl, const fw_layout_t *lay, int page)
{
    return lay->log_addr + (uint32_t)page * fl->page_size;
}

static void apply(fwlog_t *log, int type, uint32_t v)
{
    switch (type) {
    case FWLOG_FLOOR:
        if (v > log->floor) log->floor = v;
        break;
    case FWLOG_INSTALL:
        log->pending = true;
        log->pending_version = v;
        break;
    case FWLOG_DONE:
        if (v > log->floor) log->floor = v;
        if (log->pending && log->pending_version == v) log->pending = false;
        break;
    case FWLOG_CANCEL:
        if (log->pending && log->pending_version == v) log->pending = false;
        break;
    default:
        break;
    }
}

void fwlog_load(fwlog_t *log, const fw_flash_t *fl, const fw_layout_t *lay)
{
    memset(log, 0, sizeof(*log));
    log->page = -1;
    for (int p = 0; p < 2; p++) {
        uint8_t r[REC];
        uint32_t seq = 0;
        bool ok = fl->read(fl->ctx, page_addr(fl, lay, p), r, REC);
        if (decode(r, ok, &seq) == FWLOG_SEQ && (log->page < 0 || seq > log->seq)) {
            log->page = p;
            log->seq = seq;
        }
    }
    if (log->page < 0) return;
    uint32_t base = page_addr(fl, lay, log->page);
    log->next = REC;
    for (uint32_t off = REC; off < fl->page_size; off += REC) {
        uint8_t r[REC];
        uint32_t v = 0;
        bool ok = fl->read(fl->ctx, base + off, r, REC);
        int t = decode(r, ok, &v);
        if (t == 0) continue;
        log->next = off + REC;
        if (t < 0 || t == FWLOG_SEQ) {
            log->skipped++;
            continue;
        }
        apply(log, t, v);
    }
}

static bool write_rec(const fw_flash_t *fl, uint32_t addr, uint8_t type, uint32_t value)
{
    uint8_t r[REC], back[REC];
    encode(r, type, value);
    return fl->program(fl->ctx, addr, r, REC) && fl->read(fl->ctx, addr, back, REC) &&
           memcmp(r, back, REC) == 0;
}

/* Starts the other page with the current state plus one new record; SEQ
 * goes in last, which is what makes the new page live. */
static bool new_page(fwlog_t *log, const fw_flash_t *fl, const fw_layout_t *lay, uint8_t type,
                     uint32_t value)
{
    int p = log->page < 0 ? 0 : 1 - log->page;
    uint32_t base = page_addr(fl, lay, p);
    if (!fl->erase(fl->ctx, base)) return false;
    uint32_t off = REC;
    bool ok = write_rec(fl, base + off, FWLOG_FLOOR, log->floor);
    off += REC;
    if (ok && log->pending) {
        ok = write_rec(fl, base + off, FWLOG_INSTALL, log->pending_version);
        off += REC;
    }
    ok = ok && write_rec(fl, base + off, type, value);
    off += REC;
    ok = ok && write_rec(fl, base, FWLOG_SEQ, log->seq + 1u);
    if (!ok) return false;
    log->page = p;
    log->seq++;
    log->next = off;
    apply(log, type, value);
    return true;
}

bool fwlog_append(fwlog_t *log, const fw_flash_t *fl, const fw_layout_t *lay, uint8_t type,
                  uint32_t value)
{
    if (log->page < 0 || log->next + REC > fl->page_size) return new_page(log, fl, lay, type, value);
    uint32_t addr = page_addr(fl, lay, log->page) + log->next;
    log->next += REC; /* even if it fails: that double-word is no longer erased */
    if (!write_rec(fl, addr, type, value)) return new_page(log, fl, lay, type, value);
    apply(log, type, value);
    return true;
}
