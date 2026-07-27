/*
 * Candidate materialization and cursor tracking. Two obligations from the API
 * round live here and nowhere else:
 *
 *  - Eager materialization. pathime_context_candidate() is documented
 *    callback-safe, which is only true if every candidate the
 *    PATHIME_OPT_MAX_CANDIDATES cap allows is fetched *before*
 *    composition_changed is dispatched (context.cc sequences this). pyzy's
 *    hasCandidate(i) is lazy and mutating, so the ordering is a real
 *    constraint, not a formality.
 *
 *  - The currently-shown candidate (TODO.md §2, Finding 2). Neither anthy nor
 *    pyzy durably records which candidate the user is hovering before commit
 *    — anthy records only at anthy_commit_segment() time — so the cursor is
 *    tracked here, per active region, and fed back to the backend only on
 *    selection or commit.
 */

#include <pathime/pathime.h>
