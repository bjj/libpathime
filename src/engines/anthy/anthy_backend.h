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
 *    were deferred for exactly that reason (docs/design-history.md §1).
 *
 * ---------------------------------------------------------------------------
 * Nothing is declared here
 * ---------------------------------------------------------------------------
 *
 * The three functions backend.h declares under PATHIME_WITH_ANTHY —
 * anthy_global_init(), anthy_global_shutdown() and anthy_create_engine() — are
 * the whole of this adapter's surface, and they are already declared there,
 * which is the point of that header: init.cc and engine.cc call them without
 * naming a vendor type. The EngineBackend and ContextBackend subclasses behind
 * them have no second caller, so they live in the .cc as internal-linkage
 * types and never appear in a header at all.
 *
 * The file remains because <anthy/anthy.h> must not be reachable from core:
 * this is the documented place to put anything the adapter later has to share
 * with a second translation unit of its own — the kana-entry state machine
 * romaji.h defers, say — without that becoming an invitation to include it
 * from src/.
 */

#ifndef LIBPATHIME_SRC_ENGINES_ANTHY_BACKEND_H
#define LIBPATHIME_SRC_ENGINES_ANTHY_BACKEND_H

#include "backend.h"

namespace pathime {

/* Intentionally empty; see the note above. */

}  // namespace pathime

#endif /* LIBPATHIME_SRC_ENGINES_ANTHY_BACKEND_H */
