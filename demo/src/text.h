/*
 * UTF-8 and terminal-width helpers.
 *
 * Two units meet in this program and must not be confused, which is the same
 * split <pathime/pathime.h> makes: the library counts *Unicode scalar values*
 * for every position and count (pathime_str_t::len being the one byte-denominated
 * quantity), while a terminal is laid out in *columns*, and a CJK character
 * occupies two of them. Nothing here mixes the three; each function says which
 * unit it is in.
 */

#ifndef PATHIME_DEMO_TEXT_H
#define PATHIME_DEMO_TEXT_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace demo {

/** Number of Unicode scalar values in @a s. Malformed bytes count as one each. */
std::size_t scalar_count(const std::string &s);

/**
 * Byte offset of scalar @a n, or s.size() if @a n is at or past the end. The
 * conversion the library never does for you: every offset it hands out is in
 * scalars, and every substring operation here is in bytes.
 */
std::size_t byte_offset_of_scalar(const std::string &s, std::size_t n);

/**
 * Terminal columns @a s occupies.
 *
 * Approximate by construction: a full grapheme-cluster and East Asian Width
 * implementation is a library of its own, and this program needs only enough
 * to keep a line from wrapping and a caret from drifting. Wide ranges are the
 * CJK and Hangul blocks the engines actually produce, combining marks are zero,
 * everything else is one.
 */
std::size_t display_width(const std::string &s);

/**
 * Byte offset of the longest suffix of @a s that fits in @a columns.
 *
 * How the document line scrolls: it shows its tail, so the caret is always at
 * the right-hand end and no wrapping or scroll state is needed.
 */
std::size_t tail_start_byte(const std::string &s, std::size_t columns);

/**
 * Clip @a styled to @a columns, counting only what the terminal will draw.
 *
 * ANSI escape sequences occupy no columns, so lines are assembled with colour
 * and style codes already in them and clipped here rather than being tracked
 * as (text, style) pairs. A clipped line is closed with a style reset, since
 * the cut may have landed inside a styled run.
 */
std::string clip_to_width(const std::string &styled, std::size_t columns);

/** @a s padded with spaces to @a columns, or returned unchanged if wider. */
std::string pad_to_width(const std::string &s, std::size_t columns);

/**
 * Erase @a count scalars starting at scalar @a start. Out-of-range requests
 * are clamped rather than rejected: this is what the demo does with a
 * delete_surrounding_text it has decided to honour.
 */
void erase_scalars(std::string *s, std::size_t start, std::size_t count);

/**
 * The last @a count scalars of @a s, or all of it when it is shorter.
 *
 * For supplying a deliberately short surrounding-text fragment. The result is
 * a suffix, which is what makes the demo's delete_surrounding_text arithmetic
 * work for a fragment and for a whole document alike: both end at the caret.
 */
std::string last_scalars(const std::string &s, std::size_t count);

/**
 * @a text with control characters replaced by a visible escape, for showing a
 * string inside the event log. Quotes are not added.
 */
std::string escape_for_log(const std::string &text);

/** One Unicode scalar as UTF-8. */
std::string utf8_encode(std::uint32_t scalar);

}  // namespace demo

#endif /* PATHIME_DEMO_TEXT_H */
