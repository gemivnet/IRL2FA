/* Firmware updates: Ed25519, the LZMA decoder, image parsing and checks,
 * the IR transfer, the journal, and power cuts at every flash operation of
 * a transfer, an install and a journal page change. The device-to-device
 * transfer through the application UI is in test_app_update_*.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include <stdlib.h>

#include "../core/fwboot.h"
#include "../core/fwimage.h"
#include "../core/fwlink.h"
#include "../core/fwlog.h"
#include "../core/hash.h"
#include "../core/lzma_dec.h"
#include "../core/third_party/monocypher/monocypher.h"
#include "data/fw_fixtures.h"
#include "test.h"
#include "test_fw.h"
#include "vflash.h"

const fw_layout_t TFW_LAYOUT = {0x6800, 0x18000, 0x1E800, 0xC800, 0x2B000};
#define STORE_LO 0x2C000u
#define STORE_HI 0x40000u
#define BOOT_VERSION 1u

/* RFC 8032 7.1 TEST 1: the test key (fw_fixtures.h is signed with it). */
const uint8_t TFW_TEST_SEED[32] = {
    0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a, 0xf4, 0x92, 0xec, 0x2c, 0xc4,
    0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32, 0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60};
static const uint8_t OTHER_SEED[32] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                                       7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};

/* ---- helpers shared with test_app.c (test_fw.h) ---- */

void tfw_public(uint8_t pub[32], const uint8_t seed[32])
{
    uint8_t az[64], a[32];
    hash(HASH_SHA512, seed, 32, az);
    crypto_eddsa_trim_scalar(a, az);
    crypto_eddsa_scalarbase(pub, a);
}

/* Ed25519 signing (RFC 8032 5.1.6) from Monocypher's primitives and our
 * SHA-512, as Monocypher's optional crypto_ed25519_sign does. */
void tfw_sign(uint8_t sig[64], const uint8_t seed[32], const uint8_t *msg, size_t len)
{
    uint8_t az[64], a[32], pub[32], rh[64], r[32], kh[64], k[32];
    hash(HASH_SHA512, seed, 32, az);
    crypto_eddsa_trim_scalar(a, az);
    crypto_eddsa_scalarbase(pub, a);
    hash_ctx_t h;
    hash_init(&h, HASH_SHA512);
    hash_update(&h, az + 32, 32);
    hash_update(&h, msg, len);
    hash_final(&h, rh);
    crypto_eddsa_reduce(r, rh);
    crypto_eddsa_scalarbase(sig, r);
    hash_init(&h, HASH_SHA512);
    hash_update(&h, sig, 32);
    hash_update(&h, pub, 32);
    hash_update(&h, msg, len);
    hash_final(&h, kh);
    crypto_eddsa_reduce(k, kh);
    crypto_eddsa_mul_add(sig + 32, k, a, r);
}

size_t tfw_sample_plain(uint8_t *out, size_t n)
{
    uint32_t s = 0x12345678u;
    size_t len = 0;
    while (len < n) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        if (s % 3 == 0 && len > 64) {
            uint32_t lim = len < 4000 ? (uint32_t)len : 4000u;
            uint32_t d = 1 + (s >> 8) % lim, l = 3 + (s >> 20) % 40;
            for (uint32_t k = 0; k < l && len < n; k++, len++) out[len] = out[len - d];
        } else {
            out[len++] = (uint8_t)(((s >> 13) & 0x3F) | 0x20);
        }
    }
    return len;
}

size_t tfw_build(uint8_t *out, const uint8_t *bin, uint32_t n, const uint8_t *payload,
                 uint32_t plen, uint8_t comp, uint32_t version, const uint8_t *seed, uint32_t hw,
                 uint32_t min_boot)
{
    fw_header_t h;
    memset(&h, 0, sizeof(h));
    h.hdr_version = FW_HDR_VERSION;
    h.flags = seed ? 0 : FW_FLAG_UNSIGNED;
    h.version = version;
    h.hw_id = hw;
    h.min_boot = min_boot;
    h.image_size = n;
    h.payload_size = plen;
    h.comp = comp;
    if (comp == FW_COMP_LZMA) {
        h.props = (1 * 5 + 1) * 9 + 0;
        h.dict_log2 = 13;
    }
    fw_header_write(&h, out);
    uint8_t digest[64];
    hash_ctx_t c;
    hash_init(&c, HASH_SHA512);
    hash_update(&c, out, FW_HDR_SIGNED);
    hash_update(&c, bin, n);
    hash_final(&c, digest);
    if (seed) tfw_sign(out + FW_HDR_SIGNED, seed, digest, 64);
    else memcpy(out + FW_HDR_SIGNED, digest, 64);
    memcpy(out + FW_HDR_SIZE, payload, plen);
    return FW_HDR_SIZE + plen;
}

/* Two applications: A is the fixture (LZMA), B a stored 30000-byte one. */
static uint8_t BIN_A[FIX_SAMPLE_SIZE];
static uint8_t BIN_B[30000];
static uint8_t TEST_PUB[32], OTHER_PUB[32];

static void fixtures(void)
{
    tfw_sample_plain(BIN_A, sizeof(BIN_A));
    uint32_t x = 99;
    for (size_t i = 0; i < sizeof(BIN_B); i++) {
        x = x * 1103515245u + 12345u;
        BIN_B[i] = (uint8_t)(i % 7 == 0 ? x >> 24 : i);
    }
    tfw_public(TEST_PUB, TFW_TEST_SEED);
    tfw_public(OTHER_PUB, OTHER_SEED);
}

static size_t image_a(uint8_t *out, uint32_t version, const uint8_t *seed)
{
    return tfw_build(out, BIN_A, sizeof(BIN_A), FIX_SAMPLE_IMAGE + FW_HDR_SIZE,
                     sizeof(FIX_SAMPLE_IMAGE) - FW_HDR_SIZE, FW_COMP_LZMA, version, seed, TFW_HW_ID,
                     1);
}

static size_t image_b(uint8_t *out, uint32_t version, const uint8_t *seed)
{
    return tfw_build(out, BIN_B, sizeof(BIN_B), BIN_B, sizeof(BIN_B), FW_COMP_STORED, version,
                     seed, TFW_HW_ID, 1);
}

static fw_policy_t policy(void)
{
    fw_policy_t p = {TFW_HW_ID, BOOT_VERSION, 0, TEST_PUB, false};
    return p;
}

/* A device: flash with random bytes in the record store (which nothing
 * here may touch), plus the bootloader's state. */
typedef struct {
    vflash_t f;
    fw_flash_t fl;
    fwboot_t boot;
    uint8_t store[STORE_HI - STORE_LO];
} fwdev_t;

static fw_work_t WORK;

static void dev_init(fwdev_t *d)
{
    vflash_init(&d->f);
    d->fl = vflash_iface(&d->f);
    uint32_t x = 1;
    for (uint32_t a = STORE_LO; a < STORE_HI; a++) {
        x = x * 1664525u + 1013904223u;
        d->f.mem[a] = (uint8_t)(x >> 24);
    }
    memcpy(d->store, d->f.mem + STORE_LO, sizeof(d->store));
    d->f.prot_lo = STORE_LO;
    d->f.prot_hi = STORE_HI;
    memset(&d->boot, 0, sizeof(d->boot));
    d->boot.fl = &d->fl;
    d->boot.lay = &TFW_LAYOUT;
    d->boot.pol = policy();
    d->boot.work = &WORK;
}

static bool store_intact(const fwdev_t *d)
{
    return d->f.violations == 0 && memcmp(d->store, d->f.mem + STORE_LO, sizeof(d->store)) == 0;
}

static void stage(fwdev_t *d, const uint8_t *img, size_t len)
{
    for (uint32_t a = 0; a < TFW_LAYOUT.staging_size; a += VF_PAGE)
        d->fl.erase(d->fl.ctx, TFW_LAYOUT.staging_addr + a);
    size_t n8 = (len + 7) & ~(size_t)7;
    static uint8_t tmp[0xE000];
    memset(tmp, 0xff, sizeof(tmp));
    memcpy(tmp, img, len);
    d->fl.program(d->fl.ctx, TFW_LAYOUT.staging_addr, tmp, n8);
}

/* Boots; returns the running version, or 0 if it waits for an update. */
static uint32_t boot(fwdev_t *d)
{
    fwboot_report_t rep;
    if (fwboot_reset(&d->boot, false, &rep) != FWBOOT_RUN_APP) return 0;
    return rep.app_hdr.version;
}

/* ---- the IR link between a sender and a receiver ---- */

typedef struct {
    const uint8_t *img;
    size_t len;
} src_t;

static bool src_read(void *ctx, uint32_t off, uint8_t *buf, size_t n)
{
    const src_t *s = ctx;
    if (FW_HDR_SIZE + off + n > s->len) return false;
    memcpy(buf, s->img + FW_HDR_SIZE + off, n);
    return true;
}

static uint32_t lcg = 12345;
static unsigned rnd(unsigned n)
{
    lcg = lcg * 1103515245u + 12345u;
    return (lcg >> 16) % n;
}

/* Runs both ends for up to `ms`, losing `loss` % of frames and corrupting
 * a byte in `noise` % of them. Stops when the receiver has everything
 * (and the bootloader would take over) or the sender gives up. */
static void run_link(fwtx_t *tx, fwrx_t *rx, uint32_t *now, uint32_t ms, unsigned loss,
                     unsigned noise)
{
    /* Time moves with each frame's airtime (10 bits a byte at 4800 baud). */
    uint8_t f[FWL_FRAME_MAX];
    uint32_t end = *now + ms;
    while ((int32_t)(end - *now) > 0) {
        size_t n = fwtx_poll(tx, *now, f, sizeof(f));
        if (n) {
            *now += (uint32_t)(n * 10000u / 4800u) + 1u;
            fwtx_rx(tx, f, n, *now); /* its own echo: ignored */
            if (rnd(100) < noise) f[rnd((unsigned)n)] ^= (uint8_t)(1u << rnd(8));
            if (rnd(100) >= loss) fwrx_rx(rx, f, n);
        }
        if (rx->reply) {
            size_t m = fwrx_status(rx, f, sizeof(f));
            *now += (uint32_t)(m * 10000u / 4800u) + 1u;
            fwrx_rx(rx, f, m); /* its own echo: ignored */
            if (rnd(100) >= loss) fwtx_rx(tx, f, m, *now);
        }
        if (!n) *now += 5;
        if (tx->state == FWTX_DONE || tx->state == FWTX_FAILED) return;
        if (rx->state == FWRX_COMPLETE && tx->state == FWTX_INSTALLING) return;
    }
}

/* A whole update as the bootloader runs it: receive, install, report. */
static fw_err_t ir_update(fwdev_t *d, const uint8_t *img, size_t len, unsigned loss)
{
    fwlog_load(&d->boot.log, d->boot.fl, d->boot.lay);
    fw_policy_t pol = d->boot.pol;
    pol.floor = d->boot.log.floor;
    static fwrx_t rx;
    static fwtx_t tx;
    src_t src = {img, len};
    uint32_t now = 1000;
    fwrx_init(&rx, &d->fl, &TFW_LAYOUT, &pol);
    fwtx_start(&tx, img, src_read, &src, now);
    run_link(&tx, &rx, &now, 600000, loss, loss / 3);
    if (rx.state == FWRX_FAILED) return (fw_err_t)rx.err;
    if (rx.state != FWRX_COMPLETE) return FW_E_TIMEOUT;
    fw_header_t h;
    fw_err_t e = fwboot_install_staged(&d->boot, &h);
    fwrx_finish(&rx, e);
    run_link(&tx, &rx, &now, 10000, loss, 0);
    if (e == FW_OK) CHECK_EQ_INT(tx.state, FWTX_DONE);
    else CHECK_EQ_INT(tx.err, e);
    return e;
}

/* ------------------------------------------------------------------ */

TEST(test_fw_ed25519)
{
    static const struct {
        const char *seed, *pub, *msg, *sig;
    } V[] = {
        {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
         "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
         "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
        {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
         "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
         "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
        {"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
         "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
         "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"},
    };
    for (size_t i = 0; i < sizeof(V) / sizeof(V[0]); i++) {
        uint8_t seed[32], pub[32], msg[8], sig[64], mine[64];
        unhex(V[i].seed, seed);
        unhex(V[i].pub, pub);
        size_t n = unhex(V[i].msg, msg);
        unhex(V[i].sig, sig);
        uint8_t p2[32];
        tfw_public(p2, seed);
        CHECK(memcmp(p2, pub, 32) == 0);
        tfw_sign(mine, seed, msg, n);
        CHECK(memcmp(mine, sig, 64) == 0);
        CHECK_EQ_INT(fw_ed25519_check(sig, pub, msg, n), 0);
        sig[5] ^= 1;
        CHECK(fw_ed25519_check(sig, pub, msg, n) != 0);
        sig[5] ^= 1;
        sig[40] ^= 0x80;
        CHECK(fw_ed25519_check(sig, pub, msg, n) != 0);
        sig[40] ^= 0x80;
        sig[63] |= 0xF0; /* S >= L: non-canonical, rejected */
        CHECK(fw_ed25519_check(sig, pub, msg, n) != 0);
        unhex(V[i].sig, sig);
        pub[0] ^= 2;
        CHECK(fw_ed25519_check(sig, pub, msg, n) != 0);
        msg[0] ^= 1;
        CHECK(fw_ed25519_check(sig, p2, msg, n ? n : 1) != 0);
    }
}

typedef struct {
    const uint8_t *p;
    size_t len, pos;
} mem_in_t;

static size_t mem_in(void *ctx, uint8_t *buf, size_t cap)
{
    mem_in_t *m = ctx;
    size_t n = m->len - m->pos < cap ? m->len - m->pos : cap;
    memcpy(buf, m->p + m->pos, n);
    m->pos += n;
    return n;
}

static lzma_dec_t DEC;

TEST(test_fw_lzma)
{
    fixtures();
    const uint8_t *pl = FIX_SAMPLE_IMAGE + FW_HDR_SIZE;
    size_t plen = sizeof(FIX_SAMPLE_IMAGE) - FW_HDR_SIZE;
    static uint8_t out[FIX_SAMPLE_SIZE + 64];
    static const size_t STEPS[] = {1, 7, 333, 4096, FIX_SAMPLE_SIZE + 64};
    for (size_t s = 0; s < sizeof(STEPS) / sizeof(STEPS[0]); s++) {
        mem_in_t m = {pl, plen, 0};
        CHECK(lzma_dec_init(&DEC, 54, 8192, FIX_SAMPLE_SIZE, mem_in, &m));
        size_t got = 0, k;
        while ((k = lzma_dec_read(&DEC, out + got, STEPS[s])) > 0) got += k;
        CHECK_EQ_INT(got, FIX_SAMPLE_SIZE);
        CHECK(!DEC.error);
        CHECK(memcmp(out, BIN_A, FIX_SAMPLE_SIZE) == 0);
    }
    /* Truncated input: stops with an error, short. */
    mem_in_t m = {pl, plen / 2, 0};
    CHECK(lzma_dec_init(&DEC, 54, 8192, FIX_SAMPLE_SIZE, mem_in, &m));
    CHECK(lzma_dec_read(&DEC, out, sizeof(out)) < FIX_SAMPLE_SIZE);
    CHECK(DEC.error);
    /* Bad properties, dictionary sizes, first byte. */
    CHECK(!lzma_dec_init(&DEC, 225, 8192, 10, mem_in, &m));
    CHECK(!lzma_dec_init(&DEC, (0 * 5 + 2) * 9 + 1, 8192, 10, mem_in, &m)); /* lc + lp = 3 */
    CHECK(!lzma_dec_init(&DEC, 54, 2048, 10, mem_in, &m));
    CHECK(!lzma_dec_init(&DEC, 54, 16384, 10, mem_in, &m));
    CHECK(!lzma_dec_init(&DEC, 54, 6000, 10, mem_in, &m));
    static const uint8_t BAD0[] = {1, 0, 0, 0, 0, 0};
    mem_in_t b0 = {BAD0, sizeof(BAD0), 0};
    CHECK(!lzma_dec_init(&DEC, 54, 8192, 10, mem_in, &b0));
    static const uint8_t BADC[] = {0, 0xff, 0xff, 0xff, 0xff, 0};
    mem_in_t bc = {BADC, sizeof(BADC), 0};
    CHECK(!lzma_dec_init(&DEC, 54, 8192, 10, mem_in, &bc));
    /* Corrupted streams: never out of bounds (ASan), never the right data
     * with no error... except by luck, which the signature catches. */
    static uint8_t bad[4096];
    size_t blen = plen < sizeof(bad) ? plen : sizeof(bad);
    int errors = 0;
    for (int i = 0; i < 300; i++) {
        memcpy(bad, pl, blen);
        for (int j = 0; j < 1 + i % 4; j++) bad[1 + rnd((unsigned)blen - 1)] ^= (uint8_t)(1 + rnd(255));
        mem_in_t c = {bad, blen, 0};
        if (lzma_dec_init(&DEC, (uint8_t)(i % 2 ? 54 : 0), 4096u << (i % 2), FIX_SAMPLE_SIZE, mem_in, &c)) {
            size_t got = 0, k;
            while ((k = lzma_dec_read(&DEC, out + got, 1000)) > 0) got += k;
            CHECK(got <= FIX_SAMPLE_SIZE);
        }
        errors += DEC.error;
    }
    CHECK(errors > 250);
    /* A zero-length output needs no input beyond the range coder start. */
    static const uint8_t EMPTY[] = {0, 0, 0, 0, 0};
    mem_in_t e = {EMPTY, sizeof(EMPTY), 0};
    CHECK(lzma_dec_init(&DEC, 54, 8192, 0, mem_in, &e));
    CHECK_EQ_INT(lzma_dec_read(&DEC, out, 10), 0);
}

TEST(test_fw_header)
{
    fixtures();
    fw_header_t h;
    CHECK_EQ_INT(fw_header_parse(FIX_SAMPLE_IMAGE, &h), FW_OK);
    CHECK_EQ_INT(h.version, FIX_SAMPLE_VERSION);
    CHECK_EQ_INT(h.hw_id, TFW_HW_ID);
    CHECK_EQ_INT(h.image_size, FIX_SAMPLE_SIZE);
    CHECK_EQ_INT(h.payload_size, sizeof(FIX_SAMPLE_IMAGE) - FW_HDR_SIZE);
    CHECK_EQ_INT(h.comp, FW_COMP_LZMA);
    CHECK_EQ_INT(h.props, 54);
    CHECK_EQ_INT(h.dict_log2, 13);
    uint8_t raw[FW_HDR_SIZE];
    fw_header_write(&h, raw);
    CHECK(memcmp(raw, FIX_SAMPLE_IMAGE, FW_HDR_SIZE) == 0);
    /* The C signer and sign_image.py agree (Ed25519 is deterministic). */
    static uint8_t img[0x8000];
    image_a(img, FIX_SAMPLE_VERSION, TFW_TEST_SEED);
    CHECK(memcmp(img, FIX_SAMPLE_IMAGE, FW_HDR_SIZE) == 0);
    CHECK(memcmp(FIX_TEST_PUB, TEST_PUB, 32) == 0);

    static const struct {
        unsigned off;
        uint8_t val;
    } BAD[] = {{0, 'X'}, {4, 2}, {6, 4}, {31, 1}, {40, 1}, {28, 3}, {30, 11}, {30, 14}};
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
        memcpy(raw, FIX_SAMPLE_IMAGE, FW_HDR_SIZE);
        raw[BAD[i].off] = BAD[i].val;
        CHECK_EQ_INT(fw_header_parse(raw, &h), FW_E_FORMAT);
    }
    memcpy(raw, FIX_SAMPLE_IMAGE, FW_HDR_SIZE);
    memset(raw + 20, 0, 4); /* image_size 0 */
    CHECK_EQ_INT(fw_header_parse(raw, &h), FW_E_FORMAT);
    image_b(img, 1, TFW_TEST_SEED);
    CHECK_EQ_INT(fw_header_parse(img, &h), FW_OK);
    img[24] ^= 1; /* stored: payload size must equal the binary's */
    CHECK_EQ_INT(fw_header_parse(img, &h), FW_E_FORMAT);
    img[24] ^= 1;
    img[29] = 1; /* stored with LZMA properties */
    CHECK_EQ_INT(fw_header_parse(img, &h), FW_E_FORMAT);
}

TEST(test_fw_verify_errors)
{
    fixtures();
    static fwdev_t d;
    static uint8_t img[0xE000];
    fw_header_t h;
    fw_policy_t pol = policy();
    dev_init(&d);

    /* The image sign_image.py made verifies. */
    stage(&d, FIX_SAMPLE_IMAGE, sizeof(FIX_SAMPLE_IMAGE));
    CHECK_EQ_INT(fw_verify_staging(&WORK, &d.fl, &TFW_LAYOUT, &pol, &h), FW_OK);

#define STAGED(expr, want)                                                         \
    do {                                                                           \
        size_t _n = (expr);                                                        \
        stage(&d, img, _n);                                                        \
        CHECK_EQ_INT(fw_verify_staging(&WORK, &d.fl, &TFW_LAYOUT, &pol, &h), want); \
    } while (0)

    STAGED(image_b(img, 5, TFW_TEST_SEED), FW_OK);
    STAGED(image_b(img, 5, OTHER_SEED), FW_E_SIGNATURE);
    STAGED(image_b(img, 5, NULL), FW_E_UNSIGNED);
    STAGED(tfw_build(img, BIN_B, sizeof(BIN_B), BIN_B, sizeof(BIN_B), FW_COMP_STORED, 5,
                     TFW_TEST_SEED, TFW_HW_ID + 1, 1), FW_E_HW);
    STAGED(tfw_build(img, BIN_B, sizeof(BIN_B), BIN_B, sizeof(BIN_B), FW_COMP_STORED, 5,
                     TFW_TEST_SEED, TFW_HW_ID, BOOT_VERSION + 1), FW_E_BOOT);
    /* A corrupted chunk anywhere in the payload. */
    for (int i = 0; i < 6; i++) {
        size_t n = image_a(img, 5, TFW_TEST_SEED);
        /* (Not in the last bytes: the end marker after the data is never read.) */
        img[FW_HDR_SIZE + 1 + rnd((unsigned)(n - FW_HDR_SIZE - 32))] ^= 0x10;
        stage(&d, img, n);
        fw_err_t e = fw_verify_staging(&WORK, &d.fl, &TFW_LAYOUT, &pol, &h);
        CHECK(e == FW_E_SIGNATURE || e == FW_E_DATA);
        n = image_b(img, 5, TFW_TEST_SEED);
        img[FW_HDR_SIZE + rnd((unsigned)(n - FW_HDR_SIZE))] ^= 0x01;
        STAGED(n, FW_E_SIGNATURE);
    }
    /* Truncated: the payload stops early (the rest of the slot is erased). */
    {
        size_t n = image_a(img, 5, TFW_TEST_SEED);
        stage(&d, img, n / 2);
        fw_err_t e = fw_verify_staging(&WORK, &d.fl, &TFW_LAYOUT, &pol, &h);
        CHECK(e == FW_E_SIGNATURE || e == FW_E_DATA);
    }
    /* A torn double-word in the payload. */
    image_b(img, 5, TFW_TEST_SEED);
    stage(&d, img, FW_HDR_SIZE + sizeof(BIN_B));
    vflash_tear(&d.f, TFW_LAYOUT.staging_addr + 4096);
    CHECK_EQ_INT(fw_verify_staging(&WORK, &d.fl, &TFW_LAYOUT, &pol, &h), FW_E_FLASH);
    vflash_tear(&d.f, TFW_LAYOUT.staging_addr);
    CHECK_EQ_INT(fw_verify_staging(&WORK, &d.fl, &TFW_LAYOUT, &pol, &h), FW_E_FLASH);
    /* Rollback, no key, too big. */
    pol.floor = 6;
    STAGED(image_b(img, 5, TFW_TEST_SEED), FW_E_ROLLBACK);
    STAGED(image_b(img, 6, TFW_TEST_SEED), FW_OK);
    pol.pubkey = NULL;
    STAGED(image_b(img, 6, TFW_TEST_SEED), FW_E_NOKEY);
    pol.pubkey = TEST_PUB;
    {
        fw_header_t big;
        size_t n = image_a(img, 6, TFW_TEST_SEED);
        fw_header_parse(img, &big);
        big.image_size = TFW_LAYOUT.app_size;
        fw_header_write(&big, img);
        stage(&d, img, n);
        CHECK_EQ_INT(fw_verify_staging(&WORK, &d.fl, &TFW_LAYOUT, &pol, &h), FW_E_SIZE);
    }
    /* The application slot: an unsigned development image boots only
     * where it is allowed (RDP level 0); a damaged one never. */
    dev_init(&d);
    size_t n = image_b(img, 3, NULL);
    d.fl.program(d.fl.ctx, TFW_LAYOUT.app_addr, img, FW_HDR_SIZE);
    for (uint32_t i = 0; i < sizeof(BIN_B); i += 8) {
        uint8_t dw[8];
        memset(dw, 0xff, 8);
        memcpy(dw, BIN_B + i, sizeof(BIN_B) - i < 8 ? sizeof(BIN_B) - i : 8);
        d.fl.program(d.fl.ctx, TFW_LAYOUT.app_addr + FW_APP_OFFSET + i, dw, 8);
    }
    (void)n;
    pol = policy();
    CHECK_EQ_INT(fw_verify_app(&WORK, &d.fl, &TFW_LAYOUT, &pol, &h), FW_E_UNSIGNED);
    pol.allow_unsigned = true;
    pol.floor = 99; /* the floor is for signed images */
    CHECK_EQ_INT(fw_verify_app(&WORK, &d.fl, &TFW_LAYOUT, &pol, &h), FW_OK);
    d.f.mem[TFW_LAYOUT.app_addr + FW_APP_OFFSET + 1000] ^= 4;
    CHECK_EQ_INT(fw_verify_app(&WORK, &d.fl, &TFW_LAYOUT, &pol, &h), FW_E_SIGNATURE);
    vflash_tear(&d.f, TFW_LAYOUT.app_addr + FW_APP_OFFSET + 2000);
    CHECK_EQ_INT(fw_verify_app(&WORK, &d.fl, &TFW_LAYOUT, &pol, &h), FW_E_FLASH);
    CHECK(store_intact(&d));
#undef STAGED
}

TEST(test_fw_install_and_update)
{
    fixtures();
    static fwdev_t d;
    static uint8_t img[0xE000], img2[0xE000];
    dev_init(&d);
    /* Factory state: a blank application slot, image A v1 in staging. The
     * first boot installs it. */
    size_t n = image_a(img, 0x10000, TFW_TEST_SEED);
    stage(&d, img, n);
    CHECK_EQ_INT(boot(&d), 0x10000);
    CHECK_EQ_INT(d.boot.log.floor, 0x10000);
    CHECK(memcmp(d.f.mem + TFW_LAYOUT.app_addr + FW_APP_OFFSET, BIN_A, sizeof(BIN_A)) == 0);
    CHECK(memcmp(d.f.mem + TFW_LAYOUT.app_addr, img, FW_HDR_SIZE) == 0);
    CHECK_EQ_INT(boot(&d), 0x10000); /* and again, with nothing to do */

    /* The application asked for the receiver. */
    fwboot_report_t rep;
    CHECK_EQ_INT(fwboot_reset(&d.boot, true, &rep), FWBOOT_WAIT_UPDATE);

    /* Over IR, 20 % of frames lost: B v2. */
    n = image_b(img, 0x10100, TFW_TEST_SEED);
    CHECK_EQ_INT(ir_update(&d, img, n, 20), FW_OK);
    CHECK_EQ_INT(boot(&d), 0x10100);
    CHECK_EQ_INT(d.boot.log.floor, 0x10100);
    CHECK(memcmp(d.f.mem + TFW_LAYOUT.app_addr + FW_APP_OFFSET, BIN_B, sizeof(BIN_B)) == 0);

    /* Refused at the offer: older, unsigned, other hardware, too new a
     * bootloader. Refused after the transfer: another key. The running
     * firmware is untouched each time. */
    n = image_a(img2, 0x10000, TFW_TEST_SEED);
    CHECK_EQ_INT(ir_update(&d, img2, n, 0), FW_E_ROLLBACK);
    n = image_a(img2, 0x20000, NULL);
    CHECK_EQ_INT(ir_update(&d, img2, n, 0), FW_E_UNSIGNED);
    n = tfw_build(img2, BIN_B, sizeof(BIN_B), BIN_B, sizeof(BIN_B), FW_COMP_STORED, 0x20000,
                  TFW_TEST_SEED, TFW_HW_ID ^ 0x100, 1);
    CHECK_EQ_INT(ir_update(&d, img2, n, 0), FW_E_HW);
    n = tfw_build(img2, BIN_B, sizeof(BIN_B), BIN_B, sizeof(BIN_B), FW_COMP_STORED, 0x20000,
                  TFW_TEST_SEED, TFW_HW_ID, 9);
    CHECK_EQ_INT(ir_update(&d, img2, n, 0), FW_E_BOOT);
    n = image_a(img2, 0x20000, OTHER_SEED);
    CHECK_EQ_INT(ir_update(&d, img2, n, 10), FW_E_SIGNATURE);
    CHECK_EQ_INT(boot(&d), 0x10100);
    /* The failed image was invalidated: the next offer starts afresh. */
    uint8_t first[8];
    d.fl.read(d.fl.ctx, TFW_LAYOUT.staging_addr, first, 8);
    CHECK_EQ_INT(first[0], 0xff);
    /* Same version again is allowed (a repair); then a newer one. */
    n = image_b(img, 0x10100, TFW_TEST_SEED);
    CHECK_EQ_INT(ir_update(&d, img, n, 0), FW_OK);
    n = image_a(img, 0x10200, TFW_TEST_SEED);
    CHECK_EQ_INT(ir_update(&d, img, n, 0), FW_OK);
    CHECK_EQ_INT(boot(&d), 0x10200);
    /* A damaged application slot is repaired from staging at boot. */
    d.f.mem[TFW_LAYOUT.app_addr + FW_APP_OFFSET + 5000] ^= 1;
    CHECK_EQ_INT(boot(&d), 0x10200);
    /* Both damaged: the receiver waits. */
    d.f.mem[TFW_LAYOUT.app_addr + FW_APP_OFFSET + 5000] ^= 1;
    d.f.mem[TFW_LAYOUT.staging_addr + FW_HDR_SIZE + 10] ^= 1;
    CHECK_EQ_INT(boot(&d), 0);
    /* A build without a key refuses every update. */
    d.boot.pol.pubkey = NULL;
    n = image_a(img, 0x10300, TFW_TEST_SEED);
    CHECK_EQ_INT(ir_update(&d, img, n, 0), FW_E_NOKEY);
    CHECK(store_intact(&d));
}

TEST(test_fw_link)
{
    fixtures();
    static fwdev_t d;
    static uint8_t img[0xE000];
    static fwrx_t rx;
    static fwtx_t tx;
    dev_init(&d);
    fw_policy_t pol = policy();
    size_t n = image_b(img, 7, TFW_TEST_SEED);
    src_t src = {img, n};
    uint32_t now = 0;

    /* Nobody answers: the sender gives up after FWTX_SILENCE_MS. */
    fwtx_start(&tx, img, src_read, &src, now);
    uint8_t f[FWL_FRAME_MAX];
    int offers = 0;
    for (uint32_t t = 0; t < 30000; t += 10) offers += fwtx_poll(&tx, now += 10, f, sizeof(f)) > 0;
    CHECK_EQ_INT(tx.state, FWTX_FAILED);
    CHECK_EQ_INT(tx.err, FW_E_TIMEOUT);
    CHECK(offers > 20 && offers < 40);

    /* A receiver that restarts twice mid-transfer (the bootloader timed out,
     * or the cell was swapped): the transfer resumes, chunks before the
     * restart are not sent again. */
    fwrx_init(&rx, &d.fl, &TFW_LAYOUT, &pol);
    fwtx_start(&tx, img, src_read, &src, now);
    run_link(&tx, &rx, &now, 20000, 0, 0);
    CHECK_EQ_INT(tx.state, FWTX_SENDING);
    uint32_t at = rx.next;
    CHECK(at > 4096);
    fwrx_init(&rx, &d.fl, &TFW_LAYOUT, &pol);
    run_link(&tx, &rx, &now, 1500, 0, 0);
    CHECK(rx.next >= at - 2048 && rx.next <= at + 1024);
    run_link(&tx, &rx, &now, 15000, 30, 10);
    fwrx_init(&rx, &d.fl, &TFW_LAYOUT, &pol);
    run_link(&tx, &rx, &now, 600000, 30, 10);
    CHECK_EQ_INT(rx.state, FWRX_COMPLETE);
    CHECK_EQ_INT(tx.state, FWTX_INSTALLING);
    CHECK_EQ_INT(fwtx_permille(&tx), 1000);
    CHECK(memcmp(d.f.mem + TFW_LAYOUT.staging_addr, img, n) == 0);
    /* Offered again once complete: nothing to fetch. */
    fwrx_init(&rx, &d.fl, &TFW_LAYOUT, &pol);
    fwtx_start(&tx, img, src_read, &src, now);
    run_link(&tx, &rx, &now, 5000, 0, 0);
    CHECK_EQ_INT(rx.state, FWRX_COMPLETE);
    /* The receiver goes quiet while installing: the sender polls, then
     * gives up after FWTX_INSTALL_MS. */
    for (uint32_t t = 0; t < FWTX_INSTALL_MS + 1000; t += 100) fwtx_poll(&tx, now += 100, f, sizeof(f));
    CHECK_EQ_INT(tx.state, FWTX_FAILED);
    CHECK_EQ_INT(tx.err, FW_E_TIMEOUT);

    /* Framing: bad CRC, oversize length, false starts. */
    fwl_parser_t p;
    memset(&p, 0, sizeof(p));
    uint8_t type;
    const uint8_t *pl;
    size_t len;
    static const uint8_t PL[3] = {1, 2, 3};
    size_t m = fwl_frame(f, sizeof(f), FWL_STATUS, PL, 3);
    int good = 0;
    for (size_t i = 0; i < m; i++) good += fwl_parse(&p, f[i], &type, &pl, &len);
    CHECK_EQ_INT(good, 1);
    f[5] ^= 1;
    good = 0;
    for (size_t i = 0; i < m; i++) good += fwl_parse(&p, f[i], &type, &pl, &len);
    CHECK_EQ_INT(good, 0);
    static const uint8_t JUNK[] = {'I', 'I', '2', 0x22, 250, 'I', '2'};
    for (size_t i = 0; i < sizeof(JUNK); i++) CHECK(!fwl_parse(&p, JUNK[i], &type, &pl, &len));
    CHECK_EQ_INT(fwl_frame(f, 8, FWL_STATUS, PL, 3), 0);
    /* The receiver ignores DATA out of order, oversized or before an offer. */
    fwrx_init(&rx, &d.fl, &TFW_LAYOUT, &pol);
    uint8_t dpl[4 + FWL_CHUNK] = {0};
    m = fwl_frame(f, sizeof(f), FWL_DATA, dpl, sizeof(dpl));
    CHECK(fwrx_rx(&rx, f, m));
    CHECK_EQ_INT(rx.state, FWRX_LISTEN);
    CHECK(store_intact(&d));
}

TEST(test_fw_log)
{
    static fwdev_t d;
    dev_init(&d);
    fwlog_t log;
    fwlog_load(&log, &d.fl, &TFW_LAYOUT);
    CHECK_EQ_INT(log.page, -1);
    CHECK_EQ_INT(log.floor, 0);
    /* Many installs: the log changes page many times; the floor only rises. */
    for (uint32_t v = 1; v <= 600; v++) {
        CHECK(fwlog_append(&log, &d.fl, &TFW_LAYOUT, FWLOG_INSTALL, v));
        if (v % 50 == 0) CHECK(fwlog_append(&log, &d.fl, &TFW_LAYOUT, FWLOG_CANCEL, v));
        else CHECK(fwlog_append(&log, &d.fl, &TFW_LAYOUT, FWLOG_DONE, v));
        fwlog_t again;
        fwlog_load(&again, &d.fl, &TFW_LAYOUT);
        CHECK_EQ_INT(again.floor, v % 50 == 0 ? v - 1 : v);
        CHECK(!again.pending);
    }
    CHECK(log.seq > 3);
    /* A pending install survives a page change. */
    fwlog_t pre;
    while (log.next + 8 < VF_PAGE) fwlog_append(&log, &d.fl, &TFW_LAYOUT, FWLOG_FLOOR, 1);
    CHECK(fwlog_append(&log, &d.fl, &TFW_LAYOUT, FWLOG_INSTALL, 700));
    pre = log;
    /* Now cut the power at every operation of the next page change, in
     * every way: afterwards the journal says either the old thing or the
     * new thing, and keeps working. */
    static vflash_t snap;
    snap = d.f;
    for (int mode = 0; mode < 3; mode++) {
        for (long k = 0; k < 8; k++) {
            d.f = snap;
            fwlog_t l = pre;
            while (l.next + 8 <= VF_PAGE) fwlog_append(&l, &d.fl, &TFW_LAYOUT, FWLOG_FLOOR, 2);
            vflash_cut(&d.f, k, (vf_cut_t)mode);
            fwlog_append(&l, &d.fl, &TFW_LAYOUT, FWLOG_DONE, 700);
            vflash_power_on(&d.f);
            fwlog_t after;
            fwlog_load(&after, &d.fl, &TFW_LAYOUT);
            bool old = after.floor == pre.floor && after.pending && after.pending_version == 700;
            bool neu = after.floor == 700 && !after.pending;
            CHECK(old || neu);
            CHECK(fwlog_append(&after, &d.fl, &TFW_LAYOUT, FWLOG_DONE, 701));
            fwlog_load(&after, &d.fl, &TFW_LAYOUT);
            CHECK_EQ_INT(after.floor, 701);
        }
    }
    /* A torn record is skipped, the next goes after it. */
    d.f = snap;
    fwlog_load(&log, &d.fl, &TFW_LAYOUT);
    CHECK(fwlog_append(&log, &d.fl, &TFW_LAYOUT, FWLOG_FLOOR, 3));
    uint32_t at = TFW_LAYOUT.log_addr + (uint32_t)log.page * VF_PAGE + log.next;
    vflash_tear(&d.f, at);
    fwlog_load(&log, &d.fl, &TFW_LAYOUT);
    CHECK_EQ_INT(log.skipped, 1);
    CHECK(fwlog_append(&log, &d.fl, &TFW_LAYOUT, FWLOG_DONE, 700));
    fwlog_load(&log, &d.fl, &TFW_LAYOUT);
    CHECK_EQ_INT(log.floor, 700);
    CHECK(store_intact(&d));
}

/* The power is cut at operation k of a transfer: the old firmware still
 * boots, and offering the same image again finishes the job. */
TEST(test_fw_powercut_receive)
{
    fixtures();
    static fwdev_t d;
    static uint8_t a1[0x8000], b2[0xE000];
    static vflash_t snap;
    dev_init(&d);
    size_t na = image_a(a1, 1, TFW_TEST_SEED);
    stage(&d, a1, na);
    CHECK_EQ_INT(boot(&d), 1);
    size_t nb = image_b(b2, 2, TFW_TEST_SEED);
    snap = d.f;
    /* How many operations does a clean transfer take? */
    long base = d.f.ops;
    fw_policy_t pol = policy();
    pol.floor = 1;
    static fwrx_t rx;
    static fwtx_t tx;
    src_t src = {b2, nb};
    uint32_t now = 0;
    fwrx_init(&rx, &d.fl, &TFW_LAYOUT, &pol);
    fwtx_start(&tx, b2, src_read, &src, now);
    run_link(&tx, &rx, &now, 600000, 0, 0);
    CHECK_EQ_INT(rx.state, FWRX_COMPLETE);
    long ops = d.f.ops - base;
    CHECK(ops > 200);
    int resumed = 0;
    for (long k = 0; k < ops; k++) {
        for (int mode = 0; mode < 3; mode++) {
            if (k % 7 != 0 && k > 20 && mode != 1) continue; /* every op in one mode */
            d.f = snap;
            fwrx_init(&rx, &d.fl, &TFW_LAYOUT, &pol);
            fwtx_start(&tx, b2, src_read, &src, now);
            vflash_cut(&d.f, k, (vf_cut_t)mode);
            run_link(&tx, &rx, &now, 600000, 0, 0);
            CHECK(rx.state == FWRX_FAILED || rx.state == FWRX_COMPLETE);
            vflash_power_on(&d.f);
            CHECK_EQ_INT(boot(&d), 1);
            long before = d.f.ops;
            fwrx_init(&rx, &d.fl, &TFW_LAYOUT, &pol);
            fwtx_start(&tx, b2, src_read, &src, now);
            run_link(&tx, &rx, &now, 600000, 0, 0);
            CHECK_EQ_INT(rx.state, FWRX_COMPLETE);
            /* run_link() moved d.f.ops through the flash callbacks. */
            // cppcheck-suppress duplicateExpression
            resumed += d.f.ops - before < ops / 2;
            fw_header_t h;
            CHECK_EQ_INT(fwboot_install_staged(&d.boot, &h), FW_OK);
            CHECK_EQ_INT(boot(&d), 2);
            CHECK(store_intact(&d));
        }
    }
    CHECK(resumed > ops / 4);
}

/* The power is cut at operation k of an install (and again, for some k, at
 * operation j of the recovery): the device always boots, the old firmware
 * only if the cut came before the install was journalled, else the new. */
TEST(test_fw_powercut_install)
{
    fixtures();
    static fwdev_t d;
    static uint8_t a1[0x8000], b2[0xE000];
    static vflash_t snap;
    dev_init(&d);
    size_t na = image_a(a1, 1, TFW_TEST_SEED);
    stage(&d, a1, na);
    CHECK_EQ_INT(boot(&d), 1);
    size_t nb = image_b(b2, 2, TFW_TEST_SEED);
    stage(&d, b2, nb);
    snap = d.f;
    fw_header_t h;
    long base = d.f.ops;
    CHECK_EQ_INT(fwboot_install_staged(&d.boot, &h), FW_OK);
    long ops = d.f.ops - base;
    CHECK(ops > 3000);
    int olds = 0, news = 0, nested = 0;
    for (long k = 0; k < ops; k++) {
        bool dense = k < 8 || k > ops - 8 || k % 97 == 0 || k % 41 == 5;
        for (int mode = 0; mode < 3; mode++) {
            if (!dense && (k % 13 != 0 || mode != (int)(k % 3))) continue;
            d.f = snap;
            vflash_cut(&d.f, k, (vf_cut_t)mode);
            fwboot_install_staged(&d.boot, &h);
            vflash_power_on(&d.f);
            if (k % 41 == 5 || k == 3) {
                /* Cut the recovery too, a little way in. */
                vflash_cut(&d.f, 1 + k % 40, (vf_cut_t)((mode + 1) % 3));
                boot(&d);
                vflash_power_on(&d.f);
                nested++;
            }
            uint32_t v = boot(&d);
            CHECK(v == 1 || v == 2);
            if (k > 0) CHECK_EQ_INT(v, 2);
            olds += v == 1;
            news += v == 2;
            CHECK_EQ_INT(boot(&d), v); /* stable */
            CHECK(store_intact(&d));
        }
    }
    CHECK(olds >= 1 && news > 100 && nested > 100);
}

/* make_fw_fixtures.py thumb_plain(): Thumb-like halfwords with BL pairs. */
static void thumb_plain(uint8_t *out, size_t n)
{
    uint32_t s = 0x2468ACE1u;
    size_t len = 0;
    while (len < n) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        uint8_t b[4];
        size_t k = 2;
        if (s % 5 == 0) {
            b[0] = (uint8_t)(s >> 16);
            b[1] = (uint8_t)(0xF0 | ((s >> 8) & 7));
            b[2] = (uint8_t)(s >> 24);
            b[3] = (uint8_t)(0xF8 | ((s >> 11) & 7));
            k = 4;
        } else {
            b[0] = (uint8_t)s;
            b[1] = (uint8_t)(s >> 8);
        }
        for (size_t i = 0; i < k && len < n; i++) out[len++] = b[i];
    }
}

/* The release format: xz's ARM-Thumb branch filter, then LZMA, as
 * sign_image.py makes it; decoded through every chunk size the copy uses. */
TEST(test_fw_thumb)
{
    fixtures();
    static fwdev_t d;
    static uint8_t want[FIX_THUMB_SIZE];
    thumb_plain(want, sizeof(want));
    dev_init(&d);
    fw_header_t h;
    CHECK_EQ_INT(fw_header_parse(FIX_THUMB_IMAGE, &h), FW_OK);
    CHECK_EQ_INT(h.comp, FW_COMP_LZMA_THUMB);
    stage(&d, FIX_THUMB_IMAGE, sizeof(FIX_THUMB_IMAGE));
    CHECK_EQ_INT(boot(&d), FIX_SAMPLE_VERSION);
    CHECK(memcmp(d.f.mem + TFW_LAYOUT.app_addr + FW_APP_OFFSET, want, sizeof(want)) == 0);
    /* A damaged payload never installs. */
    static uint8_t bad[sizeof(FIX_THUMB_IMAGE)];
    for (int i = 0; i < 8; i++) {
        memcpy(bad, FIX_THUMB_IMAGE, sizeof(bad));
        bad[FW_HDR_SIZE + 1 + rnd((unsigned)(sizeof(bad) - FW_HDR_SIZE - 40))] ^= (uint8_t)(1u << (i % 8));
        stage(&d, bad, sizeof(bad));
        fw_policy_t pol = policy();
        fw_err_t e = fw_verify_staging(&WORK, &d.fl, &TFW_LAYOUT, &pol, &h);
        CHECK(e == FW_E_SIGNATURE || e == FW_E_DATA);
    }
    CHECK(store_intact(&d));
}

/* A signed application programmed over SWD (the factory image) was never
 * journalled; the first boot makes its version the floor. */
TEST(test_fw_factory_floor)
{
    fixtures();
    static fwdev_t d;
    static uint8_t img[0xE000];
    dev_init(&d);
    image_b(img, 5, TFW_TEST_SEED);
    d.fl.program(d.fl.ctx, TFW_LAYOUT.app_addr, img, FW_HDR_SIZE);
    for (uint32_t i = 0; i < sizeof(BIN_B); i += 8) {
        uint8_t dw[8];
        memset(dw, 0xff, 8);
        memcpy(dw, BIN_B + i, sizeof(BIN_B) - i < 8 ? sizeof(BIN_B) - i : 8);
        d.fl.program(d.fl.ctx, TFW_LAYOUT.app_addr + FW_APP_OFFSET + i, dw, 8);
    }
    CHECK_EQ_INT(boot(&d), 5);
    CHECK_EQ_INT(d.boot.log.floor, 5);
    long ops = d.f.ops;
    CHECK_EQ_INT(boot(&d), 5);
    CHECK_EQ_INT(d.f.ops, ops); /* written once */
    size_t n = image_a(img, 4, TFW_TEST_SEED);
    CHECK_EQ_INT(ir_update(&d, img, n, 0), FW_E_ROLLBACK);
    CHECK(store_intact(&d));
}
