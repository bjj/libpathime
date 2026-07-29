/*
 * Korean adapter over libhangul. The mapping is docs/libhangul-mapping.md;
 * what makes this more than a shim:
 *
 *  - libhangul exposes only the trailing mutable syllable, so the settled
 *    prefix of the preedit is accumulated on this side.
 *  - Its composition API is UCS-4; everything crossing backend.h is UTF-8
 *    (utf8.h owns the conversion).
 *  - Input is finished: a plain US-QWERTY int — uppercase means Shift, no
 *    modifiers, no releases — and backspace is the separate
 *    hangul_ic_backspace(). layout_key maps onto this directly: keycodes
 *    matter to hangul only as key *position*.
 *  - Hangul produces no candidates at all: hanja conversion is out of scope
 *    for this API, so PATHIME_OPT_MAX_CANDIDATES reports itself unsupported
 *    here and none of libhangul's HanjaTable/HanjaList API is used.
 *  - PATHIME_HANGUL_PREEDIT_NONE builds the syllable inside the client's
 *    document by deleting the partial form and recommitting a fuller one
 *    (docs/libhangul-mapping.md:159). It is the only consumer of the
 *    surrounding-text surface and the only setter of a PATHIME_REQUIRES_*
 *    bit.
 *
 * Gotchas (the unknown-keyboard crash, flush semantics) are documented in
 * the mapping doc with file:line citations; consult it before coding around
 * a claim.
 *
 * ---------------------------------------------------------------------------
 * Why this header declares nothing
 * ---------------------------------------------------------------------------
 *
 * backend.h already declares the whole of this adapter's outside surface —
 * hangul_global_init(), hangul_global_shutdown() and hangul_create_engine() —
 * and those three functions are all the core is ever allowed to name. The
 * EngineBackend and ContextBackend subclasses behind them stay file-local in
 * hangul_backend.cc, because nothing outside that file can use them without
 * also naming HangulInputContext, which is exactly the vendor type backend.h
 * exists to keep out of the core.
 *
 * The file therefore survives as the one place to state that, and as the
 * include the .cc opens with so that the adapter and its documentation stay
 * together. If a second hangul translation unit ever appears — the way anthy
 * has romaji.* beside its adapter — its shared declarations belong here.
 */

#ifndef LIBPATHIME_SRC_ENGINES_HANGUL_BACKEND_H
#define LIBPATHIME_SRC_ENGINES_HANGUL_BACKEND_H

#include "backend.h"

#endif /* LIBPATHIME_SRC_ENGINES_HANGUL_BACKEND_H */
