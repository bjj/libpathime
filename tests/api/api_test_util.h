/*
 * Minimal assertion helpers shared by the API-surface tests. Deliberately not
 * a test framework, and deliberately C11: these tests are the proof that
 * <pathime/pathime.h> is consumable from strict C, so nothing here may lean
 * on anything the header does not already require.
 *
 * Everything is `static` because each test program is a single translation
 * unit; there is no test library to link. Mirrors tests/hangul's
 * hangul_test_util.h.
 */
#ifndef LIBPATHIME_API_TEST_UTIL_H
#define LIBPATHIME_API_TEST_UTIL_H

#include <stdio.h>

#include <pathime/pathime.h>

static int pt_failures = 0;
static int pt_checks = 0;

#define PT_FAILF(...)                                         \
    do {                                                      \
        fprintf(stderr, "%s:%d: FAIL: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__);                         \
        fputc('\n', stderr);                                  \
        pt_failures++;                                        \
    } while (0)

#define PT_CHECK(cond)                                        \
    do {                                                      \
        pt_checks++;                                          \
        if (!(cond))                                          \
            PT_FAILF("%s", #cond);                            \
    } while (0)

/* Check a pathime_status_t result, reporting both sides by name. */
#define PT_CHECK_STATUS(expr, expected)                            \
    do {                                                           \
        pathime_status_t pt_got_ = (expr);                         \
        pt_checks++;                                               \
        if (pt_got_ != (expected))                                 \
            PT_FAILF("%s: got \"%s\", expected \"%s\"", #expr,     \
                     pathime_status_string(pt_got_),               \
                     pathime_status_string(expected));             \
    } while (0)

/* Common main() tail: one line of summary, conventional exit status. */
static int pt_report(const char *name)
{
    if (pt_failures == 0) {
        printf("%s: %d checks passed\n", name, pt_checks);
        return 0;
    }
    printf("%s: %d of %d checks FAILED\n", name, pt_failures, pt_checks);
    return 1;
}

/*
 * Exit status that ctest reports as "skipped" (SKIP_RETURN_CODE in
 * tests/api/CMakeLists.txt). For tests registered ahead of the code they will
 * exercise; the printed reason keeps a skipped run self-explaining.
 */
#define LIBPATHIME_TEST_SKIP 77

static int pt_skip(const char *name, const char *why)
{
    printf("%s: SKIPPED — %s\n", name, why);
    return LIBPATHIME_TEST_SKIP;
}

/* Compare two size_t for equality */
#define PT_CHECK_SIZE(expr, expected)                                        \
    do {                                                                     \
        const size_t pt_got_sz_ = (size_t)(expr);                            \
        const size_t pt_want_sz_ = (size_t)(expected);                       \
        pt_checks++;                                                         \
        if (pt_got_sz_ != pt_want_sz_)                                       \
            PT_FAILF("%s: got %zu, expected %zu", #expr,                     \
                     pt_got_sz_, pt_want_sz_);                               \
    } while (0)

#endif /* LIBPATHIME_API_TEST_UTIL_H */
