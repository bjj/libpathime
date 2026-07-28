/*
 * Minimal assertion helpers shared by the libpathime-authored libhangul tests.
 *
 * This is deliberately not a test framework: three counters, two macros and a
 * UCS-4 compare. It exists because the one thing these tests must never do is
 * borrow the host's wide-character machinery.
 *
 * libhangul's public API speaks `ucschar`, a typedef for uint32_t (hangul.h).
 * Upstream's own suite (engines/libhangul/test/test.c) compares those buffers with
 *
 *     wcscmp((const wchar_t*)hangul_ic_get_preedit_string(ic), L"...")
 *
 * which is only correct where wchar_t happens to be 32 bits. On Windows
 * wchar_t is 16 bits, so that cast reinterprets each UCS-4 code point as two
 * UTF-16 code units; for BMP characters on a little-endian machine the second
 * unit is zero, so wcscmp sees a one-character string and silently agrees with
 * almost anything. Every comparison here therefore goes through ucs4_equal(),
 * which walks uint32_t elements and is identical on both platforms.
 *
 * Everything is `static` because each test program is a single translation
 * unit; there is no test library to link.
 */
#ifndef LIBPATHIME_HANGUL_TEST_UTIL_H
#define LIBPATHIME_HANGUL_TEST_UTIL_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <hangul.h>

/* Guard the assumption the whole file rests on. A C11 _Static_assert is
 * available on every compiler this build supports (GCC, Clang, clang-cl and
 * MSVC 2019+ in /std:c11 mode). */
_Static_assert(sizeof(ucschar) == 4, "libhangul's ucschar must be 32 bits");

static int ht_failures = 0;
static int ht_checks = 0;

/* Build a NUL-terminated ucschar literal inline:  UCS(0xAC00, 0xB098)
 * A compound literal at block scope lives until the end of the enclosing
 * block, which outlives the call it is passed to. */
#define UCS(...) ((const ucschar[]){ __VA_ARGS__, 0u })
#define UCS_EMPTY UCS(0u) /* leading terminator == the empty string */

#define HT_FAILF(...)                                     \
    do {                                                  \
        fprintf(stderr, "%s:%d: FAIL: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__);                     \
        fputc('\n', stderr);                              \
        ht_failures++;                                    \
    } while (0)

#define HT_CHECK(cond)                                    \
    do {                                                  \
        ht_checks++;                                      \
        if (!(cond))                                      \
            HT_FAILF("%s", #cond);                        \
    } while (0)

/* Compare two ucschar strings, reporting both in code points on mismatch. */
#define HT_CHECK_UCS(what, actual, expected)                                  \
    do {                                                                      \
        ht_checks++;                                                          \
        if (!ucs4_equal((actual), (expected))) {                              \
            fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, (what)); \
            ht_print_ucs("  expected", (expected));                           \
            ht_print_ucs("  actual  ", (actual));                             \
            ht_failures++;                                                    \
        }                                                                     \
    } while (0)

static size_t ucs4_len(const ucschar* s)
{
    size_t n = 0;
    if (s == NULL)
        return 0;
    while (s[n] != 0)
        n++;
    return n;
}

static int ucs4_equal(const ucschar* a, const ucschar* b)
{
    if (a == NULL || b == NULL)
        return a == b;
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void ht_print_ucs(const char* label, const ucschar* s)
{
    size_t i;
    fprintf(stderr, "%s: [", label);
    if (s == NULL) {
        fprintf(stderr, "NULL]\n");
        return;
    }
    for (i = 0; s[i] != 0; i++)
        fprintf(stderr, "%sU+%04X", i ? " " : "", (unsigned)s[i]);
    fprintf(stderr, "]\n");
}

/* Common main() tail: one line of summary, conventional exit status. */
static int ht_report(const char* name)
{
    if (ht_failures == 0) {
        printf("%s: %d checks passed\n", name, ht_checks);
        return 0;
    }
    printf("%s: %d of %d checks FAILED\n", name, ht_failures, ht_checks);
    return 1;
}

#endif /* LIBPATHIME_HANGUL_TEST_UTIL_H */
