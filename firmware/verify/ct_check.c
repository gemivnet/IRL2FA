/* Constant-time check (the "ctgrind" technique). Run under valgrind memcheck
 * with secrets marked undefined: memcheck then reports every conditional
 * branch and every memory address that depends on a secret. Each check
 * below counts the errors its code produces and must produce none; a
 * deliberately leaky comparison must be caught, so the checker cannot pass
 * vacuously. Build and run: make -C firmware ct (needs valgrind).
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include <valgrind/memcheck.h>

#include "../core/ct.h"
#include "../core/hal.h"
#include "../core/pairing.h"
#include "../core/store.h"
#include "../core/third_party/monocypher/monocypher.h"
#include "../vhal/vhal.h"

static int failures;

#define CHECK_CT(name, expect_leak, ...)                                         \
    do {                                                                         \
        unsigned before_ = VALGRIND_COUNT_ERRORS;                                \
        __VA_ARGS__;                                                             \
        unsigned n_ = VALGRIND_COUNT_ERRORS - before_;                           \
        int bad_ = (expect_leak) ? n_ == 0 : n_ != 0;                            \
        printf("%-52s %s (%u reports)\n", name, bad_ ? "FAIL" : "ok", n_);       \
        failures += bad_;                                                        \
    } while (0)

static void fill(uint8_t *b, size_t n, uint8_t seed)
{
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(seed + 13 * i);
}

/* What ct_equal replaces: returns early at the first difference. */
__attribute__((noinline)) static int leaky_equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static volatile int sink;

/* Offsets in the meta page (store.c meta_t): MAC and wrapped master key. */
#define META_MAC 48
#define META_MK 64

int main(void)
{
    if (!RUNNING_ON_VALGRIND) {
        fprintf(stderr, "ct_check must run under valgrind (make -C firmware ct)\n");
        return 2;
    }
    uint8_t a[32], b[32];

    /* 0. The checker works: an early-exit comparison of secrets is caught. */
    CHECK_CT("control: early-exit compare is reported", 1, {
        fill(a, 32, 1);
        fill(b, 32, 1);
        b[20] ^= 1;
        CT_SECRET(a, 32);
        CT_SECRET(b, 32);
        sink = leaky_equal(a, b, 32);
    });

    /* 1. PIN confirmation compare in the app (ct_equal). */
    CHECK_CT("ct_equal (PIN confirm), equal and unequal", 0, {
        fill(a, 12, 2);
        fill(b, 12, 2);
        CT_SECRET(a, 12);
        CT_SECRET(b, 12);
        sink = ct_equal(a, b, 12);
        b[3] ^= 0x40;
        CT_SECRET(b, 12);
        sink += ct_equal(a, b, 12);
    });

    /* 2. MAC/tag and commitment compares. */
    CHECK_CT("crypto_verify16/32 (tag, commitment)", 0, {
        fill(a, 32, 3);
        fill(b, 32, 4);
        CT_SECRET(a, 32);
        CT_SECRET(b, 32);
        int r = crypto_verify16(a, b) | crypto_verify32(a, b);
        CT_PUBLIC(&r, sizeof(r));
        sink = r;
    });

    /* 3. AEAD open with a secret key and tag: only the yes/no result may
     * steer control flow (declassified in ct.supp, see there). */
    CHECK_CT("crypto_aead_unlock, right and wrong tag", 0, {
        uint8_t key[32], nonce[24], mac[16], pt[40], ct[40], out[40];
        fill(key, 32, 5);
        fill(nonce, 24, 6);
        fill(pt, 40, 7);
        crypto_aead_lock(ct, mac, key, nonce, NULL, 0, pt, 40);
        CT_SECRET(key, 32);
        CT_SECRET(pt, 40);
        int r = crypto_aead_unlock(out, mac, key, nonce, NULL, 0, ct, 40);
        CT_PUBLIC(&r, sizeof(r));
        mac[0] ^= 1;
        int r2 = crypto_aead_unlock(out, mac, key, nonce, NULL, 0, ct, 40);
        CT_PUBLIC(&r2, sizeof(r2));
        sink = r + r2;
    });

    /* 4. PIN verification: Argon2i over a secret PIN, then unwrapping a
     * secret master key; then reading a record under that key. */
    static const uint8_t PIN[] = {1, 2, 3, 4, 3, 2};
    vhal_reset(7);
    store_mount();
    totp_rec_t t = {0};
    strcpy(t.label, "Secret");
    t.secret_len = 20;
    store_write_totp(-1, &t);
    store_set_pin(PIN, sizeof(PIN));
    CHECK_CT("store_unlock: wrong PIN, then right PIN", 0, {
        uint8_t pin[sizeof(PIN)];
        CT_SECRET(vhal_flash() + META_MAC, 16 + 32); /* tag and wrapped key */
        store_mount();
        memcpy(pin, PIN, sizeof(pin));
        pin[5] ^= 7;
        CT_SECRET(pin, sizeof(pin));
        int r1 = store_unlock(pin, sizeof(pin));
        memcpy(pin, PIN, sizeof(pin));
        CT_SECRET(pin, sizeof(pin));
        int r2 = store_unlock(pin, sizeof(pin));
        sink = r1 * 10 + r2;
        printf("  (unlock results: wrong=%d right=%d)\n", r1, r2);
        failures += !(r1 == STORE_BAD_PIN && r2 == STORE_OK);
    });
    CHECK_CT("store_read_totp under a secret master key", 0, {
        totp_rec_t back;
        int ok = store_read_totp(0, &back);
        sink = ok;
        failures += !ok;
    });

    /* 5. Pairing: X25519, commitment, HKDF and the SAS derivation, with
     * the private key and nonce secret until the protocol publishes them. */
    CHECK_CT("pairing: keygen, commit/reveal, X25519, HKDF, SAS", 0, {
        pairing_t pa, pb;
        uint8_t ra[48], rb[48], f[PAIR_FRAME_MAX];
        fill(ra, 48, 8);
        fill(rb, 48, 9);
        CT_SECRET(ra, 48);
        CT_SECRET(rb, 48);
        pairing_start(&pa, ra);
        pairing_start(&pb, rb);
        for (unsigned seq = 0; seq < 6; seq++) {
            size_t n = pairing_tx(&pa, f, sizeof(f), seq);
            pairing_rx(&pb, f, n);
            n = pairing_tx(&pb, f, sizeof(f), seq + 1);
            pairing_rx(&pa, f, n);
        }
        CT_PUBLIC(pa.key, 32); /* only so the harness can check the result */
        CT_PUBLIC(pb.key, 32);
        int ok = pa.state == PAIR_DONE && pb.state == PAIR_DONE &&
                 memcmp(pa.key, pb.key, 32) == 0 && memcmp(pa.sas, pb.sas, PAIR_SAS_LEN) == 0;
        failures += !ok;
        printf("  (pairing %s)\n", ok ? "completed, keys and SAS agree" : "DID NOT COMPLETE");
    });

    printf("\n%s\n", failures ? "constant-time check FAILED" : "constant-time check passed");
    return failures ? 1 : 0;
}
