/*
 * Japanese adapter over anthy-unicode. The mapping is docs/anthy-mapping.md;
 * what makes this more than a shim:
 *
 *  - anthy holds N segments, each with its own candidate array, plus an
 *    active-segment index (Finding 1). The API exposes none of that: greedy
 *    left-to-right resolution over the leftmost unsettled segment, no
 *    segment navigation or resizing — the phone-keyboard target's call.
 *  - It is UTF-8 only after anthy_context_set_encoding(ANTHY_UTF8_ENCODING),
 *    and seg_len counts input reading xchars, not bytes (Finding 4).
 *  - It records a candidate choice only at anthy_commit_segment() time, so
 *    the currently-shown cursor is core's (Finding 2, candidates.cc).
 *  - It wants completed kana (Finding 6); the composing front end that turns
 *    key events into kana is romaji.*, above this adapter.
 *  - anthy_init() is process-global, and "personality" is the write-once
 *    trap that shaped pathime_init()'s data_dir; the auxiliary dictionaries
 *    were deferred for exactly that reason (TODO.md §1).
 */

#ifndef LIBPATHIME_SRC_ENGINES_ANTHY_BACKEND_H
#define LIBPATHIME_SRC_ENGINES_ANTHY_BACKEND_H

#include "backend.h"

namespace pathime {

/* Adapter to be defined once backend.h has its shape. */

}  // namespace pathime

#endif /* LIBPATHIME_SRC_ENGINES_ANTHY_BACKEND_H */
