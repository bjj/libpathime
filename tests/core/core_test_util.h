/*
 * Minimal assertion helpers shared by the internal-core tests. Deliberately not
 * a test framework, and deliberately a near-copy of tests/api/api_test_util.h:
 * that file is C11 because the API tests exist partly to prove the public
 * header is consumable from strict C, and this one is C++17 because its
 * subjects — src/utf8.h, src/options.h — are internal C++ that a C client can
 * never see.
 *
 * The two suites differ in what they link, and that is the whole distinction.
 * tests/api links the built library and touches only exported symbols.
 * tests/core compiles the internal sources it needs directly into each test
 * executable, because internal helpers carry no PATHIME_API and would not be
 * exported from a shared build.
 *
 * Everything is `static` because each test program is a single translation
 * unit; there is no test library to link.
 */
#ifndef LIBPATHIME_CORE_TEST_UTIL_H
#define LIBPATHIME_CORE_TEST_UTIL_H

#include <cstdio>
#include <string>

static int pt_failures = 0;
static int pt_checks = 0;

#define PT_FAILF(...)                                         \
    do {                                                      \
        std::fprintf(stderr, "%s:%d: FAIL: ", __FILE__, __LINE__); \
        std::fprintf(stderr, __VA_ARGS__);                    \
        std::fputc('\n', stderr);                             \
        pt_failures++;                                        \
    } while (0)

#define PT_CHECK(cond)                                        \
    do {                                                      \
        pt_checks++;                                          \
        if (!(cond))                                          \
            PT_FAILF("%s", #cond);                            \
    } while (0)

/* Compare two size_t values, reporting both sides. */
#define PT_CHECK_SIZE(expr, expected)                                        \
    do {                                                                     \
        const size_t pt_got_ = (expr);                                       \
        const size_t pt_want_ = (expected);                                  \
        pt_checks++;                                                         \
        if (pt_got_ != pt_want_)                                             \
            PT_FAILF("%s: got %zu, expected %zu", #expr, pt_got_, pt_want_); \
    } while (0)

/* Compare a std::string against a literal, reporting both sides. */
#define PT_CHECK_STR(expr, expected)                                          \
    do {                                                                      \
        const std::string pt_got_ = (expr);                                   \
        const std::string pt_want_ = (expected);                              \
        pt_checks++;                                                          \
        if (pt_got_ != pt_want_)                                              \
            PT_FAILF("%s: got \"%s\", expected \"%s\"", #expr,                \
                     pt_got_.c_str(), pt_want_.c_str());                      \
    } while (0)

/* Common main() tail: one line of summary, conventional exit status. */
static int pt_report(const char *name)
{
    if (pt_failures == 0) {
        std::printf("%s: %d checks passed\n", name, pt_checks);
        return 0;
    }
    std::printf("%s: %d of %d checks FAILED\n", name, pt_failures, pt_checks);
    return 1;
}

#endif /* LIBPATHIME_CORE_TEST_UTIL_H */
