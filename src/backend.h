/*
 * The internal engine interface — the seam between the core (everything
 * directly in src/) and the adapters (everything under src/engines/). Core
 * code never names a vendor type; engine code never touches the public API
 * surface; everything an adapter provides crosses through here.
 *
 * Deliberately not yet designed. Its shape falls out of the structured
 * composition representation (TODO.md §3, question 1, constraints in
 * composition.h), which should be settled against all three mapping docs at
 * once — the interface mostly exists to move that structure and the events
 * that change it. What it must end up carrying is known:
 *
 *  - The two-layer lifetime (Finding 3): process-global one-time
 *    init/shutdown hooks driven by init.cc, engine-level shared state, and
 *    one owned per-context handle, caller-destroyed.
 *  - Finished input only (Finding 6): the key layer and any composing front
 *    end sit above this interface; a backend is handed input in its own
 *    terms.
 *  - The structured composition (Finding 1): regions + active index, richer
 *    than the flat public value, projected by composition.cc.
 *  - Candidate access for the active region only. The core owns eager
 *    materialization and the currently-shown cursor (candidates.cc); the
 *    backend is asked to enumerate, select, and commit.
 *  - The borrowed-string rule (Finding 4): everything a backend returns is
 *    volatile until its next mutating call, so the boundary copies
 *    immediately (utf8.h helpers).
 *
 * Per-backend gotchas (flush semantics, the unknown-keyboard crash, the
 * two-call length protocol) are in docs/*-mapping.md — consult those rather
 * than re-deriving.
 */

#ifndef LIBPATHIME_SRC_BACKEND_H
#define LIBPATHIME_SRC_BACKEND_H

namespace pathime {

/* Interface to be defined alongside the composition representation. */

}  // namespace pathime

#endif /* LIBPATHIME_SRC_BACKEND_H */
