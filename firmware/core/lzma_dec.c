/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lzma_dec.h"

#include <string.h>

#define PROB_BITS 11
#define PROB_INIT (1u << (PROB_BITS - 1))
#define MOVE_BITS 5
#define TOP (1u << 24)
#define END_POS_MODEL 14
#define MATCH_MIN 2

static uint8_t in_byte(lzma_dec_t *d)
{
    if (d->ipos == d->ilen) {
        size_t n = d->error ? 0 : d->in(d->in_ctx, d->ibuf, sizeof(d->ibuf));
        if (n == 0 || n > sizeof(d->ibuf)) {
            d->error = true;
            return 0;
        }
        d->ilen = (uint8_t)n;
        d->ipos = 0;
    }
    return d->ibuf[d->ipos++];
}

static void normalize(lzma_dec_t *d)
{
    if (d->range < TOP) {
        d->range <<= 8;
        d->code = (d->code << 8) | in_byte(d);
    }
}

static unsigned bit(lzma_dec_t *d, uint16_t *p)
{
    unsigned v = *p;
    uint32_t bound = (d->range >> PROB_BITS) * v;
    unsigned r;
    if (d->code < bound) {
        v += ((1u << PROB_BITS) - v) >> MOVE_BITS;
        d->range = bound;
        r = 0;
    } else {
        v -= v >> MOVE_BITS;
        d->code -= bound;
        d->range -= bound;
        r = 1;
    }
    *p = (uint16_t)v;
    normalize(d);
    return r;
}

static uint32_t direct_bits(lzma_dec_t *d, unsigned n)
{
    uint32_t res = 0;
    while (n--) {
        d->range >>= 1;
        d->code -= d->range;
        uint32_t t = 0u - (d->code >> 31);
        d->code += d->range & t;
        if (d->code == d->range) d->error = true;
        normalize(d);
        res = (res << 1) + (t + 1u);
    }
    return res;
}

static unsigned tree(lzma_dec_t *d, uint16_t *p, unsigned nbits)
{
    unsigned m = 1;
    for (unsigned i = 0; i < nbits; i++) m = (m << 1) + bit(d, &p[m]);
    return m - (1u << nbits);
}

static unsigned rtree(lzma_dec_t *d, uint16_t *p, unsigned nbits)
{
    unsigned m = 1, sym = 0;
    for (unsigned i = 0; i < nbits; i++) {
        unsigned b = bit(d, &p[m]);
        m = (m << 1) + b;
        sym |= b << i;
    }
    return sym;
}

static void init_probs(uint16_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) p[i] = PROB_INIT;
}

static void init_len(lzma_len_t *l)
{
    l->choice = l->choice2 = PROB_INIT;
    init_probs(&l->low[0][0], sizeof(l->low) / sizeof(uint16_t));
    init_probs(&l->mid[0][0], sizeof(l->mid) / sizeof(uint16_t));
    init_probs(l->high, sizeof(l->high) / sizeof(uint16_t));
}

/* The byte `dist` positions back (dist >= 1, checked by the callers). */
static uint8_t back(const lzma_dec_t *d, uint32_t dist)
{
    return d->dict[(d->total - dist) & (d->dict_size - 1u)];
}

static void put(lzma_dec_t *d, uint8_t b)
{
    d->dict[d->total & (d->dict_size - 1u)] = b;
    d->total++;
}

static uint32_t len_decode(lzma_dec_t *d, lzma_len_t *l, unsigned ps)
{
    if (!bit(d, &l->choice)) return tree(d, l->low[ps], 3);
    if (!bit(d, &l->choice2)) return 8u + tree(d, l->mid[ps], 3);
    return 16u + tree(d, l->high, 8);
}

static uint32_t distance(lzma_dec_t *d, uint32_t len)
{
    unsigned slot = tree(d, d->pos_slot[len > 3 ? 3 : len], 6);
    if (slot < 4) return slot;
    unsigned nd = (slot >> 1) - 1u;
    uint32_t dist = (2u | (slot & 1u)) << nd;
    if (slot < END_POS_MODEL) {
        dist += rtree(d, &d->pos_dec[dist - slot], nd);
    } else {
        dist += direct_bits(d, nd - 4u) << 4;
        dist += rtree(d, d->align, 4);
    }
    return dist;
}

static void literal(lzma_dec_t *d)
{
    unsigned prev = d->total ? back(d, 1) : 0;
    unsigned ls = ((d->total & ((1u << d->lp) - 1u)) << d->lc) + (prev >> (8u - d->lc));
    uint16_t *p = &d->lit[0x300u * ls];
    unsigned sym = 1;
    if (d->state >= 7) {
        /* rep0 < total here: it was checked when the match that put the
         * coder in this state was decoded. */
        unsigned mb = back(d, d->rep0 + 1u);
        do {
            unsigned mbit = (mb >> 7) & 1u;
            mb <<= 1;
            unsigned b = bit(d, &p[((1u + mbit) << 8) + sym]);
            sym = (sym << 1) | b;
            if (mbit != b) break;
        } while (sym < 0x100u);
    }
    while (sym < 0x100u) sym = (sym << 1) | bit(d, &p[sym]);
    put(d, (uint8_t)(sym - 0x100u));
    d->state = d->state < 4 ? 0 : d->state < 10 ? d->state - 3u : d->state - 6u;
}

/* One coder step: a literal or a short rep (one byte into the window), or a
 * match (d->pending bytes to copy). */
static void step(lzma_dec_t *d)
{
    unsigned ps = d->total & ((1u << d->pb) - 1u);
    unsigned s = d->state;
    if (!bit(d, &d->is_match[(s << 4) + ps])) {
        literal(d);
        return;
    }
    uint32_t len;
    if (!bit(d, &d->is_rep[s])) {
        d->rep3 = d->rep2;
        d->rep2 = d->rep1;
        d->rep1 = d->rep0;
        len = len_decode(d, &d->len_dec, ps);
        d->state = s < 7 ? 7u : 10u;
        d->rep0 = distance(d, len);
        /* 0xFFFFFFFF is the end marker: too early, we know the size. */
    } else {
        if (d->total == 0) {
            d->error = true;
            return;
        }
        if (!bit(d, &d->is_rep_g0[s])) {
            if (!bit(d, &d->is_rep0_long[(s << 4) + ps])) {
                d->state = s < 7 ? 9u : 11u;
                if (d->rep0 >= d->total) {
                    d->error = true;
                    return;
                }
                put(d, back(d, d->rep0 + 1u));
                return;
            }
        } else {
            uint32_t dist;
            if (!bit(d, &d->is_rep_g1[s])) {
                dist = d->rep1;
            } else {
                if (!bit(d, &d->is_rep_g2[s])) {
                    dist = d->rep2;
                } else {
                    dist = d->rep3;
                    d->rep3 = d->rep2;
                }
                d->rep2 = d->rep1;
            }
            d->rep1 = d->rep0;
            d->rep0 = dist;
        }
        len = len_decode(d, &d->rep_len_dec, ps);
        d->state = s < 7 ? 8u : 11u;
    }
    len += MATCH_MIN;
    if (d->rep0 >= d->dict_size || d->rep0 >= d->total || len > d->unpacked - d->total) {
        d->error = true;
        return;
    }
    d->pending = len;
}

bool lzma_dec_init(lzma_dec_t *d, uint8_t props, uint32_t dict_size, uint32_t unpacked,
                   lzma_in_fn in, void *in_ctx)
{
    memset(d, 0, sizeof(*d));
    d->in = in;
    d->in_ctx = in_ctx;
    d->unpacked = unpacked;
    d->dict_size = dict_size;
    d->lc = props % 9u;
    d->lp = (props / 9u) % 5u;
    d->pb = props / 45u;
    /* Powers of two from 4 KB (LZMA's minimum) up to the RAM window. */
    if (props >= 9u * 5u * 5u || d->lc + d->lp > LZMA_LCLP_MAX || dict_size < 4096u ||
        dict_size > LZMA_DICT_MAX || (dict_size & (dict_size - 1u)) != 0) {
        d->error = true;
        return false;
    }
    init_probs(d->is_match, sizeof(d->is_match) / sizeof(uint16_t));
    init_probs(d->is_rep, 12);
    init_probs(d->is_rep_g0, 12);
    init_probs(d->is_rep_g1, 12);
    init_probs(d->is_rep_g2, 12);
    init_probs(d->is_rep0_long, sizeof(d->is_rep0_long) / sizeof(uint16_t));
    init_probs(&d->pos_slot[0][0], sizeof(d->pos_slot) / sizeof(uint16_t));
    init_probs(d->pos_dec, sizeof(d->pos_dec) / sizeof(uint16_t));
    init_probs(d->align, sizeof(d->align) / sizeof(uint16_t));
    init_len(&d->len_dec);
    init_len(&d->rep_len_dec);
    init_probs(d->lit, sizeof(d->lit) / sizeof(uint16_t));
    d->range = 0xFFFFFFFFu;
    if (in_byte(d) != 0) d->error = true;
    for (int i = 0; i < 4; i++) d->code = (d->code << 8) | in_byte(d);
    if (d->code == d->range) d->error = true;
    return !d->error;
}

size_t lzma_dec_read(lzma_dec_t *d, uint8_t *out, size_t cap)
{
    size_t n = 0;
    while (n < cap && !d->error && d->total < d->unpacked) {
        if (d->pending) {
            uint8_t b = back(d, d->rep0 + 1u);
            put(d, b);
            out[n++] = b;
            d->pending--;
            continue;
        }
        uint32_t before = d->total;
        step(d);
        if (d->total != before && !d->error) out[n++] = back(d, 1);
    }
    return n;
}
