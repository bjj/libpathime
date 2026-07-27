/*
 * Encoding boundaries (TODO.md §2, Finding 4). The API surface is UTF-8 with
 * every position in Unicode scalar values; each backend disagrees somewhere:
 * libhangul's composition API is UCS-4; anthy is UTF-8 only after
 * anthy_context_set_encoding() and its seg_len counts input reading xchars,
 * not bytes; pyzy is UTF-8 but its cursor() is a byte offset into the raw
 * ASCII input, never to be conflated with output scalar positions.
 *
 * This file owns the conversions (UCS-4 ⇄ UTF-8, byte offset ⇄ scalar index)
 * and the copy-on-return helpers: every string a backend hands back is
 * borrowed and volatile — valid only until the next mutating call — so it is
 * copied here, immediately, at the boundary.
 */

#ifndef LIBPATHIME_SRC_UTF8_H
#define LIBPATHIME_SRC_UTF8_H

#include <cstddef>
#include <cstdint>

namespace pathime {

/* Conversion and copy helpers to be defined. */

}  // namespace pathime

#endif /* LIBPATHIME_SRC_UTF8_H */
