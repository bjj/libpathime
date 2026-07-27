/*
 * Korean adapter over libhangul. The mapping is docs/libhangul-mapping.md;
 * what makes this more than a shim:
 *
 *  - libhangul exposes only the trailing mutable syllable, so the settled
 *    prefix of the preedit is accumulated on this side (Finding 1).
 *  - Its composition API is UCS-4; everything crossing backend.h is UTF-8
 *    (utf8.h owns the conversion).
 *  - Input is a plain US-QWERTY int — uppercase means Shift, no modifiers,
 *    no releases — and backspace is the separate hangul_ic_backspace()
 *    (Finding 6). layout_key maps onto this directly: keycodes matter to
 *    hangul only as key *position*.
 *  - Hangul produces no candidates at all: hanja conversion was cut in the
 *    API review, so PATHIME_OPT_MAX_CANDIDATES reports itself unsupported
 *    here and none of libhangul's HanjaTable/HanjaList API is used.
 *  - PATHIME_HANGUL_PREEDIT_NONE builds the syllable inside the client's
 *    document by deleting the partial form and recommitting a fuller one
 *    (docs/libhangul-mapping.md:158). It is the only consumer of the
 *    surrounding-text surface and the only setter of a PATHIME_REQUIRES_*
 *    bit.
 *
 * Gotchas (the unknown-keyboard crash, flush semantics) are documented in
 * the mapping doc with file:line citations; consult it before coding around
 * a claim.
 */

#ifndef LIBPATHIME_SRC_ENGINES_HANGUL_BACKEND_H
#define LIBPATHIME_SRC_ENGINES_HANGUL_BACKEND_H

#include "backend.h"

namespace pathime {

/* Adapter to be defined once backend.h has its shape. */

}  // namespace pathime

#endif /* LIBPATHIME_SRC_ENGINES_HANGUL_BACKEND_H */
