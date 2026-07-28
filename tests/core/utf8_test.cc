/*
 * src/utf8.cc — the encoding boundary (docs/design-history.md §2, Finding 4).
 *
 * This is the one piece of the library every backend adapter will lean on, and
 * the one where a quietly wrong answer does not announce itself: an overlong
 * form accepted here becomes a malformed slice handed to a client later, and a
 * byte offset mistaken for a scalar position silently moves a cursor. So the
 * rejection cases are tested at least as hard as the acceptance ones.
 *
 * Test data is deliberately drawn from the scripts the library actually
 * handles — Hangul syllables, kana, Han — rather than from Latin-1 accents,
 * because three-byte sequences are the common case here and two-byte ones are
 * the exception.
 */

#include "core_test_util.h"

#include "utf8.h"

using pathime::kUtf8NoPosition;

namespace {

/* 한글 — three Hangul syllables, U+D55C U+AE00, three bytes each. */
const char kHangul[] = "\xED\x95\x9C\xEA\xB8\x80";

/* かな — two hiragana, U+304B U+306A, three bytes each. */
const char kKana[] = "\xE3\x81\x8B\xE3\x81\xAA";

/* "a中𝄞" — one, three and four bytes: U+0061, U+4E2D, U+1D11E. */
const char kMixed[] = "\x61\xE4\xB8\xAD\xF0\x9D\x84\x9E";

void test_validate_accepts()
{
    size_t scalars = 0;

    PT_CHECK(pathime::utf8_validate("", 0, &scalars));
    PT_CHECK_SIZE(scalars, 0);

    PT_CHECK(pathime::utf8_validate("abc", 3, &scalars));
    PT_CHECK_SIZE(scalars, 3);

    PT_CHECK(pathime::utf8_validate(kHangul, 6, &scalars));
    PT_CHECK_SIZE(scalars, 2);

    PT_CHECK(pathime::utf8_validate(kKana, 6, &scalars));
    PT_CHECK_SIZE(scalars, 2);

    /* One scalar of each sequence length in one string: 1 + 3 + 4 bytes. */
    PT_CHECK(pathime::utf8_validate(kMixed, 8, &scalars));
    PT_CHECK_SIZE(scalars, 3);

    /* The two-byte case, which the CJK data above never exercises: U+00E9. */
    PT_CHECK(pathime::utf8_validate("\xC3\xA9", 2, &scalars));
    PT_CHECK_SIZE(scalars, 1);

    /* out_scalars is optional when only validity is wanted. */
    PT_CHECK(pathime::utf8_validate(kHangul, 6, nullptr));

    /* A null pointer is coherent only with a zero length. */
    PT_CHECK(pathime::utf8_validate(nullptr, 0, &scalars));
    PT_CHECK_SIZE(scalars, 0);
    PT_CHECK(!pathime::utf8_validate(nullptr, 4, &scalars));

    /* The boundary scalars either side of the surrogate block, and the last
     * scalar value there is. */
    PT_CHECK(pathime::utf8_validate("\xED\x9F\xBF", 3, nullptr));      /* U+D7FF */
    PT_CHECK(pathime::utf8_validate("\xEE\x80\x80", 3, nullptr));      /* U+E000 */
    PT_CHECK(pathime::utf8_validate("\xF4\x8F\xBF\xBF", 4, nullptr));  /* U+10FFFF */
}

void test_validate_rejects()
{
    /*
     * U+0000 is not representable in this API in either direction, so a NUL
     * inside a slice is malformed input and not an early terminator. This is
     * what lets every slice the library produces be NUL-terminated while its
     * len stays authoritative.
     */
    PT_CHECK(!pathime::utf8_validate("a\0b", 3, nullptr));

    /* Structural errors. */
    PT_CHECK(!pathime::utf8_validate("\x80", 1, nullptr));          /* stray continuation */
    PT_CHECK(!pathime::utf8_validate("\xBF", 1, nullptr));          /* stray continuation */
    PT_CHECK(!pathime::utf8_validate("\xF8\x88\x80\x80\x80", 5, nullptr));  /* 5-byte lead */
    PT_CHECK(!pathime::utf8_validate("\xFE", 1, nullptr));          /* not a lead at all */
    PT_CHECK(!pathime::utf8_validate("\xE3\x81", 2, nullptr));      /* truncated 3-byte */
    PT_CHECK(!pathime::utf8_validate("\xF0\x9D\x84", 3, nullptr));  /* truncated 4-byte */
    PT_CHECK(!pathime::utf8_validate("\xE3\x81\x41", 3, nullptr));  /* bad continuation */

    /* Truncation detected by the length rather than by content: the first
     * scalar of kHangul needs three bytes and only two are offered. */
    PT_CHECK(!pathime::utf8_validate(kHangul, 2, nullptr));

    /*
     * Overlong forms — the classic way to smuggle a NUL or an ASCII delimiter
     * past a naive check. One per sequence length, each encoding a value that
     * has a shorter legal form.
     */
    PT_CHECK(!pathime::utf8_validate("\xC0\x80", 2, nullptr));              /* U+0000 as 2 */
    PT_CHECK(!pathime::utf8_validate("\xC1\xBF", 2, nullptr));              /* U+007F as 2 */
    PT_CHECK(!pathime::utf8_validate("\xE0\x80\x80", 3, nullptr));          /* U+0000 as 3 */
    PT_CHECK(!pathime::utf8_validate("\xE0\x9F\xBF", 3, nullptr));          /* U+07FF as 3 */
    PT_CHECK(!pathime::utf8_validate("\xF0\x80\x80\x80", 4, nullptr));      /* U+0000 as 4 */
    PT_CHECK(!pathime::utf8_validate("\xF0\x8F\xBF\xBF", 4, nullptr));      /* U+FFFF as 4 */

    /* Above U+10FFFF. */
    PT_CHECK(!pathime::utf8_validate("\xF4\x90\x80\x80", 4, nullptr));  /* U+110000 */
    PT_CHECK(!pathime::utf8_validate("\xF7\xBF\xBF\xBF", 4, nullptr));  /* U+1FFFFF */

    /*
     * Surrogate halves are not scalar values, whatever encodes them. Both ends
     * of the block, plus the CESU-8 pair that some Java and Windows tooling
     * emits for an astral character — accepting that would mean handing a
     * client text no conforming decoder can read.
     */
    PT_CHECK(!pathime::utf8_validate("\xED\xA0\x80", 3, nullptr));  /* U+D800 */
    PT_CHECK(!pathime::utf8_validate("\xED\xBF\xBF", 3, nullptr));  /* U+DFFF */
    PT_CHECK(!pathime::utf8_validate("\xED\xA0\xBD\xED\xB8\x80", 6, nullptr));  /* CESU-8 */
}

void test_validate_z()
{
    size_t scalars = 0;

    PT_CHECK(pathime::utf8_validate_z("", &scalars));
    PT_CHECK_SIZE(scalars, 0);

    PT_CHECK(pathime::utf8_validate_z(kHangul, &scalars));
    PT_CHECK_SIZE(scalars, 2);

    PT_CHECK(pathime::utf8_validate_z(kMixed, &scalars));
    PT_CHECK_SIZE(scalars, 3);

    PT_CHECK(!pathime::utf8_validate_z(nullptr, &scalars));

    /*
     * A terminator inside a multi-byte sequence must read as truncation, not
     * as a successful parse of a shorter string and never as a read past the
     * NUL. "\xE3\x81" is the first two bytes of か.
     */
    PT_CHECK(!pathime::utf8_validate_z("\xE3\x81", &scalars));

    /* The slice form and the terminated form must agree on the same bytes. */
    size_t via_slice = 0;
    size_t via_z = 0;
    PT_CHECK(pathime::utf8_validate(kKana, 6, &via_slice));
    PT_CHECK(pathime::utf8_validate_z(kKana, &via_z));
    PT_CHECK_SIZE(via_slice, via_z);
}

void test_scalar_count()
{
    PT_CHECK_SIZE(pathime::utf8_scalar_count("", 0), 0);
    PT_CHECK_SIZE(pathime::utf8_scalar_count("abc", 3), 3);
    PT_CHECK_SIZE(pathime::utf8_scalar_count(kHangul, 6), 2);
    PT_CHECK_SIZE(pathime::utf8_scalar_count(kMixed, 8), 3);

    /* It must agree with the validator on every string the validator accepts —
     * they are two implementations of the same count and the cheap one is used
     * on the hot path. */
    size_t counted = 0;
    PT_CHECK(pathime::utf8_validate(kMixed, 8, &counted));
    PT_CHECK_SIZE(pathime::utf8_scalar_count(kMixed, 8), counted);
}

void test_byte_offset()
{
    /* kMixed is U+0061 (1 byte), U+4E2D (3 bytes), U+1D11E (4 bytes). */
    PT_CHECK_SIZE(pathime::utf8_byte_offset(kMixed, 8, 0), 0);
    PT_CHECK_SIZE(pathime::utf8_byte_offset(kMixed, 8, 1), 1);
    PT_CHECK_SIZE(pathime::utf8_byte_offset(kMixed, 8, 2), 4);

    /*
     * One past the last scalar is a legal position, not an error: a cursor
     * sitting after the final character is the common case, and every API
     * function that takes a position accepts it.
     */
    PT_CHECK_SIZE(pathime::utf8_byte_offset(kMixed, 8, 3), 8);
    PT_CHECK_SIZE(pathime::utf8_byte_offset(kMixed, 8, 4), kUtf8NoPosition);
    PT_CHECK_SIZE(pathime::utf8_byte_offset("", 0, 0), 0);
    PT_CHECK_SIZE(pathime::utf8_byte_offset("", 0, 1), kUtf8NoPosition);

    PT_CHECK_SIZE(pathime::utf8_byte_offset(kHangul, 6, 1), 3);
    PT_CHECK_SIZE(pathime::utf8_byte_offset(kHangul, 6, 2), 6);
}

void test_scalar_index()
{
    PT_CHECK_SIZE(pathime::utf8_scalar_index(kMixed, 8, 0), 0);
    PT_CHECK_SIZE(pathime::utf8_scalar_index(kMixed, 8, 1), 1);
    PT_CHECK_SIZE(pathime::utf8_scalar_index(kMixed, 8, 4), 2);
    PT_CHECK_SIZE(pathime::utf8_scalar_index(kMixed, 8, 8), 3);

    /*
     * A byte offset inside a multi-byte sequence names no scalar position.
     * Rounding to a neighbour would silently move a cursor, so it is refused —
     * this is the guard that matters when a backend hands back a byte offset,
     * as pyzy's cursor() does.
     */
    PT_CHECK_SIZE(pathime::utf8_scalar_index(kMixed, 8, 2), kUtf8NoPosition);
    PT_CHECK_SIZE(pathime::utf8_scalar_index(kMixed, 8, 3), kUtf8NoPosition);
    PT_CHECK_SIZE(pathime::utf8_scalar_index(kMixed, 8, 5), kUtf8NoPosition);

    /* Past the end. */
    PT_CHECK_SIZE(pathime::utf8_scalar_index(kMixed, 8, 9), kUtf8NoPosition);

    /* Round-trip: every scalar position converts to a byte offset and back. */
    for (size_t i = 0; i <= 3; ++i) {
        const size_t offset = pathime::utf8_byte_offset(kMixed, 8, i);
        PT_CHECK(offset != kUtf8NoPosition);
        PT_CHECK_SIZE(pathime::utf8_scalar_index(kMixed, 8, offset), i);
    }
}

void test_append_scalar()
{
    std::string out;

    /* One either side of each sequence-length boundary. */
    struct {
        uint32_t scalar;
        const char *utf8;
    } cases[] = {
        {0x0041u, "\x41"},                  /* 'A' */
        {0x007Fu, "\x7F"},                  /* last 1-byte */
        {0x0080u, "\xC2\x80"},              /* first 2-byte */
        {0x07FFu, "\xDF\xBF"},              /* last 2-byte */
        {0x0800u, "\xE0\xA0\x80"},          /* first 3-byte */
        {0xD7FFu, "\xED\x9F\xBF"},          /* last before surrogates */
        {0xE000u, "\xEE\x80\x80"},          /* first after surrogates */
        {0xFFFFu, "\xEF\xBF\xBF"},          /* last 3-byte */
        {0x10000u, "\xF0\x90\x80\x80"},     /* first 4-byte */
        {0x10FFFFu, "\xF4\x8F\xBF\xBF"},    /* last scalar value */
        {0xD55Cu, "\xED\x95\x9C"},          /* 한 */
        {0x304Bu, "\xE3\x81\x8B"},          /* か */
    };

    for (const auto &c : cases) {
        out.clear();
        PT_CHECK(pathime::utf8_append_scalar(out, c.scalar));
        PT_CHECK_STR(out, c.utf8);
        /* Everything it produces must survive the validator — the two halves of
         * the boundary have to agree. */
        PT_CHECK(pathime::utf8_validate(out.data(), out.size(), nullptr));
    }

    /* Rejections append nothing, so a caller that ignores the result does not
     * end up with a half-written string. */
    for (uint32_t bad : {0x0u, 0xD800u, 0xDFFFu, 0x110000u, 0xFFFFFFFFu}) {
        out = "keep";
        PT_CHECK(!pathime::utf8_append_scalar(out, bad));
        PT_CHECK_STR(out, "keep");
    }

    /* It appends rather than replaces. */
    out = "a";
    PT_CHECK(pathime::utf8_append_scalar(out, 0xD55Cu));
    PT_CHECK_STR(out, "a\xED\x95\x9C");
}

void test_from_ucs4()
{
    std::string out;

    /* The libhangul shape: ucschar is uint32_t and the preedit comes back as a
     * NUL-terminated array. 한글 is U+D55C U+AE00. */
    const uint32_t hangul[] = {0xD55Cu, 0xAE00u, 0u};
    PT_CHECK(pathime::utf8_from_ucs4_z(hangul, &out));
    PT_CHECK_STR(out, kHangul);

    PT_CHECK(pathime::utf8_from_ucs4(hangul, 2, &out));
    PT_CHECK_STR(out, kHangul);

    /* The length form must not stop at an embedded terminator the caller did
     * not intend, and the terminated form must stop at one that it did. */
    PT_CHECK(pathime::utf8_from_ucs4(hangul, 1, &out));
    PT_CHECK_STR(out, "\xED\x95\x9C");

    /* An empty composition, spelled both ways. libhangul is entitled to return
     * a null pointer for a context with nothing in it. */
    PT_CHECK(pathime::utf8_from_ucs4_z(nullptr, &out));
    PT_CHECK_STR(out, "");
    const uint32_t empty[] = {0u};
    PT_CHECK(pathime::utf8_from_ucs4_z(empty, &out));
    PT_CHECK_STR(out, "");
    PT_CHECK(pathime::utf8_from_ucs4(nullptr, 0, &out));
    PT_CHECK_STR(out, "");
    PT_CHECK(!pathime::utf8_from_ucs4(nullptr, 2, &out));

    /*
     * All or nothing. A partially converted string handed onward would be
     * indistinguishable from a legitimately shorter one, so a bad unit anywhere
     * clears what was written rather than truncating there.
     */
    const uint32_t bad_tail[] = {0xD55Cu, 0xD800u, 0u};
    PT_CHECK(!pathime::utf8_from_ucs4_z(bad_tail, &out));
    PT_CHECK_STR(out, "");
    const uint32_t too_big[] = {0x110000u, 0u};
    PT_CHECK(!pathime::utf8_from_ucs4_z(too_big, &out));
    PT_CHECK_STR(out, "");

    /* Everything it produces must validate. */
    const uint32_t mixed[] = {0x61u, 0x4E2Du, 0x1D11Eu, 0u};
    PT_CHECK(pathime::utf8_from_ucs4_z(mixed, &out));
    PT_CHECK_STR(out, kMixed);
    PT_CHECK(pathime::utf8_validate(out.data(), out.size(), nullptr));
}

}  // namespace

int main()
{
    test_validate_accepts();
    test_validate_rejects();
    test_validate_z();
    test_scalar_count();
    test_byte_offset();
    test_scalar_index();
    test_append_scalar();
    test_from_ucs4();
    return pt_report("core.utf8");
}
