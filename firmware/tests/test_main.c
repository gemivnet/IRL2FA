/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "test.h"

int test_failures;
int test_checks;

size_t unhex(const char *hex, unsigned char *out)
{
    size_t n = 0;
    while (hex[0] && hex[1]) {
        unsigned v;
        sscanf(hex, "%2x", &v);
        out[n++] = (unsigned char)v;
        hex += 2;
    }
    return n;
}

void check_hex(const char *file, int line, const char *what, const unsigned char *buf,
               size_t len, const char *hex)
{
    unsigned char want[256];
    size_t n = unhex(hex, want);
    test_checks++;
    if (n != len || memcmp(buf, want, n) != 0) {
        test_failures++;
        fprintf(stderr, "%s:%d: %s mismatch\n  got  ", file, line, what);
        for (size_t i = 0; i < len; i++) fprintf(stderr, "%02x", buf[i]);
        fprintf(stderr, "\n  want %s\n", hex);
    }
}

#define X(name) void name(void);
#include "tests.def"
#undef X

static const struct {
    const char *name;
    test_fn fn;
} TESTS[] = {
#define X(name) {#name, name},
#include "tests.def"
#undef X
};

int main(int argc, char **argv)
{
    for (size_t i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); i++) {
        if (argc > 1 && strstr(TESTS[i].name, argv[1]) == NULL) continue;
        int before = test_failures;
        TESTS[i].fn();
        printf("%-34s %s\n", TESTS[i].name, test_failures == before ? "ok" : "FAIL");
    }
    printf("\n%d checks, %d failures\n", test_checks, test_failures);
    return test_failures ? 1 : 0;
}
