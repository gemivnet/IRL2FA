/* libFuzzer harness: the IR frame parser and the pairing state machine.
 *
 * Device A (the target) gets its randomness from the input. Each further
 * input byte either goes to A's receiver as raw line noise or, if it is one
 * of the command bytes below, delivers whole frames from an honest peer B,
 * A's own echo, or a corrupted honest frame, so the fuzzer reaches every
 * state. Checked on every step: the receive buffer stays in bounds, a
 * finished session never changes, and if A finished with B's public key it
 * has B's key and SAS.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../core/pairing.h"

#define CMD_PEER 0xb0   /* B -> A and A -> B: one frame each way */
#define CMD_ECHO 0xb1   /* A hears its own frame */
#define CMD_FLIP 0xb2   /* B -> A with one byte flipped (next two bytes: where, what) */
#define CMD_TRUNC 0xb3  /* B -> A cut short (next byte: length) */

#define ASSERT(c) do { if (!(c)) abort(); } while (0)

static void check(const pairing_t *a, const pairing_t *b, pair_state_t *last)
{
    ASSERT(a->rx_len < PAIR_FRAME_MAX);
    ASSERT(a->state >= PAIR_IDLE && a->state <= PAIR_FAILED);
    if (*last == PAIR_DONE || *last == PAIR_FAILED) ASSERT(a->state == *last);
    if (*last == PAIR_REVEALING) ASSERT(a->state != PAIR_SEARCHING);
    *last = a->state;
    if (a->state == PAIR_DONE && b->state == PAIR_DONE && memcmp(a->peer_pub, b->pub, 32) == 0) {
        ASSERT(memcmp(a->key, b->key, 32) == 0);
        ASSERT(memcmp(a->sas, b->sas, PAIR_SAS_LEN) == 0);
        ASSERT(a->is_low != b->is_low);
    }
    for (int i = 0; i < PAIR_SAS_LEN; i++) ASSERT(a->sas[i] < 5);
}

/* Every frame we build must parse as itself. */
static void check_tx(const uint8_t *f, size_t n)
{
    ASSERT(n <= PAIR_FRAME_MAX);
    if (n == 0) return;
    ASSERT(f[0] == 'I' && f[1] == '2' && (size_t)f[3] + 6 == n);
    uint16_t crc = pairing_crc16(f + 2, n - 4);
    ASSERT(f[n - 2] == (uint8_t)(crc >> 8) && f[n - 1] == (uint8_t)crc);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t ra[48] = {0}, rb[48], f[PAIR_FRAME_MAX];
    size_t take = size < 48 ? size : 48;
    memcpy(ra, data, take);
    data += take;
    size -= take;
    for (int i = 0; i < 48; i++) rb[i] = (uint8_t)(0x5a ^ (i * 29));

    pairing_t a, b;
    pairing_start(&a, ra);
    pairing_start(&b, rb);
    pair_state_t last = a.state;
    unsigned seq_a = 0, seq_b = 1;

    for (size_t i = 0; i < size; i++) {
        uint8_t c = data[i];
        size_t n;
        switch (c) {
        case CMD_PEER:
            n = pairing_tx(&b, f, sizeof(f), seq_b++);
            check_tx(f, n);
            pairing_rx(&a, f, n);
            n = pairing_tx(&a, f, sizeof(f), seq_a++);
            check_tx(f, n);
            pairing_rx(&b, f, n);
            break;
        case CMD_ECHO:
            n = pairing_tx(&a, f, sizeof(f), seq_a++);
            check_tx(f, n);
            pairing_rx(&a, f, n);
            break;
        case CMD_FLIP:
            n = pairing_tx(&b, f, sizeof(f), seq_b++);
            if (n && i + 2 < size) {
                f[data[i + 1] % n] ^= data[i + 2];
                i += 2;
            }
            pairing_rx(&a, f, n);
            break;
        case CMD_TRUNC:
            n = pairing_tx(&b, f, sizeof(f), seq_b++);
            if (i + 1 < size) {
                n = n ? data[++i] % n : 0;
            }
            pairing_rx(&a, f, n);
            break;
        default:
            pairing_rx(&a, &c, 1);
            break;
        }
        check(&a, &b, &last);
    }
    pairing_wipe(&a);
    pairing_wipe(&b);
    return 0;
}
