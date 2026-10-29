/* SHA-1, SHA-256, SHA-512, HMAC over any of them, and HKDF-SHA256.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_HASH_H
#define IRL2FA_HASH_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    HASH_SHA1 = 0,
    HASH_SHA256 = 1,
    HASH_SHA512 = 2,
} hash_alg_t;

#define HASH_MAX_DIGEST 64
#define HASH_MAX_BLOCK 128

typedef struct {
    hash_alg_t alg;
    uint64_t len_lo; /* bytes processed (SHA-512 length fits in 64 bits for our use) */
    uint8_t buf[HASH_MAX_BLOCK];
    size_t buf_len;
    union {
        uint32_t h32[8];
        uint64_t h64[8];
    } s;
} hash_ctx_t;

size_t hash_digest_len(hash_alg_t alg);
size_t hash_block_len(hash_alg_t alg);

void hash_init(hash_ctx_t *ctx, hash_alg_t alg);
void hash_update(hash_ctx_t *ctx, const void *data, size_t len);
void hash_final(hash_ctx_t *ctx, uint8_t *out);
void hash(hash_alg_t alg, const void *data, size_t len, uint8_t *out);

typedef struct {
    hash_ctx_t inner;
    hash_ctx_t outer;
} hmac_ctx_t;

void hmac_init(hmac_ctx_t *ctx, hash_alg_t alg, const uint8_t *key, size_t key_len);
void hmac_update(hmac_ctx_t *ctx, const void *data, size_t len);
void hmac_final(hmac_ctx_t *ctx, uint8_t *out);
void hmac(hash_alg_t alg, const uint8_t *key, size_t key_len,
          const void *data, size_t len, uint8_t *out);

/* RFC 5869 HKDF with SHA-256. out_len <= 255*32. */
void hkdf_sha256(const uint8_t *salt, size_t salt_len,
                 const uint8_t *ikm, size_t ikm_len,
                 const uint8_t *info, size_t info_len,
                 uint8_t *out, size_t out_len);

#endif
