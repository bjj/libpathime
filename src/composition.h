/*
 * The structured composition model and its projection to the flat public
 * value. TODO.md §3 open question 1 lives here, and it is still open — this
 * header carries the constraints, not yet the types.
 *
 * Every backend keeps state the flat {preedit, preedit_settled, auxiliary,
 * candidates} value cannot hold (TODO.md §2, Finding 1): anthy has N
 * segments, each with its own candidate array, plus an active-segment index;
 * pyzy's preedit is three parts (selected | conversion | rest) with the
 * middle one provisional and its own focused-candidate index; libhangul
 * exposes only the trailing mutable syllable, so the settled prefix must be
 * accumulated on our side. The structured form — regions, an active index, a
 * per-region candidate cursor — is what lives here; the flat
 * pathime_composition_t is recomputed from it at the boundary after every
 * change, never patched incrementally.
 *
 * Projection rules fixed by the API round: preedit_settled is the boundary
 * between the settled prefix and the still-mutable region; the candidate list
 * always describes the leftmost unsettled span (greedy resolution, no segment
 * navigation — the phone-keyboard target breaks the tie). All projected
 * positions are Unicode scalar values; utf8.h owns the conversions.
 *
 * Design the representation against all three mapping docs at once
 * (the docs/ per-library mapping notes); the shape of backend.h falls out of it.
 */

#ifndef LIBPATHIME_SRC_COMPOSITION_H
#define LIBPATHIME_SRC_COMPOSITION_H

namespace pathime {

/* Types to be defined with backend.h; see the header comment. */

}  // namespace pathime

#endif /* LIBPATHIME_SRC_COMPOSITION_H */
