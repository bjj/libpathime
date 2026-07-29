/*
 * Candidate materialization and cursor tracking. Two obligations the public
 * API takes on live here and nowhere else:
 *
 *  - Eager materialization. pathime_context_candidate() is documented
 *    callback-safe, which is only true if every candidate the
 *    PATHIME_OPT_MAX_CANDIDATES cap allows is fetched *before*
 *    composition_changed is dispatched (context.cc sequences this). pyzy's
 *    hasCandidate(i) is lazy and mutating, so the ordering is a real
 *    constraint, not a formality.
 *
 *  - The currently-shown candidate. Neither anthy nor
 *    pyzy durably records which candidate the user is hovering before commit
 *    — anthy records only at anthy_commit_segment() time — so the cursor is
 *    tracked here, per active region, and fed back to the backend when the
 *    client moves it, when a selection makes it real, and on commit.
 *
 * The split with context.cc is deliberate: context.cc sequences a mutating
 * call, this file owns everything candidate-shaped. What crosses between them
 * is materialize_candidates(), declared in candidates.h, plus the cursor
 * itself — refresh_composition() publishes Composition::cursor into
 * pathime_composition_t::candidate_cursor, which is why there is no cursor
 * getter here to pair with the setter below. The cursor is composition data
 * and is read from the struct.
 */

#include <pathime/pathime.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "candidates.h"
#include "context.h"
#include "engine.h"
#include "init.h"
#include "options.h"

/*
 * The currently-shown candidate cursor lives in Composition::cursor
 * (composition.h), not here. It is per *active span* rather than per context —
 * a span settles, the next becomes active, and the new one starts hovering its
 * own first candidate — so it belongs with the span structure, and this file
 * is what moves it: the core tracks it because neither anthy nor pyzy durably
 * records it before commit, and it reaches the backend only when a selection
 * or a commit makes it real.
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
 * Fill ctx->model.candidates with every candidate the cap allows, and drop any past
 * it. Called by refresh_composition() after the backend has finished mutating
 * and before any callback is dispatched — see the file comment for why that
 * ordering is load-bearing rather than tidy.
 *
 * Declared in candidates.h.
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
    if (ctx->model.candidates.size() > cap) {
        ctx->model.candidates.resize(cap);
        if (ctx->model.cursor >= ctx->model.candidates.size()) {
            ctx->model.cursor = 0;
        }
    }

    /*
     * Enumerate the active span's candidates, appending until the backend runs
     * out or the list reaches the cap. Three things about this are decided and
     * should not be rediscovered:
     *
     *  - It is the *active span* only. There is never more than one under
     *    consideration and the client never chooses which — greedy
     *    left-to-right resolution, no segment navigation, which the
     *    phone-keyboard target settled.
     *  - Every string is copied at the seam. Everything a backend returns is
     *    borrowed and volatile, valid only until its next mutating call
     *    so aliasing one into the model would hand a
     *    client a dangling slice the moment the next key arrives. The
     *    obligation is the adapter's, stated as rule 1 of backend.h.
     *  - Running out before the cap is normal and is not an error: the cap is
     *    a ceiling, and what the enumeration produces is presented to the
     *    client as the complete list. Hangul reaches here with nothing to
     *    enumerate at all — libhangul composes syllables from jamo and has
     *    nothing to choose between — which is why it reports
     *    PATHIME_OPT_MAX_CANDIDATES unsupported.
     */
    if (ctx->backend != nullptr) {
        const pathime::ContextOptions options(ctx);
        ctx->backend->materialize_candidates(cap, options, &ctx->model);
    }

    /* The adapter is trusted to respect the cap, but not blindly: a backend
     * that overshot would silently break the client's position numbering. */
    if (ctx->model.candidates.size() > cap) {
        ctx->model.candidates.resize(cap);
    }
    if (ctx->model.cursor >= ctx->model.candidates.size()) {
        ctx->model.cursor = 0;
    }
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
     * The bound is ctx->model.candidates.size(), which is exactly
     * ctx->composition.candidate_count — refresh_composition() publishes the
     * one from the other — but reading it from the vector is what makes the
     * indexing below safe by construction rather than by agreement between two
     * files. Today the pump has nothing to enumerate, so the list is always
     * empty and every index is out of range; the read itself is real and needs
     * no revisiting when it stops being.
     */
    if (index >= ctx->model.candidates.size()) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    /*
     * Callback-safe, and this is where that promise is cashed: an array read
     * of storage the library finished writing before it dispatched anything.
     * Nothing here re-enters a backend, which is only true because the pump
     * above ran first. The slice is borrowed with the ordinary lifetime, and
     * c_str() is NUL-terminated as everything the library produces is.
     */
    const std::string &candidate = ctx->model.candidates[index];
    out->bytes = candidate.c_str();
    out->len = candidate.size();
    return PATHIME_OK;
}

pathime_status_t pathime_context_set_candidate_cursor(pathime_context_t *ctx,
                                                      size_t index)
{
    if (ctx == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }
    if (!pathime::initialized()) {
        return PATHIME_ERROR_NOT_INITIALIZED;
    }

    /*
     * Out of range is rejected here rather than by the adapter, which is what
     * lets ContextBackend::set_cursor() take the index on trust. An empty list
     * fails this test for every index, so "no candidates" needs no separate
     * arm: there is no position 0 to move to when there is nothing to show.
     */
    if (index >= ctx->model.candidates.size()) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    /*
     * Moving the cursor settles nothing, so unlike select_candidate() there is
     * no Output to clear and nothing to commit — the only thing that can come
     * back is a rewritten active span, for a backend that previews the
     * candidate it is hovering.
     *
     * The core's copy moves first, and the adapter may move it again. That
     * ordering matters for a backend which cannot hover at all: it returns
     * UNSUPPORTED below and the assignment is rolled back, so a failed call
     * leaves the cursor exactly where it was. Which is also why this is not
     * an assignment from the caller's point of view — the value that ends up
     * in pathime_composition_t::candidate_cursor is whatever survives the
     * refresh below, and the header tells clients to read it back rather than
     * assume.
     */
    const size_t previous = ctx->model.cursor;
    ctx->model.cursor = index;

    pathime_status_t status = PATHIME_ERROR_UNSUPPORTED;
    if (ctx->backend != nullptr) {
        const pathime::ContextOptions options(ctx);
        status = ctx->backend->set_cursor(index, options, &ctx->model);
    }

    if (status != PATHIME_OK) {
        ctx->model.cursor = previous;
        /*
         * UNSUPPORTED is a rejection: the backend declined to hover and
         * changed nothing, so the composition is intact. Anything else got
         * partway and leaves it indeterminate until reset, as the header's
         * Status section promises.
         */
        if (status != PATHIME_ERROR_UNSUPPORTED) {
            ctx->indeterminate = true;
        }
        return status;
    }

    pathime::refresh_composition(ctx, false);
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
    if (index >= ctx->model.candidates.size()) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    /*
     * The selection itself, greedy and left to right. The adapter settles the
     * span the chosen candidate covers, extends the settled prefix, and
     * produces a fresh list for whatever remains; when nothing remains it puts
     * the finished text in the Output and the dispatch below delivers it. The
     * client never navigates or resizes spans.
     *
     * The cursor moves first, because this is the moment the library's
     * tracking of the currently-shown candidate stops being ours alone: an
     * adapter that must tell its backend which candidate was chosen reads it
     * from the model rather than being passed it twice.
     */
    ctx->model.cursor = index;
    ctx->output.clear();

    pathime_status_t status = PATHIME_OK;
    if (ctx->backend != nullptr) {
        const pathime::ContextOptions options(ctx);
        status = ctx->backend->select_candidate(index, options, &ctx->model, &ctx->output);
    }

    if (status != PATHIME_OK) {
        /*
         * PATHIME_ERROR_UNSUPPORTED is a rejection — an engine that produces no
         * candidates never had a list to select from, so nothing happened and
         * nothing needs assembling. Anything else is a failure that got partway,
         * and leaves the composition indeterminate until reset, exactly as the
         * header's Status section promises.
         */
        if (status != PATHIME_ERROR_UNSUPPORTED) {
            ctx->indeterminate = true;
        }
        return status;
    }

    pathime::refresh_composition(ctx, false);
    return PATHIME_OK;
}
