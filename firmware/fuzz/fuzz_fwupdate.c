/* libFuzzer harness: everything in a firmware update that sees bytes from
 * outside before the signature has been checked.
 *
 * The first input byte picks the target, the rest is its input:
 *   0  the LZMA decoder (properties, size, stream)
 *   1  an image in the staging and application slots: header parsing,
 *      verification (the decompressing pass included) and the copy
 *   2  the IR receiver: the bytes arrive over IR in pieces; a complete
 *      image is verified and installed as the bootloader does
 *   3  the bootloader journal pages, then a boot
 *   4  the sender, hearing the bytes as the receiver's replies
 * Checked: memory safety (ASan/UBSan), the record store never written, the
 * receiver's and sender's states in range, and nothing installed that is
 * not signed with the test key.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../core/fwboot.h"
#include "../core/fwlink.h"
#include "../core/lzma_dec.h"
#include "../tests/data/fw_fixtures.h"
#include "../tests/vflash.h"

#define ASSERT(c) do { if (!(c)) abort(); } while (0)

static const fw_layout_t LAY = {0x6800, 0x18000, 0x1E800, 0xC800, 0x2B000};
static vflash_t F;
static fw_flash_t FL;
static fw_work_t W;
static lzma_dec_t D;

typedef struct {
    const uint8_t *p;
    size_t n, at;
} in_t;

static size_t in_fill(void *ctx, uint8_t *buf, size_t cap)
{
    in_t *in = ctx;
    size_t k = in->n - in->at < cap ? in->n - in->at : cap;
    memcpy(buf, in->p + in->at, k);
    in->at += k;
    return k;
}

static void put(uint32_t addr, const uint8_t *d, size_t n, uint32_t cap)
{
    if (n > cap) n = cap;
    memcpy(F.mem + addr, d, n);
}

static fw_policy_t policy(void)
{
    fw_policy_t p = {0x00830001u, 1, 0, FIX_TEST_PUB, true};
    return p;
}

static void lzma(const uint8_t *d, size_t n)
{
    if (n < 4) return;
    in_t in = {d + 4, n - 4, 0};
    uint32_t size = (uint32_t)d[2] | ((uint32_t)d[3] << 8);
    static uint8_t out[70000];
    if (!lzma_dec_init(&D, d[0], 4096u << (d[1] & 1), size, in_fill, &in)) return;
    size_t got = 0, k;
    while ((k = lzma_dec_read(&D, out + got, 1 + (d[1] >> 1) * 7u)) > 0) got += k;
    ASSERT(got <= size);
}

static void image(const uint8_t *d, size_t n)
{
    put(LAY.staging_addr, d, n, LAY.staging_size);
    put(LAY.app_addr, d, n < FW_HDR_SIZE ? n : FW_HDR_SIZE, FW_HDR_SIZE);
    if (n > FW_HDR_SIZE) put(LAY.app_addr + FW_APP_OFFSET, d + FW_HDR_SIZE, n - FW_HDR_SIZE, 0x8000);
    fw_policy_t pol = policy();
    fw_header_t h;
    fw_err_t s = fw_verify_staging(&W, &FL, &LAY, &pol, &h);
    fw_verify_app(&W, &FL, &LAY, &pol, &h);
    pol.allow_unsigned = false;
    fw_verify_app(&W, &FL, &LAY, &pol, &h);
    fw_install(&W, &FL, &LAY);
    if (s == FW_OK) {
        /* Only the test key's images verify; installing one gives that image. */
        ASSERT(fw_verify_app(&W, &FL, &LAY, &pol, &h) == FW_OK);
    }
}

static void receive(const uint8_t *d, size_t n)
{
    static fwrx_t rx;
    static fwboot_t fb;
    memset(&fb, 0, sizeof(fb));
    fb.fl = &FL;
    fb.lay = &LAY;
    fb.pol = policy();
    fb.pol.allow_unsigned = false;
    fb.work = &W;
    fwrx_init(&rx, &FL, &LAY, &fb.pol);
    uint8_t f[FWL_FRAME_MAX];
    size_t i = 0, step = 1;
    while (i < n) {
        size_t k = n - i < step ? n - i : step;
        fwrx_rx(&rx, d + i, k);
        i += k;
        step = step * 7 % 61 + 1;
        ASSERT(rx.state <= FWRX_FAILED && rx.next <= rx.payload_size);
        if (rx.reply) ASSERT(fwrx_status(&rx, f, sizeof(f)) == 4 + FWL_STATUS_LEN + 2);
        if (rx.state == FWRX_COMPLETE) {
            fw_header_t h;
            fw_err_t e = fwboot_install_staged(&fb, &h);
            fwrx_finish(&rx, e);
            if (e == FW_OK) {
                fwboot_report_t rep;
                ASSERT(fwboot_reset(&fb, false, &rep) == FWBOOT_RUN_APP);
            }
        }
    }
}

static void journal(const uint8_t *d, size_t n)
{
    put(LAY.log_addr, d, n, 2 * VF_PAGE);
    if (n > 2 * VF_PAGE) put(LAY.staging_addr, d + 2 * VF_PAGE, n - 2 * VF_PAGE, LAY.staging_size);
    fwlog_t log;
    fwlog_load(&log, &FL, &LAY);
    uint32_t floor = log.floor;
    if (floor < UINT32_MAX) {
        /* Whatever the pages held, the journal still takes a record and the
         * floor never goes down. */
        ASSERT(fwlog_append(&log, &FL, &LAY, FWLOG_DONE, floor + 1));
        fwlog_load(&log, &FL, &LAY);
        ASSERT(log.floor == floor + 1);
    }
    fwboot_t fb;
    memset(&fb, 0, sizeof(fb));
    fb.fl = &FL;
    fb.lay = &LAY;
    fb.pol = policy();
    fb.work = &W;
    fwboot_report_t rep;
    fwboot_reset(&fb, n & 1, &rep);
}

static bool fix_read(void *ctx, uint32_t off, uint8_t *buf, size_t n)
{
    (void)ctx;
    if (FW_HDR_SIZE + off + n > sizeof(FIX_SAMPLE_IMAGE)) return false;
    memcpy(buf, FIX_SAMPLE_IMAGE + FW_HDR_SIZE + off, n);
    return true;
}

static void send(const uint8_t *d, size_t n)
{
    static fwtx_t tx;
    uint32_t now = 0;
    fwtx_start(&tx, FIX_SAMPLE_IMAGE, fix_read, NULL, now);
    uint8_t f[FWL_FRAME_MAX];
    for (size_t i = 0; i < n; i += 9) {
        size_t k = n - i < 9 ? n - i : 9;
        fwtx_rx(&tx, d + i, k, now);
        now += d[i] * 50u;
        size_t m = fwtx_poll(&tx, now, f, sizeof(f));
        ASSERT(m == 0 || m <= sizeof(f));
        ASSERT(tx.state <= FWTX_FAILED && fwtx_permille(&tx) <= 1000);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) return 0;
    vflash_init(&F);
    FL = vflash_iface(&F);
    F.prot_lo = 0x2C000;
    F.prot_hi = 0x40000;
    const uint8_t *d = data + 1;
    size_t n = size - 1;
    switch (data[0] % 5) {
    case 0: lzma(d, n); break;
    case 1: image(d, n); break;
    case 2: receive(d, n); break;
    case 3: journal(d, n); break;
    default: send(d, n); break;
    }
    ASSERT(F.violations == 0);
    return 0;
}
