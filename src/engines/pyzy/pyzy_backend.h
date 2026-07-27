/*
 * Chinese adapter over pyzy — one backend supplying two engine ids,
 * PATHIME_ENGINE_PINYIN and PATHIME_ENGINE_BOPOMOFO (the one place
 * PATHIME_WITH_* is not one-to-one with an engine). The mapping is
 * docs/pyzy-mapping.md; what makes this more than a shim:
 *
 *  - pyzy's InputType is fixed at context creation, so pinyin vs bopomofo is
 *    decided when the pathime context is created, not switched later.
 *  - Its preedit is three parts (selectedText | conversionText | restText)
 *    with the middle provisional and its own focused-candidate index
 *    (Finding 1).
 *  - cursor() is a byte offset into the raw ASCII input; never conflate it
 *    with output scalar positions (Finding 4).
 *  - hasCandidate(i) is lazy and mutating, which is why core materializes
 *    candidates eagerly before dispatching callbacks (candidates.cc); this
 *    adapter is where that pump actually touches pyzy.
 *  - Mutations fire six Observer callbacks synchronously mid-call; the
 *    dirty-flag observer that reconciles push with the core's pull model is
 *    observer.* (Finding 5).
 *  - pyzy schedules its user-database save through g_timeout_add and a
 *    GTimer, which needs a GMainLoop we do not run — the save would never
 *    fire, so this adapter drives it explicitly (TODO.md §5).
 *  - Input is [a-z] and apostrophe only (Finding 6).
 *
 * One claim to re-verify here: whether PATHIME_OPT_PINYIN_FUZZY/_CORRECTION
 * are truly unreachable from bopomofo (TODO.md §1, "One claim to re-check").
 */

#ifndef LIBPATHIME_SRC_ENGINES_PYZY_BACKEND_H
#define LIBPATHIME_SRC_ENGINES_PYZY_BACKEND_H

#include "backend.h"

namespace pathime {

/* Adapter to be defined once backend.h has its shape. */

}  // namespace pathime

#endif /* LIBPATHIME_SRC_ENGINES_PYZY_BACKEND_H */
