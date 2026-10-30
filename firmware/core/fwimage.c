/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "fwimage.h"

#include <string.h>

#include "third_party/monocypher/monocypher.h"

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

fw_err_t fw_header_parse(const uint8_t raw[FW_HDR_SIZE], fw_header_t *h)
{
    memset(h, 0, sizeof(*h));
    if (get32(raw) != FW_MAGIC) return FW_E_FORMAT;
    h->hdr_version = (uint16_t)(raw[4] | (raw[5] << 8));
    h->flags = (uint16_t)(raw[6] | (raw[7] << 8));
    h->version = get32(raw + 8);
    h->hw_id = get32(raw + 12);
    h->min_boot = get32(raw + 16);
    h->image_size = get32(raw + 20);
    h->payload_size = get32(raw + 24);
    h->comp = raw[28];
    h->props = raw[29];
    h->dict_log2 = raw[30];
    memcpy(h->sig, raw + FW_HDR_SIGNED, 64);
    if (h->hdr_version != FW_HDR_VERSION || (h->flags & ~FW_FLAG_UNSIGNED) != 0) return FW_E_FORMAT;
    for (unsigned i = 31; i < FW_HDR_SIGNED; i++)
        if (raw[i] != 0) return FW_E_FORMAT;
    if (h->image_size < 8 || h->payload_size == 0) return FW_E_FORMAT;
    if (h->comp == FW_COMP_STORED) {
        if (h->payload_size != h->image_size || h->props != 0 || h->dict_log2 != 0) return FW_E_FORMAT;
    } else if (h->comp == FW_COMP_LZMA) {
        /* lzma_dec_init() checks the rest. */
        if (h->dict_log2 < 12 || h->dict_log2 > 13) return FW_E_FORMAT;
    } else {
        return FW_E_FORMAT;
    }
    return FW_OK;
}

void fw_header_write(const fw_header_t *h, uint8_t raw[FW_HDR_SIZE])
{
    memset(raw, 0, FW_HDR_SIZE);
    put32(raw, FW_MAGIC);
    raw[4] = (uint8_t)h->hdr_version;
    raw[5] = (uint8_t)(h->hdr_version >> 8);
    raw[6] = (uint8_t)h->flags;
    raw[7] = (uint8_t)(h->flags >> 8);
    put32(raw + 8, h->version);
    put32(raw + 12, h->hw_id);
    put32(raw + 16, h->min_boot);
    put32(raw + 20, h->image_size);
    put32(raw + 24, h->payload_size);
    raw[28] = h->comp;
    raw[29] = h->props;
    raw[30] = h->dict_log2;
    memcpy(raw + FW_HDR_SIGNED, h->sig, 64);
}

fw_err_t fw_header_check(const fw_header_t *h, const fw_policy_t *pol, const fw_layout_t *lay)
{
    if (h->hw_id != pol->hw_id) return FW_E_HW;
    if (h->min_boot > pol->boot_version) return FW_E_BOOT;
    if (h->image_size > lay->app_size - FW_APP_OFFSET ||
        h->payload_size > lay->staging_size - FW_HDR_SIZE)
        return FW_E_SIZE;
    if (!(h->flags & FW_FLAG_UNSIGNED) && h->version < pol->floor) return FW_E_ROLLBACK;
    return FW_OK;
}

int fw_ed25519_check(const uint8_t sig[64], const uint8_t pub[32], const uint8_t *msg, size_t len)
{
    /* RFC 8032 5.1.7, as Monocypher's optional crypto_ed25519_check:
     * k = SHA-512(R || A || M) mod L, then [S]B = R + [k]A. */
    hash_ctx_t h;
    uint8_t hram[64], k[32];
    hash_init(&h, HASH_SHA512);
    hash_update(&h, sig, 32);
    hash_update(&h, pub, 32);
    hash_update(&h, msg, len);
    hash_final(&h, hram);
    crypto_eddsa_reduce(k, hram);
    return crypto_eddsa_check_equation(sig, pub, k);
}

/* ---- the binary as a byte stream out of the staging slot ---- */

static size_t in_fill(void *ctx, uint8_t *buf, size_t cap)
{
    fw_work_t *w = ctx;
    size_t n = cap < w->in_left ? cap : w->in_left;
    if (n == 0 || w->in_error) return 0;
    if (!w->fl->read(w->fl->ctx, w->in_addr, buf, n)) {
        w->in_error = true;
        return 0;
    }
    w->in_addr += (uint32_t)n;
    w->in_left -= (uint32_t)n;
    return n;
}

static bool stream_open(fw_work_t *w, const fw_flash_t *fl, const fw_layout_t *lay,
                        const fw_header_t *h)
{
    w->fl = fl;
    w->in_addr = lay->staging_addr + FW_HDR_SIZE;
    w->in_left = h->payload_size;
    w->in_error = false;
    w->lz = h->comp == FW_COMP_LZMA;
    if (!w->lz) return true;
    return lzma_dec_init(&w->dec, h->props, 1u << h->dict_log2, h->image_size, in_fill, w);
}

/* Reads exactly `n` bytes of the binary, or fails. */
static fw_err_t stream_read(fw_work_t *w, uint8_t *out, size_t n)
{
    size_t got = w->lz ? lzma_dec_read(&w->dec, out, n) : in_fill(w, out, n);
    if (got == n) return FW_OK;
    return w->in_error ? FW_E_FLASH : FW_E_DATA;
}

static fw_err_t read_header(const fw_flash_t *fl, uint32_t addr, uint8_t raw[FW_HDR_SIZE],
                            fw_header_t *h, const fw_policy_t *pol, const fw_layout_t *lay)
{
    if (!fl->read(fl->ctx, addr, raw, FW_HDR_SIZE)) return FW_E_FLASH;
    fw_err_t e = fw_header_parse(raw, h);
    if (e == FW_OK) e = fw_header_check(h, pol, lay);
    return e;
}

static fw_err_t check_digest(fw_work_t *w, const fw_header_t *h, const fw_policy_t *pol,
                             bool unsigned_ok)
{
    uint8_t digest[64];
    hash_final(&w->hash, digest);
    if (h->flags & FW_FLAG_UNSIGNED) {
        if (!unsigned_ok) return FW_E_UNSIGNED;
        return memcmp(digest, h->sig, 64) == 0 ? FW_OK : FW_E_SIGNATURE;
    }
    if (pol->pubkey == NULL) return FW_E_NOKEY;
    return fw_ed25519_check(h->sig, pol->pubkey, digest, sizeof(digest)) == 0 ? FW_OK
                                                                            : FW_E_SIGNATURE;
}

fw_err_t fw_verify_staging(fw_work_t *w, const fw_flash_t *fl, const fw_layout_t *lay,
                           const fw_policy_t *pol, fw_header_t *out)
{
    uint8_t raw[FW_HDR_SIZE];
    fw_err_t e = read_header(fl, lay->staging_addr, raw, out, pol, lay);
    if (e != FW_OK) return e;
    if (out->flags & FW_FLAG_UNSIGNED) return FW_E_UNSIGNED;
    if (pol->pubkey == NULL) return FW_E_NOKEY;
    if (!stream_open(w, fl, lay, out)) return FW_E_DATA;
    hash_init(&w->hash, HASH_SHA512);
    hash_update(&w->hash, raw, FW_HDR_SIGNED);
    for (uint32_t done = 0; done < out->image_size;) {
        uint32_t n = out->image_size - done;
        if (n > sizeof(w->buf)) n = sizeof(w->buf);
        e = stream_read(w, w->buf, n);
        if (e != FW_OK) return e;
        hash_update(&w->hash, w->buf, n);
        done += n;
    }
    return check_digest(w, out, pol, false);
}

fw_err_t fw_verify_app(fw_work_t *w, const fw_flash_t *fl, const fw_layout_t *lay,
                       const fw_policy_t *pol, fw_header_t *out)
{
    uint8_t raw[FW_HDR_SIZE];
    fw_err_t e = read_header(fl, lay->app_addr, raw, out, pol, lay);
    if (e != FW_OK) return e;
    if ((out->flags & FW_FLAG_UNSIGNED) && !pol->allow_unsigned) return FW_E_UNSIGNED;
    hash_init(&w->hash, HASH_SHA512);
    hash_update(&w->hash, raw, FW_HDR_SIGNED);
    for (uint32_t done = 0; done < out->image_size;) {
        uint32_t n = out->image_size - done;
        if (n > sizeof(w->buf)) n = sizeof(w->buf);
        if (!fl->read(fl->ctx, lay->app_addr + FW_APP_OFFSET + done, w->buf, n)) return FW_E_FLASH;
        hash_update(&w->hash, w->buf, n);
        done += n;
    }
    return check_digest(w, out, pol, pol->allow_unsigned);
}

/* Writes one application page unless it already holds `want`. */
static fw_err_t put_page(fw_work_t *w, const fw_flash_t *fl, uint32_t addr, const uint8_t *want)
{
    uint32_t ps = fl->page_size;
    uint8_t *have = w->buf;
    if (fl->read(fl->ctx, addr, have, ps) && memcmp(have, want, ps) == 0) return FW_OK;
    if (!fl->erase(fl->ctx, addr)) return FW_E_FLASH;
    /* Erased double-words are left alone: programming 0xFF is wasted wear
     * and the tail of the last page stays erased. */
    for (uint32_t i = 0; i < ps; i += 8) {
        static const uint8_t ERASED[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        if (memcmp(want + i, ERASED, 8) == 0) continue;
        if (!fl->program(fl->ctx, addr + i, want + i, 8)) return FW_E_FLASH;
    }
    if (!fl->read(fl->ctx, addr, have, ps) || memcmp(have, want, ps) != 0) return FW_E_FLASH;
    return FW_OK;
}

fw_err_t fw_install(fw_work_t *w, const fw_flash_t *fl, const fw_layout_t *lay)
{
    uint8_t raw[FW_HDR_SIZE];
    fw_header_t h;
    if (!fl->read(fl->ctx, lay->staging_addr, raw, FW_HDR_SIZE)) return FW_E_FLASH;
    if (fw_header_parse(raw, &h) != FW_OK) return FW_E_FORMAT;
    uint32_t ps = fl->page_size;
    if (ps > FW_PAGE_MAX || ps < FW_APP_OFFSET || h.image_size > lay->app_size - FW_APP_OFFSET)
        return FW_E_SIZE;
    if (!stream_open(w, fl, lay, &h)) return FW_E_DATA;

    uint32_t total = FW_APP_OFFSET + h.image_size;
    uint32_t pages = (total + ps - 1u) / ps;
    /* Page 0: the header, padding, then the start of the binary. It is
     * written after every other page, so until the copy is complete the
     * slot does not even claim to hold the new image. */
    memset(w->page0, 0xff, ps);
    memcpy(w->page0, raw, FW_HDR_SIZE);
    uint32_t first = ps - FW_APP_OFFSET;
    if (first > h.image_size) first = h.image_size;
    fw_err_t e = stream_read(w, w->page0 + FW_APP_OFFSET, first);
    uint32_t done = first;
    for (uint32_t p = 1; e == FW_OK && p < pages; p++) {
        uint32_t n = h.image_size - done;
        if (n > ps) n = ps;
        memset(w->page, 0xff, ps);
        e = stream_read(w, w->page, n);
        if (e == FW_OK) e = put_page(w, fl, lay->app_addr + p * ps, w->page);
        done += n;
    }
    if (e == FW_OK) e = put_page(w, fl, lay->app_addr, w->page0);
    return e;
}
