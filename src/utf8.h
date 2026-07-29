/*
 * Encoding boundaries. The API surface is UTF-8 with
 * every position in Unicode scalar values; each backend disagrees somewhere:
 * libhangul's composition API is UCS-4; anthy is UTF-8 only after
 * anthy_context_set_encoding() and its seg_len counts input reading xchars,
 * not bytes; pyzy is UTF-8 but its cursor() is a byte offset into the raw
 * ASCII input, never to be conflated with output scalar positions.
 *
 * This file owns the conversions (UCS-4 to UTF-8, byte offset to scalar index
 * and back) and the copy-on-return helpers: every string a backend hands back
 * is borrowed and volatile — valid only until the next mutating call — so it
 * is copied here, immediately, at the boundary.
 *
 * Two rules hold throughout, and they are the API's rather than this file's
 * inventions:
 *
 *  - A scalar value is a Unicode scalar value: U+0001..U+D7FF and
 *    U+E000..U+10FFFF. Surrogate halves are not scalar values and never
 *    validate, whatever encodes them.
 *  - U+0000 is not representable in this API in either direction, so a NUL
 *    byte inside a slice is invalid rather than a terminator. Enforcing that
 *    here is what lets every pathime_str_t the library produces be
 *    NUL-terminated while its len stays authoritative.
 */

#ifndef LIBPATHIME_SRC_UTF8_H
#define LIBPATHIME_SRC_UTF8_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace pathime {

/** Returned by the position conversions for an argument that is out of range. */
constexpr size_t kUtf8NoPosition = static_cast<size_t>(-1);

/**
 * Validate @a len bytes of UTF-8 and count the scalar values in them.
 *
 * Rejects everything that is not well-formed UTF-8 encoding a run of scalar
 * values: stray continuation bytes, five-byte leads, truncated sequences,
 * overlong forms, values above U+10FFFF, surrogate halves, and embedded NUL.
 * @a bytes may be nullptr only when @a len is 0.
 *
 * @param out_scalars May be nullptr when only validity is wanted. Written only
 *                    on success.
 *
 * This is the strict form, used at every client boundary, and it is strict on
 * purpose: text arriving from a client is the one input the library cannot
 * vouch for, and an overlong or surrogate form quietly accepted here becomes a
 * malformed slice handed to some other client later.
 */
bool utf8_validate(const char *bytes, size_t len, size_t *out_scalars);

/** utf8_validate() for a NUL-terminated string. @a text must not be nullptr. */
bool utf8_validate_z(const char *text, size_t *out_scalars);

/**
 * The number of scalar values in @a len bytes, which must already be valid
 * UTF-8. Counts lead bytes without decoding, so it is cheap; behaviour on
 * invalid input is unspecified rather than diagnosed — validate first.
 */
size_t utf8_scalar_count(const char *bytes, size_t len);

/**
 * The byte offset at which the scalar value numbered @a scalar_index begins.
 *
 * Accepts @a scalar_index equal to the total count and answers @a len, because
 * a position may legitimately sit at the end of the text — a cursor after the
 * last character is the common case. Anything beyond that is kUtf8NoPosition.
 *
 * This conversion is a named function rather than arithmetic at each call site
 * for the reason the public header gives: every position the API carries is in
 * scalar values while every buffer it carries is sized in bytes, so the two
 * are never interchangeable, and a conversion that is spelled out cannot be
 * forgotten.
 */
size_t utf8_byte_offset(const char *bytes, size_t len, size_t scalar_index);

/**
 * The inverse: how many scalar values precede @a byte_offset. Requires
 * @a byte_offset to be within @a len and on a sequence boundary; a value
 * inside a multi-byte sequence, or past the end, is kUtf8NoPosition.
 *
 * pyzy's cursor() is a byte offset, and this is what would turn one into a
 * position the API can carry — though only ever against pyzy's own UTF-8
 * output, never against its raw ASCII input, which is a different string.
 */
size_t utf8_scalar_index(const char *bytes, size_t len, size_t byte_offset);

/**
 * Decode the scalar value beginning at byte @a offset, advancing @a offset past
 * it. Returns false at the end of the text or on any sequence the validators
 * reject, leaving @a offset untouched.
 *
 * The counterpart to utf8_append_scalar(), added for the table engine, which is
 * the first backend to take *text* as data rather than as something to hand to
 * a vendor library: VALID_INPUT_CHARS, START_CHARS and the char-prompt keys are
 * all sets of scalars written as UTF-8, and membership in them is asked once per
 * key press. It applies the same strictness as utf8_validate() because it is the
 * same decoder — overlongs, surrogates and truncation are rejected here too.
 */
bool utf8_next_scalar(const char *bytes, size_t len, size_t *offset, uint32_t *out_scalar);

/**
 * Append one scalar value to @a out as UTF-8. A surrogate half, a value above
 * U+10FFFF, or U+0000 appends nothing and returns false.
 */
bool utf8_append_scalar(std::string &out, uint32_t scalar);

/**
 * Convert @a len UCS-4 code units to UTF-8, replacing @a out. Returns false
 * and leaves @a out empty if any unit is not a scalar value.
 *
 * This is the libhangul boundary: its composition API hands back ucschar
 * (uint32_t) arrays and everything we hand out is UTF-8.
 */
bool utf8_from_ucs4(const uint32_t *units, size_t len, std::string *out);

/**
 * The same for a NUL-terminated UCS-4 string, which is the form libhangul
 * actually returns from hangul_ic_get_preedit_string() and
 * hangul_ic_get_commit_string(). A nullptr @a units yields an empty string and
 * succeeds — libhangul is entitled to return one for an empty composition.
 */
bool utf8_from_ucs4_z(const uint32_t *units, std::string *out);

}  // namespace pathime

#endif /* LIBPATHIME_SRC_UTF8_H */
