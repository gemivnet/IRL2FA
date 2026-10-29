/* Constant-time comparison, and the markers the constant-time checker uses.
 *
 * firmware/verify/ct_check.c runs the secret-handling code under valgrind
 * memcheck with secrets marked "undefined" (the ctgrind technique): any
 * branch or memory index that depends on a secret is then reported. Values
 * that are secret-derived but deliberately made public (a public key, the
 * yes/no result of a MAC check, symbols shown on the screen) are marked with
 * CT_PUBLIC at the point they are published. Outside that build the markers
 * compile to nothing.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_CT_H
#define IRL2FA_CT_H

#include <stddef.h>
#include <stdint.h>

#ifdef IRL2FA_CTGRIND
#include <valgrind/memcheck.h>
#define CT_SECRET(p, n) ((void)VALGRIND_MAKE_MEM_UNDEFINED((p), (n)))
#define CT_PUBLIC(p, n) ((void)VALGRIND_MAKE_MEM_DEFINED((p), (n)))
#else
#define CT_SECRET(p, n) ((void)(p), (void)(n))
#define CT_PUBLIC(p, n) ((void)(p), (void)(n))
#endif

/* 1 if the n bytes at a and b are equal, else 0, in time that depends only
 * on n. The result itself is public. */
static inline int ct_equal(const void *a, const void *b, size_t n)
{
    const volatile uint8_t *x = (const volatile uint8_t *)a;
    const volatile uint8_t *y = (const volatile uint8_t *)b;
    uint32_t d = 0;
    for (size_t i = 0; i < n; i++) d |= (uint32_t)(x[i] ^ y[i]);
    int eq = (int)(1u & ((d - 1u) >> 8)); /* d is 0..255 */
    CT_PUBLIC(&eq, sizeof(eq));
    return eq;
}

#endif
