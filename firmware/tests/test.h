/* Minimal test harness.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_TEST_H
#define IRL2FA_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int test_failures;
extern int test_checks;

#define CHECK(cond)                                                              \
    do {                                                                         \
        test_checks++;                                                           \
        if (!(cond)) {                                                           \
            test_failures++;                                                     \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

#define CHECK_EQ_INT(a, b)                                                       \
    do {                                                                         \
        long long _a = (long long)(a), _b = (long long)(b);                      \
        test_checks++;                                                           \
        if (_a != _b) {                                                          \
            test_failures++;                                                     \
            fprintf(stderr, "%s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #a, _a, _b); \
        }                                                                        \
    } while (0)

#define CHECK_HEX(buf, len, hex) check_hex(__FILE__, __LINE__, #buf, buf, len, hex)

void check_hex(const char *file, int line, const char *what, const unsigned char *buf,
               size_t len, const char *hex);
size_t unhex(const char *hex, unsigned char *out);

typedef void (*test_fn)(void);
#define TEST(name) void name(void)

#endif
