/* Minimal assertion helpers for the pyzy tests.
 *
 * Deliberately not a framework: three macros, a failure counter, and a main()
 * epilogue. pyzy's own suite uses glib's g_assert_* family, which aborts on the
 * first mismatch; these keep going instead, because when a Windows/Linux
 * divergence shows up we want the whole list of what differs, not just the
 * first item.
 *
 * String mismatches print the bytes in hex as well as verbatim. Nearly
 * everything pyzy returns is UTF-8 Chinese, and the failure modes we are
 * hunting (a wchar_t/UCS-4 confusion, a source file decoded as the host ANSI
 * codepage) produce output that a Windows console renders as indistinguishable
 * mojibake either way. The hex is the part you can actually reason about.
 */
#ifndef LIBPATHIME_TESTS_PYZY_ASSERT_H
#define LIBPATHIME_TESTS_PYZY_ASSERT_H

#include <cstdio>
#include <string>

namespace pyzy_test {

inline int & failures ()
{
    static int count = 0;
    return count;
}

inline std::string hex (const std::string & s)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (std::string::size_type i = 0; i < s.size (); ++i) {
        const unsigned char c = static_cast<unsigned char> (s[i]);
        if (i) out += ' ';
        out += digits[c >> 4];
        out += digits[c & 0xf];
    }
    return out.empty () ? std::string ("<empty>") : out;
}

inline void fail (const char *file, int line, const std::string &message)
{
    std::fprintf (stderr, "%s:%d: FAIL: %s\n", file, line, message.c_str ());
    ++failures ();
}

inline void expectTrue (const char *file, int line, const char *expr, bool value)
{
    if (!value)
        fail (file, line, std::string ("expected true: ") + expr);
}

inline void expectStr (const char *file, int line, const char *expr,
                       const std::string &actual, const std::string &expected)
{
    if (actual == expected)
        return;
    fail (file, line,
          std::string (expr) + "\n"
          "         actual: \"" + actual   + "\"  [" + hex (actual)   + "]\n"
          "       expected: \"" + expected + "\"  [" + hex (expected) + "]");
}

inline void expectInt (const char *file, int line, const char *expr,
                       long long actual, long long expected)
{
    if (actual == expected)
        return;
    char buf[128];
    std::snprintf (buf, sizeof (buf), " (actual %lld, expected %lld)",
                   actual, expected);
    fail (file, line, std::string (expr) + buf);
}

}  // namespace pyzy_test

#define PYZY_EXPECT(cond) \
    ::pyzy_test::expectTrue (__FILE__, __LINE__, #cond, (cond))
#define PYZY_EXPECT_STR(actual, expected) \
    ::pyzy_test::expectStr (__FILE__, __LINE__, #actual, (actual), (expected))
#define PYZY_EXPECT_INT(actual, expected) \
    ::pyzy_test::expectInt (__FILE__, __LINE__, #actual, (actual), (expected))

/* Use as `return PYZY_TEST_RESULT;` at the end of main(). */
#define PYZY_TEST_RESULT (::pyzy_test::failures () == 0 ? 0 : 1)

#endif  /* LIBPATHIME_TESTS_PYZY_ASSERT_H */
