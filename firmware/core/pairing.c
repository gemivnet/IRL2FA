/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "pairing.h"

#include <string.h>

#include "ct.h"
#include "hash.h"
#include "third_party/monocypher/monocypher.h"

/* Frame: 'I' '2' type len payload[len] crc16(type..payload), big endian. */
#define SYNC0 'I'
#define SYNC1 '2'
#define T_COMMIT 1
#define T_REVEAL 2
#define HDR 4
#define CRC 2

/* REVEAL payload: pub[32] nonce[16] first 8 bytes of the peer commit we
 * are answering (so a device ignores reveals addressed to someone else). */
#define REVEAL_LEN (32 + 16 + 8)

uint16_t pairing_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xffff; /* CRC-16/CCITT-FALSE */
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)(data[i] << 8);
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static void make_commit(const uint8_t pub[32], const uint8_t nonce[16], uint8_t out[32])
{
    static const char LABEL[] = "IRL2FA commit v1";
    hash_ctx_t h;
    hash_init(&h, HASH_SHA256);
    hash_update(&h, LABEL, sizeof(LABEL) - 1);
    hash_update(&h, pub, 32);
    hash_update(&h, nonce, 16);
    hash_final(&h, out);
}

void pairing_start(pairing_t *p, const uint8_t random[48])
{
    memset(p, 0, sizeof(*p));
    memcpy(p->priv, random, 32);
    memcpy(p->nonce, random + 32, 16);
    crypto_x25519_public_key(p->pub, p->priv);
    CT_PUBLIC(p->pub, sizeof(p->pub));       /* sent in REVEAL */
    make_commit(p->pub, p->nonce, p->commit);
    CT_PUBLIC(p->commit, sizeof(p->commit)); /* sent in COMMIT */
    p->state = PAIR_SEARCHING;
}

void pairing_wipe(pairing_t *p)
{
    crypto_wipe(p, sizeof(*p));
}

static void finish(pairing_t *p)
{
    uint8_t shared[32];
    crypto_x25519(shared, p->priv, p->peer_pub);

    /* Order the two public keys so both sides derive identical values. */
    p->is_low = memcmp(p->pub, p->peer_pub, 32) < 0;
    uint8_t info[2 * 32 + 8];
    memcpy(info, p->is_low ? p->pub : p->peer_pub, 32);
    memcpy(info + 32, p->is_low ? p->peer_pub : p->pub, 32);

    static const uint8_t SALT[] = "IRL2FA pair v1";
    uint8_t okm[32 + 8];
    /* 8 bytes on purpose, NUL included: part of the protocol, not a string. */
    memcpy(info + 64, "key+sas\0", 8); // NOLINT(bugprone-not-null-terminated-result)
    hkdf_sha256(SALT, sizeof(SALT) - 1, shared, sizeof(shared), info, sizeof(info),
                okm, sizeof(okm));
    memcpy(p->key, okm, 32);

    /* SAS: 6 base-5 digits from a 32-bit value (5^6 = 15625 outcomes). */
    uint32_t v = ((uint32_t)okm[32] << 24) | ((uint32_t)okm[33] << 16) |
                 ((uint32_t)okm[34] << 8) | okm[35];
    for (int i = 0; i < PAIR_SAS_LEN; i++) {
        p->sas[i] = (uint8_t)(v % 5);
        v /= 5;
    }
    CT_PUBLIC(p->sas, sizeof(p->sas)); /* shown on both screens */
    crypto_wipe(shared, sizeof(shared));
    crypto_wipe(okm, sizeof(okm));
    crypto_wipe(p->priv, sizeof(p->priv));
    p->state = PAIR_DONE;
}

static bool handle_frame(pairing_t *p, uint8_t type, const uint8_t *pl, size_t len)
{
    if (p->state == PAIR_IDLE || p->state == PAIR_DONE || p->state == PAIR_FAILED)
        return false;

    if (type == T_COMMIT && len == 32) {
        if (memcmp(pl, p->commit, 32) == 0) return false; /* our own echo */
        if (p->state == PAIR_SEARCHING) {
            memcpy(p->peer_commit, pl, 32);
            p->state = PAIR_REVEALING;
            return true;
        }
        return false;
    }
    if (type == T_REVEAL && len == REVEAL_LEN) {
        if (memcmp(pl, p->pub, 32) == 0) return false; /* our own echo */
        /* Only accept a reveal addressed to our commit, and only once we
         * have the sender's commitment: that ordering is what stops a
         * man in the middle from choosing a key to match the SAS. */
        if (memcmp(pl + 48, p->commit, 8) != 0) return false;
        if (p->state != PAIR_REVEALING) return false;
        uint8_t check[32];
        make_commit(pl, pl + 32, check);
        if (crypto_verify32(check, p->peer_commit) != 0) {
            p->state = PAIR_FAILED;
            return true;
        }
        memcpy(p->peer_pub, pl, 32);
        finish(p);
        return true;
    }
    return false;
}

bool pairing_rx(pairing_t *p, const uint8_t *data, size_t len)
{
    bool changed = false;
    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];
        if (p->rx_len == 0 && c != SYNC0) continue;
        if (p->rx_len == 1 && c != SYNC1) {
            p->rx_len = (c == SYNC0) ? 1 : 0;
            continue;
        }
        p->rx[p->rx_len++] = c;
        if (p->rx_len >= HDR) {
            size_t plen = p->rx[3];
            if (plen > PAIR_FRAME_MAX - HDR - CRC) {
                p->rx_len = 0;
                continue;
            }
            if (p->rx_len == HDR + plen + CRC) {
                uint16_t want = (uint16_t)((p->rx[HDR + plen] << 8) | p->rx[HDR + plen + 1]);
                if (pairing_crc16(p->rx + 2, 2 + plen) == want)
                    changed |= handle_frame(p, p->rx[2], p->rx + HDR, plen);
                p->rx_len = 0;
            }
        }
    }
    return changed;
}

static size_t frame(uint8_t *out, size_t cap, uint8_t type, const uint8_t *pl, size_t len)
{
    if (cap < HDR + len + CRC) return 0;
    out[0] = SYNC0;
    out[1] = SYNC1;
    out[2] = type;
    out[3] = (uint8_t)len;
    memcpy(out + HDR, pl, len);
    uint16_t crc = pairing_crc16(out + 2, 2 + len);
    out[HDR + len] = (uint8_t)(crc >> 8);
    out[HDR + len + 1] = (uint8_t)crc;
    return HDR + len + CRC;
}

size_t pairing_tx(const pairing_t *p, uint8_t *out, size_t cap, unsigned seq)
{
    switch (p->state) {
    case PAIR_SEARCHING:
        return frame(out, cap, T_COMMIT, p->commit, 32);
    case PAIR_REVEALING:
    case PAIR_DONE: {
        /* Alternate COMMIT and REVEAL, and keep going after we finish: the
         * peer may have missed our COMMIT or not yet heard our REVEAL. */
        if (seq & 1) return frame(out, cap, T_COMMIT, p->commit, 32);
        uint8_t pl[REVEAL_LEN];
        memcpy(pl, p->pub, 32);
        memcpy(pl + 32, p->nonce, 16);
        CT_PUBLIC(pl + 32, 16); /* the nonce is published by the reveal */
        memcpy(pl + 48, p->peer_commit, 8);
        return frame(out, cap, T_REVEAL, pl, sizeof(pl));
    }
    default:
        return 0;
    }
}
