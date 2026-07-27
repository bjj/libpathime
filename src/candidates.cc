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
 *
 * The split with context.cc is deliberate: context.cc sequences a mutating
 * call, this file owns everything candidate-shaped. The one thing that has to
 * cross between them is materialize_candidates(), which refresh_composition()
 * calls; it is declared at the top of context.cc rather than in a header,
 * because a src/candidates.h holding a single function nothing else names
 * would be ceremony. It becomes the right answer the moment the pump has real
 * work to do — an enumeration entry point, a cursor accessor, and the
 * selection path all want declaring in one place.
 */

#include <pathime/pathime.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "context.h"
#include "engine.h"
#include "init.h"
#include "options.h"

/*
 * TODO(impl): the currently-shown candidate cursor belongs here — the index
 * within the active region's list that the user is hovering, which we track
 * because neither backend does (Finding 2), and which is pushed into the
 * backend only when a selection or a commit makes it real.
 *
 * It has nowhere to live yet. It is per *active region*, not per context: a
 * region settles, the next one becomes active, and the new region starts with
 * its own cursor at 0 — so the natural home is the structured composition
 * (composition.h), which does not exist, and inventing a representation for it
 * here would pre-empt the one design round that is meant to settle regions,
 * the active index and the per-region cursor together against all three
 * mapping docs at once (TODO.md §3, question 1). Nothing between now and then
 * needs it: with no enumeration there is no list to hover in.
 */

namespace {

/**
 * The candidate cap in force for @a ctx: the resolved value of
 * PATHIME_OPT_MAX_CANDIDATES, walked through the tiers by options.cc so that
 * the one resolution rule in the library lives in one place.
 */
size_t resolve_candidate_cap(const pathime_context_t *ctx)
{
    int64_t value =
        pathime::resolve_option_number(ctx->engine, ctx, PATHIME_OPT_MAX_CANDIDATES);

    /*
     * The setters already reject anything outside the descriptor's bounds, so
     * these two clamps are belt-and-braces rather than policy. The minimum is
     * the documented one: zero is rejected rather than treated as a way to
     * suppress the list, because engines that convert by selection cannot make
     * progress without a candidate and a cap of zero would deadlock the
     * composition.
     */
    if (value < 1) {
        value = 1;
    }
    const uint64_t unsigned_value = static_cast<uint64_t>(value);
    const uint64_t size_max = static_cast<uint64_t>(std::numeric_limits<size_t>::max());
    if (unsigned_value > size_max) {
        return std::numeric_limits<size_t>::max();
    }
    return static_cast<size_t>(unsigned_value);
}

}  // namespace

namespace pathime {

/**
 * Fill ctx->candidates with every candidate the cap allows, and drop any past
 * it. Called by refresh_composition() after the backend has finished mutating
 * and before any callback is dispatched — see the file comment for why that
 * ordering is load-bearing rather than tidy.
 *
 * Declared at the top of context.cc.
 */
void materialize_candidates(pathime_context_t *ctx)
{
    const size_t cap = resolve_candidate_cap(ctx);

    /*
     * Lowering the cap removes entries from the tail, and that is the whole of
     * what a lowered cap means. Raising it only ever appends — candidates are
     * never reordered — which is what lets a client raise the cap as the user
     * scrolls without renumbering positions it has already handed out. Doing
     * the truncation first also bounds the work below to (cap - size())
     * fetches rather than a full re-enumeration.
     */
    if (ctx->candidates.size() > cap) {
        ctx->candidates.resize(cap);
    }

    /*
     * TODO(impl): enumerate the active region's candidates through backend.h,
     * appending copies until either the backend runs out or the list reaches
     * `cap`. Three things about that loop are already decided and should not
     * be rediscovered:
     *
     *  - It is the *active region* only. There is never more than one span
     *    under consideration and the client never chooses which — greedy
     *    left-to-right resolution, no segment navigation, which the
     *    phone-keyboard target settled.
     *  - Every string is copied at the seam. Everything a backend returns is
     *    borrowed and volatile, valid only until its next mutating call
     *    (TODO.md §2, Finding 4), so aliasing one into ctx->candidates would
     *    hand a client a dangling slice the moment the next key arrives.
     *  - Running out before the cap is normal and is not an error: the cap is
     *    a ceiling, and what the enumeration produces is presented to the
     *    client as the complete list. Hangul reaches this function with
     *    nothing to enumerate at all — libhangul composes syllables from jamo
     *    and has nothing to choose between — which is why it reports
     *    PATHIME_OPT_MAX_CANDIDATES unsupported.
     *
     * The enumeration crosses backend.h, which is deliberately undesigned
     * until the composition representation settles (TODO.md §3, question 1),
     * so there is nothing to call yet. Until then the list stays empty and
     * pathime_context_candidate() below therefore rejects every index.
     */
    (void)cap;
}

}  // namespace pathime

/* ===========================================================================
 * Public entry points
 * ======================================================================== */

pathime_status_t pathime_context_candidate(const pathime_context_t *ctx,
                                           size_t index,
                                           pathime_str_t *out)
{
    if (ctx == nullptr || out == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }
    if (!pathime::initialized()) {
        return PATHIME_ERROR_NOT_INITIALIZED;
    }

    /*
     * Focus is not required: this is a non-mutating query, and focus gates
     * input and only input.
     *
     * The bound is ctx->candidates.size(), which is exactly
     * ctx->composition.candidate_count — refresh_composition() publishes the
     * one from the other — but reading it from the vector is what makes the
     * indexing below safe by construction rather than by agreement between two
     * files. Today the pump has nothing to enumerate, so the list is always
     * empty and every index is out of range; the read itself is real and needs
     * no revisiting when it stops being.
     */
    if (index >= ctx->candidates.size()) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    /*
     * Callback-safe, and this is where that promise is cashed: an array read
     * of storage the library finished writing before it dispatched anything.
     * Nothing here re-enters a backend, which is only true because the pump
     * above ran first. The slice is borrowed with the ordinary lifetime, and
     * c_str() is NUL-terminated as everything the library produces is.
     */
    const std::string &candidate = ctx->candidates[index];
    out->bytes = candidate.c_str();
    out->len = candidate.size();
    return PATHIME_OK;
}

pathime_status_t pathime_context_select_candidate(pathime_context_t *ctx,
                                                  size_t index)
{
    if (ctx == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }
    if (!pathime::initialized()) {
        return PATHIME_ERROR_NOT_INITIALIZED;
    }
    if (!ctx->focused) {
        return PATHIME_ERROR_NOT_FOCUSED;
    }

    /*
     * Selecting from an obsolete list is a client error and only partly a
     * detectable one. An index past the end of the current list is caught
     * here; an index that is still in range is indistinguishable from a
     * deliberate selection and silently selects from the *current* list. That
     * gap is a decided non-goal rather than an oversight: no generation
     * counter is carried, because the API is synchronous and every list
     * replacement is announced by composition_changed before the triggering
     * call returns, so a stale in-range index can only come from a client that
     * ignored the announcement — a bug a counter would report rather than
     * prevent.
     *
     * With the pump empty, candidates.size() is 0 and every call is rejected
     * here.
     */
    if (index >= ctx->candidates.size()) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    /*
     * TODO(impl): perform the selection. It is greedy and resolves left to
     * right, and the four steps are fixed by the API round:
     *
     *   1. Push the selection into the backend for the active region — this is
     *      also the moment the currently-shown cursor (Finding 2) stops being
     *      ours and becomes the backend's, since neither anthy nor pyzy
     *      records it before then.
     *   2. Settle the span that candidate covers, extending the settled region
     *      of the preedit: composition.preedit_settled moves right by the
     *      scalar count of the settled text.
     *   3. Produce a fresh candidate list for whatever input remains — the new
     *      leftmost unsettled span becomes the active region, with its own
     *      cursor at 0. The client never navigates or resizes segments.
     *   4. When nothing remains, commit. That commit and any resulting
     *      composition change are dispatched by refresh_composition() below,
     *      in the fixed order, before this call returns.
     */

    /* Every mutating entry point ends here, this one included: assemble,
     * materialize, then dispatch. */
    pathime::refresh_composition(ctx, false);
    return PATHIME_OK;
}
