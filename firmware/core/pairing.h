/* In-person pairing over IR: commit/reveal X25519 key agreement plus a
 * 6-symbol short authentication string (SAS) the two people compare.
 * See docs/pairing-protocol.md.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_PAIRING_H
#define IRL2FA_PAIRING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAIR_SAS_LEN 6
#define PAIR_FRAME_MAX 80

typedef enum {
    PAIR_IDLE = 0,
    PAIR_SEARCHING,  /* sending COMMIT, waiting for the peer's COMMIT */
    PAIR_REVEALING,  /* have peer COMMIT, sending REVEAL, waiting for peer's */
    PAIR_DONE,       /* key agreed; SAS ready for the people to compare */
    PAIR_FAILED,     /* peer's REVEAL did not match its COMMIT */
} pair_state_t;

typedef struct {
    pair_state_t state;
    uint8_t priv[32];
    uint8_t pub[32];
    uint8_t nonce[16];
    uint8_t commit[32];
    uint8_t peer_commit[32];
    uint8_t peer_pub[32];
    uint8_t key[32];             /* contact key once DONE */
    bool is_low;                 /* our role once DONE */
    uint8_t sas[PAIR_SAS_LEN];   /* symbols 0..4 = up, right, down, left, centre */
    /* frame receiver */
    uint8_t rx[PAIR_FRAME_MAX];
    size_t rx_len;
} pairing_t;

/* Starts a session using 48 random bytes (32 for the X25519 secret, 16 for
 * the commitment nonce). */
void pairing_start(pairing_t *p, const uint8_t random[48]);

/* Feeds received IR bytes. Returns true if the state changed. */
bool pairing_rx(pairing_t *p, const uint8_t *data, size_t len);

/* Builds the frame to (re)transmit now; `seq` counts transmissions so the
 * protocol can alternate frames. Returns its length, or 0 if there is
 * nothing to send. */
size_t pairing_tx(const pairing_t *p, uint8_t *out, size_t cap, unsigned seq);

void pairing_wipe(pairing_t *p);

/* Exposed for the tests. */
uint16_t pairing_crc16(const uint8_t *data, size_t len);

#endif
