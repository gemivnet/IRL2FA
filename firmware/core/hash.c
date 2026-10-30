/* SHA-1 (FIPS 180-4), SHA-256, SHA-512, HMAC (RFC 2104), HKDF (RFC 5869).
 * Straightforward, size-oriented implementations; not constant-time with
 * respect to message length, which is public everywhere we use them.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include "hash.h"

#include <string.h>

#define ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

static uint32_t load32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t load64be(const uint8_t *p)
{
    return ((uint64_t)load32be(p) << 32) | load32be(p + 4);
}

static void store32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void store64be(uint8_t *p, uint64_t v)
{
    store32be(p, (uint32_t)(v >> 32));
    store32be(p + 4, (uint32_t)v);
}

/* The bootloader only needs SHA-512 (Ed25519 and the image digest):
 * IRL2FA_HASH_SHA512_ONLY leaves the others out of it. */
#ifndef IRL2FA_HASH_SHA512_ONLY
/* ---- SHA-1 ---- */

static void sha1_block(uint32_t h[5], const uint8_t *b)
{
    uint32_t w[16];
    for (int i = 0; i < 16; i++) w[i] = load32be(b + 4 * i);
    uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        if (i >= 16) {
            uint32_t t = w[(i + 13) & 15] ^ w[(i + 8) & 15] ^ w[(i + 2) & 15] ^ w[i & 15];
            w[i & 15] = ROL32(t, 1);
        }
        uint32_t f, k;
        if (i < 20) { f = (bb & c) | (~bb & d); k = 0x5a827999; }
        else if (i < 40) { f = bb ^ c ^ d; k = 0x6ed9eba1; }
        else if (i < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8f1bbcdc; }
        else { f = bb ^ c ^ d; k = 0xca62c1d6; }
        uint32_t t = ROL32(a, 5) + f + e + k + w[i & 15];
        e = d; d = c; c = ROL32(bb, 30); bb = a; a = t;
    }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
}

/* ---- SHA-256 ---- */

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static void sha256_block(uint32_t h[8], const uint8_t *b)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = load32be(b + 4 * i);
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + K256[i] + w[i];
        uint32_t S0 = ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22);
        uint32_t maj = (a & bb) ^ (a & c) ^ (bb & c);
        uint32_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
    }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

#endif /* IRL2FA_HASH_SHA512_ONLY */

/* ---- SHA-512 ---- */

static const uint64_t K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

static void sha512_block(uint64_t h[8], const uint8_t *b)
{
    uint64_t w[16];
    for (int i = 0; i < 16; i++) w[i] = load64be(b + 8 * i);
    uint64_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 80; i++) {
        if (i >= 16) {
            uint64_t x = w[(i + 1) & 15], y = w[(i + 14) & 15];
            uint64_t s0 = ROR64(x, 1) ^ ROR64(x, 8) ^ (x >> 7);
            uint64_t s1 = ROR64(y, 19) ^ ROR64(y, 61) ^ (y >> 6);
            w[i & 15] += s0 + w[(i + 9) & 15] + s1;
        }
        uint64_t S1 = ROR64(e, 14) ^ ROR64(e, 18) ^ ROR64(e, 41);
        uint64_t ch = (e & f) ^ (~e & g);
        uint64_t t1 = hh + S1 + ch + K512[i] + w[i & 15];
        uint64_t S0 = ROR64(a, 28) ^ ROR64(a, 34) ^ ROR64(a, 39);
        uint64_t maj = (a & bb) ^ (a & c) ^ (bb & c);
        uint64_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
    }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

/* ---- generic front end ---- */

size_t hash_digest_len(hash_alg_t alg)
{
    switch (alg) {
    case HASH_SHA1: return 20;
    case HASH_SHA256: return 32;
    default: return 64;
    }
}

size_t hash_block_len(hash_alg_t alg)
{
    return alg == HASH_SHA512 ? 128 : 64;
}

void hash_init(hash_ctx_t *ctx, hash_alg_t alg)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->alg = alg;
    switch (alg) {
    case HASH_SHA1: {
        static const uint32_t iv[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
        memcpy(ctx->s.h32, iv, sizeof(iv));
        break;
    }
    case HASH_SHA256: {
        static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        memcpy(ctx->s.h32, iv, sizeof(iv));
        break;
    }
    default: {
        static const uint64_t iv[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
                                       0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                                       0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
                                       0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
        memcpy(ctx->s.h64, iv, sizeof(iv));
        break;
    }
    }
}

static void hash_block(hash_ctx_t *ctx, const uint8_t *b)
{
#ifdef IRL2FA_HASH_SHA512_ONLY
    sha512_block(ctx->s.h64, b);
#else
    switch (ctx->alg) {
    case HASH_SHA1: sha1_block(ctx->s.h32, b); break;
    case HASH_SHA256: sha256_block(ctx->s.h32, b); break;
    default: sha512_block(ctx->s.h64, b); break;
    }
#endif
}

void hash_update(hash_ctx_t *ctx, const void *data, size_t len)
{
    const uint8_t *p = data;
    size_t bl = hash_block_len(ctx->alg);
    ctx->len_lo += len;
    while (len > 0) {
        size_t n = bl - ctx->buf_len;
        if (n > len) n = len;
        memcpy(ctx->buf + ctx->buf_len, p, n);
        ctx->buf_len += n;
        p += n;
        len -= n;
        if (ctx->buf_len == bl) {
            hash_block(ctx, ctx->buf);
            ctx->buf_len = 0;
        }
    }
}

void hash_final(hash_ctx_t *ctx, uint8_t *out)
{
    size_t bl = hash_block_len(ctx->alg);
    size_t lenfield = ctx->alg == HASH_SHA512 ? 16 : 8;
    uint64_t bits = ctx->len_lo * 8;
    ctx->buf[ctx->buf_len++] = 0x80;
    if (ctx->buf_len > bl - lenfield) {
        memset(ctx->buf + ctx->buf_len, 0, bl - ctx->buf_len);
        hash_block(ctx, ctx->buf);
        ctx->buf_len = 0;
    }
    memset(ctx->buf + ctx->buf_len, 0, bl - ctx->buf_len);
    store64be(ctx->buf + bl - 8, bits);
    hash_block(ctx, ctx->buf);

    switch (ctx->alg) {
    case HASH_SHA1:
        for (int i = 0; i < 5; i++) store32be(out + 4 * i, ctx->s.h32[i]);
        break;
    case HASH_SHA256:
        for (int i = 0; i < 8; i++) store32be(out + 4 * i, ctx->s.h32[i]);
        break;
    default:
        for (int i = 0; i < 8; i++) store64be(out + 8 * i, ctx->s.h64[i]);
        break;
    }
    memset(ctx, 0, sizeof(*ctx));
}

void hash(hash_alg_t alg, const void *data, size_t len, uint8_t *out)
{
    hash_ctx_t ctx;
    hash_init(&ctx, alg);
    hash_update(&ctx, data, len);
    hash_final(&ctx, out);
}

/* ---- HMAC ---- */

void hmac_init(hmac_ctx_t *ctx, hash_alg_t alg, const uint8_t *key, size_t key_len)
{
    uint8_t k[HASH_MAX_BLOCK];
    size_t bl = hash_block_len(alg);
    memset(k, 0, sizeof(k));
    if (key_len > bl) {
        hash(alg, key, key_len, k);
    } else {
        memcpy(k, key, key_len);
    }
    for (size_t i = 0; i < bl; i++) k[i] ^= 0x36;
    hash_init(&ctx->inner, alg);
    hash_update(&ctx->inner, k, bl);
    for (size_t i = 0; i < bl; i++) k[i] ^= 0x36 ^ 0x5c;
    hash_init(&ctx->outer, alg);
    hash_update(&ctx->outer, k, bl);
    memset(k, 0, sizeof(k));
}

void hmac_update(hmac_ctx_t *ctx, const void *data, size_t len)
{
    hash_update(&ctx->inner, data, len);
}

void hmac_final(hmac_ctx_t *ctx, uint8_t *out)
{
    uint8_t ih[HASH_MAX_DIGEST];
    size_t dl = hash_digest_len(ctx->inner.alg);
    hash_final(&ctx->inner, ih);
    hash_update(&ctx->outer, ih, dl);
    hash_final(&ctx->outer, out);
    memset(ih, 0, sizeof(ih));
}

void hmac(hash_alg_t alg, const uint8_t *key, size_t key_len,
          const void *data, size_t len, uint8_t *out)
{
    hmac_ctx_t ctx;
    hmac_init(&ctx, alg, key, key_len);
    hmac_update(&ctx, data, len);
    hmac_final(&ctx, out);
}

/* ---- HKDF-SHA256 ---- */

void hkdf_sha256(const uint8_t *salt, size_t salt_len,
                 const uint8_t *ikm, size_t ikm_len,
                 const uint8_t *info, size_t info_len,
                 uint8_t *out, size_t out_len)
{
    static const uint8_t zeros[32] = {0};
    uint8_t prk[32], t[32];
    if (salt == NULL || salt_len == 0) {
        salt = zeros;
        salt_len = sizeof(zeros);
    }
    hmac(HASH_SHA256, salt, salt_len, ikm, ikm_len, prk);

    size_t t_len = 0;
    uint8_t counter = 1;
    while (out_len > 0) {
        hmac_ctx_t ctx;
        hmac_init(&ctx, HASH_SHA256, prk, sizeof(prk));
        hmac_update(&ctx, t, t_len);
        hmac_update(&ctx, info, info_len);
        hmac_update(&ctx, &counter, 1);
        hmac_final(&ctx, t);
        t_len = sizeof(t);
        size_t n = out_len < t_len ? out_len : t_len;
        memcpy(out, t, n);
        out += n;
        out_len -= n;
        counter++;
    }
    memset(prk, 0, sizeof(prk));
    memset(t, 0, sizeof(t));
}
