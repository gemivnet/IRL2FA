/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "../core/base32.h"
#include "../core/hash.h"
#include "../core/third_party/monocypher/monocypher.h"
#include "../core/timeutil.h"
#include "../core/totp.h"
#include "test.h"

static const char ABC[] = "abc";
static const char TWO_BLOCK[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

TEST(test_sha)
{
    unsigned char out[64];
    hash(HASH_SHA1, ABC, 3, out);
    CHECK_HEX(out, 20, "a9993e364706816aba3e25717850c26c9cd0d89d");
    hash(HASH_SHA1, TWO_BLOCK, strlen(TWO_BLOCK), out);
    CHECK_HEX(out, 20, "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    hash(HASH_SHA256, ABC, 3, out);
    CHECK_HEX(out, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    hash(HASH_SHA256, TWO_BLOCK, strlen(TWO_BLOCK), out);
    CHECK_HEX(out, 32, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    hash(HASH_SHA512, ABC, 3, out);
    CHECK_HEX(out, 64,
              "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
              "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    hash(HASH_SHA256, "", 0, out);
    CHECK_HEX(out, 32, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    /* Incremental updates across block boundaries match one-shot. */
    hash_ctx_t c;
    unsigned char one[32];
    hash(HASH_SHA256, TWO_BLOCK, strlen(TWO_BLOCK), one);
    hash_init(&c, HASH_SHA256);
    for (size_t i = 0; i < strlen(TWO_BLOCK); i++) hash_update(&c, TWO_BLOCK + i, 1);
    hash_final(&c, out);
    CHECK(memcmp(one, out, 32) == 0);
}

TEST(test_hmac)
{
    unsigned char out[64];
    static const char DATA[] = "what do ya want for nothing?";
    hmac(HASH_SHA1, (const uint8_t *)"Jefe", 4, DATA, strlen(DATA), out);
    CHECK_HEX(out, 20, "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79");
    hmac(HASH_SHA256, (const uint8_t *)"Jefe", 4, DATA, strlen(DATA), out);
    CHECK_HEX(out, 32, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    hmac(HASH_SHA512, (const uint8_t *)"Jefe", 4, DATA, strlen(DATA), out);
    CHECK_HEX(out, 64,
              "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea250554"
              "9758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737");
    /* RFC 4231 case 6: key longer than the block size. */
    unsigned char key[131];
    memset(key, 0xaa, sizeof(key));
    static const char D6[] = "Test Using Larger Than Block-Size Key - Hash Key First";
    hmac(HASH_SHA256, key, sizeof(key), D6, strlen(D6), out);
    CHECK_HEX(out, 32, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

TEST(test_hkdf)
{
    unsigned char ikm[22], salt[13], info[10], okm[42];
    memset(ikm, 0x0b, sizeof(ikm));
    for (int i = 0; i < 13; i++) salt[i] = (unsigned char)i;
    for (int i = 0; i < 10; i++) info[i] = (unsigned char)(0xf0 + i);
    hkdf_sha256(salt, sizeof(salt), ikm, sizeof(ikm), info, sizeof(info), okm, sizeof(okm));
    CHECK_HEX(okm, 42,
              "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
              "34007208d5b887185865");
}

TEST(test_hotp_rfc4226)
{
    static const uint32_t WANT[10] = {755224, 287082, 359152, 969429, 338314,
                                      254676, 287922, 162583, 399871, 520489};
    const uint8_t *key = (const uint8_t *)"12345678901234567890";
    for (int i = 0; i < 10; i++) CHECK_EQ_INT(hotp(HASH_SHA1, key, 20, (uint64_t)i, 6), WANT[i]);
}

TEST(test_totp_rfc6238)
{
    static const struct {
        uint64_t t;
        uint32_t sha1, sha256, sha512;
    } V[] = {
        {59, 94287082, 46119246, 90693936},
        {1111111109, 7081804, 68084774, 25091201},
        {1111111111, 14050471, 67062674, 99943326},
        {1234567890, 89005924, 91819424, 93441116},
        {2000000000, 69279037, 90698825, 38618901},
        {20000000000ULL, 65353130, 77737706, 47863826},
    };
    const uint8_t *k1 = (const uint8_t *)"12345678901234567890";
    const uint8_t *k256 = (const uint8_t *)"12345678901234567890123456789012";
    const uint8_t *k512 =
        (const uint8_t *)"1234567890123456789012345678901234567890123456789012345678901234";
    for (size_t i = 0; i < sizeof(V) / sizeof(V[0]); i++) {
        CHECK_EQ_INT(totp(HASH_SHA1, k1, 20, V[i].t, 30, 8), V[i].sha1);
        CHECK_EQ_INT(totp(HASH_SHA256, k256, 32, V[i].t, 30, 8), V[i].sha256);
        CHECK_EQ_INT(totp(HASH_SHA512, k512, 64, V[i].t, 30, 8), V[i].sha512);
    }
    char s[10];
    totp_format(7081804, 8, s);
    CHECK(strcmp(s, "07081804") == 0);
    totp_format(42, 6, s);
    CHECK(strcmp(s, "000042") == 0);
}

TEST(test_base32)
{
    uint8_t out[64];
    /* RFC 4648 section 10 */
    CHECK_EQ_INT(base32_decode("MZXW6YTBOI======", out, sizeof(out)), 6);
    CHECK(memcmp(out, "foobar", 6) == 0);
    CHECK_EQ_INT(base32_decode("mzxw 6ytb-oi", out, sizeof(out)), 6);
    CHECK(memcmp(out, "foobar", 6) == 0);
    CHECK_EQ_INT(base32_decode("MZXW6YQ=", out, sizeof(out)), 4);
    CHECK(memcmp(out, "foob", 4) == 0);
    CHECK_EQ_INT(base32_decode("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ", out, sizeof(out)), 20);
    CHECK(memcmp(out, "12345678901234567890", 20) == 0);
    CHECK_EQ_INT(base32_decode("ABC1", out, sizeof(out)), -1);      /* 1 is not base32 */
    CHECK_EQ_INT(base32_decode("MZXW6YTBOI", out, 3), -1);          /* too long */
    char enc[40];
    base32_encode((const uint8_t *)"foobar", 6, enc);
    CHECK(strcmp(enc, "MZXW6YTBOI") == 0);
    CHECK(base32_is_alpha('Q') && base32_is_alpha('7') && !base32_is_alpha('8'));
}

TEST(test_x25519_rfc7748)
{
    uint8_t a[32], b[32], pa[32], pb[32], s1[32], s2[32];
    unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a);
    unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", b);
    crypto_x25519_public_key(pa, a);
    crypto_x25519_public_key(pb, b);
    CHECK_HEX(pa, 32, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    CHECK_HEX(pb, 32, "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    crypto_x25519(s1, a, pb);
    crypto_x25519(s2, b, pa);
    CHECK_HEX(s1, 32, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    CHECK(memcmp(s1, s2, 32) == 0);
}

TEST(test_time)
{
    civil_t c;
    time_to_civil(0, &c);
    CHECK(c.year == 1970 && c.month == 1 && c.day == 1 && c.hour == 0);
    time_to_civil(1767225600, &c);
    CHECK(c.year == 2026 && c.month == 1 && c.day == 1);
    time_to_civil(951782400, &c); /* 2000-02-29 */
    CHECK(c.year == 2000 && c.month == 2 && c.day == 29);
    for (int64_t t = 0; t < 4102444800LL; t += 86400 * 37 + 3601) {
        time_to_civil(t, &c);
        CHECK_EQ_INT(time_from_civil(&c), t);
    }
    CHECK_EQ_INT(time_days_in_month(2028, 2), 29);
    CHECK_EQ_INT(time_days_in_month(2100, 2), 28);
}
