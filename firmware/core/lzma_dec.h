/* Streaming LZMA ("LZMA1", raw stream, no header) decoder for firmware
 * update images. Written from Igor Pavlov's public-domain LzmaSpec
 * reference; small rather than fast. The input is untrusted until the
 * image signature has been checked over the decoded output, so every
 * distance, length and state is bounds-checked: on malformed input the
 * decoder stops with an error, never reads or writes out of bounds.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_LZMA_DEC_H
#define IRL2FA_LZMA_DEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The window lives in RAM, so the encoder's dictionary must not be larger
 * (tools/sign_image.py uses 8 KB). lc + lp is limited to 2 (literal
 * probability tables grow as 0x300 << (lc + lp)). */
#define LZMA_DICT_MAX 8192u
#define LZMA_LCLP_MAX 2

/* Supplies up to `cap` input bytes; returns how many (0 = no more input,
 * or a read error: both stop the decoder). */
typedef size_t (*lzma_in_fn)(void *ctx, uint8_t *buf, size_t cap);

typedef struct {
    uint16_t choice, choice2;
    uint16_t low[16][8];
    uint16_t mid[16][8];
    uint16_t high[256];
} lzma_len_t;

typedef struct {
    /* probabilities */
    uint16_t is_match[12 << 4];
    uint16_t is_rep[12], is_rep_g0[12], is_rep_g1[12], is_rep_g2[12];
    uint16_t is_rep0_long[12 << 4];
    uint16_t pos_slot[4][64];
    uint16_t pos_dec[115];
    uint16_t align[16];
    lzma_len_t len_dec, rep_len_dec;
    uint16_t lit[0x300u << LZMA_LCLP_MAX];
    /* window */
    uint8_t dict[LZMA_DICT_MAX];
    uint32_t dict_size;
    uint32_t total;      /* bytes produced so far */
    uint32_t unpacked;   /* bytes to produce in all */
    /* range coder and input */
    uint32_t range, code;
    lzma_in_fn in;
    void *in_ctx;
    uint8_t ibuf[64];
    uint8_t ilen, ipos;
    /* coder state */
    unsigned lc, lp, pb;
    unsigned state;
    uint32_t rep0, rep1, rep2, rep3;
    uint32_t pending;    /* match bytes still to copy (distance rep0 + 1) */
    bool error;
} lzma_dec_t;

/* `props` is the standard properties byte, (pb * 5 + lp) * 9 + lc.
 * False (and d->error) if the properties or dictionary size are not
 * supported or the stream does not start like an LZMA stream. */
bool lzma_dec_init(lzma_dec_t *d, uint8_t props, uint32_t dict_size, uint32_t unpacked,
                   lzma_in_fn in, void *in_ctx);

/* Decodes up to `cap` bytes into `out`. Returns the count; less than `cap`
 * only at the end of the output (d->total == d->unpacked) or on an error
 * (d->error set, sticky). */
size_t lzma_dec_read(lzma_dec_t *d, uint8_t *out, size_t cap);

#endif
