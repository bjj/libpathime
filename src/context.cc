/*
 * The per-context layer: pathime_context_* lifecycle, the process_key entry
 * point, focus, reset, surrounding text, and callback dispatch.
 *
 * The discipline this file exists to enforce is the ordering of a mutating
 * call: validate → route through the key layer (keys.cc, or the engine's own
 * front end) → let the backend mutate → assemble the structured composition
 * (composition.cc; pyzy via its observer's dirty flags) → materialize
 * candidates up to the cap (candidates.cc) → only then dispatch
 * composition_changed / commit callbacks, so every callback observes one
 * consistent, fully materialized state and pathime_context_candidate() stays
 * callback-safe. A failure partway leaves the context valid-but-indeterminate
 * until pathime_context_reset(), exactly as the header's Status section
 * promises.
 *
 * The tail of that sequence is pathime::refresh_composition() at the bottom of
 * this file, and every mutating entry point ends there.
 *
 * Two conventions hold throughout, and both are library-wide:
 *
 *  - Validation order is NULL and struct_size first
 *    (PATHIME_ERROR_INVALID_ARGUMENT), then pathime::initialized()
 *    (PATHIME_ERROR_NOT_INITIALIZED), then focus
 *    (PATHIME_ERROR_NOT_FOCUSED), then value legality. Out-parameters are
 *    untouched on failure — the one documented exception being
 *    pathime_context_process_key()'s out_handled, which is always written.
 *  - No exception crosses the C boundary, and nor is one avoided by
 *    new(std::nothrow) alone: std::string and std::vector can throw where
 *    nothrow placement does not reach. Every allocating region is wrapped and
 *    std::bad_alloc becomes PATHIME_ERROR_OUT_OF_MEMORY, matching init.cc.
 */

#include <pathime/pathime.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <vector>

#include "candidates.h"
#include "context.h"
#include "engine.h"
#include "init.h"
#include "keys.h"
#include "options.h"
#include "utf8.h"

namespace {

/* ---------------------------------------------------------------------------
 * UTF-8 validation
 * ------------------------------------------------------------------------- */


/* ---------------------------------------------------------------------------
 * struct_size versioning
 * ------------------------------------------------------------------------- */

/*
 * Every layout of each caller-supplied struct this library has ever shipped.
 *
 * The header's rule is that a value the library does not recognize is
 * PATHIME_ERROR_INVALID_ARGUMENT, which is a membership test rather than a
 * range test: it accepts an older client against a newer library — the
 * direction the header promises — and rejects a newer client against an older
 * one, whose layout we cannot know and whose extra fields we would silently
 * ignore.
 *
 * Only one layout of each has shipped. When a member is appended, today's
 * sizeof stays in the list and the new one joins it; nothing is ever removed,
 * because that would break a client still compiling against the old header.
 */
constexpr size_t kClientStructSizes[] = { sizeof(pathime_client_t) };
constexpr size_t kKeyEventStructSizes[] = { sizeof(pathime_key_event_t) };

template <size_t N>
bool known_struct_size(const size_t (&sizes)[N], size_t value)
{
    for (size_t i = 0; i < N; ++i) {
        if (sizes[i] == value) {
            return true;
        }
    }
    return false;
}

/**
 * Whether a member at @a member_offset of @a member_size bytes lies inside the
 * struct the caller actually compiled against. "Members beyond the size given
 * are not read" is a per-member test, and reading one that is not there is the
 * exact bug struct_size exists to prevent.
 */
constexpr bool has_member(size_t struct_size, size_t member_offset, size_t member_size)
{
    return member_offset + member_size <= struct_size;
}

}  // namespace

/* ===========================================================================
 * Lifecycle
 * ======================================================================== */

pathime_status_t pathime_context_create(pathime_engine_t *engine,
                                        const pathime_client_t *client,
                                        void *user_data,
                                        pathime_context_t **out_ctx)
{
    if (engine == nullptr || client == nullptr || out_ctx == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }
    if (!known_struct_size(kClientStructSizes, client->struct_size)) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }
    if (!pathime::initialized()) {
        return PATHIME_ERROR_NOT_INITIALIZED;
    }

    /*
     * Resolve the callback table once, here. The header promises the table
     * stays valid and unchanged for the context's lifetime, which is what
     * licenses caching the three pointers, and resolving through struct_size
     * once is the honest way to apply it: a member the caller's struct is too
     * small to contain becomes nullptr, and every later use is a plain pointer
     * test rather than repeated offset arithmetic. See context.h.
     */
    void (*commit_text)(void *, pathime_str_t) = nullptr;
    void (*delete_surrounding_text)(void *, ptrdiff_t, size_t) = nullptr;
    void (*composition_changed)(void *, const pathime_composition_t *) = nullptr;

    if (has_member(client->struct_size,
                   offsetof(pathime_client_t, commit_text),
                   sizeof(client->commit_text))) {
        commit_text = client->commit_text;
    }
    if (has_member(client->struct_size,
                   offsetof(pathime_client_t, delete_surrounding_text),
                   sizeof(client->delete_surrounding_text))) {
        delete_surrounding_text = client->delete_surrounding_text;
    }
    if (has_member(client->struct_size,
                   offsetof(pathime_client_t, composition_changed),
                   sizeof(client->composition_changed))) {
        composition_changed = client->composition_changed;
    }

    /*
     * commit_text is documented as required and may not be NULL. It is
     * reported as a missing callback rather than a bad argument because it is
     * the degenerate case of the requirements check below — every engine needs
     * it — and because PATHIME_ERROR_MISSING_CALLBACK tells the client the one
     * useful thing: which entry of its own table to fill in.
     */
    if (commit_text == nullptr) {
        return PATHIME_ERROR_MISSING_CALLBACK;
    }

    /*
     * The engine's resolved configuration decides the rest. Today only
     * PATHIME_HANGUL_PREEDIT_NONE sets either bit, and it sets both: that mode
     * holds nothing at all, building the syllable inside the client's document
     * by deleting the partial form and recommitting a fuller one.
     *
     * Only PATHIME_REQUIRES_DELETE_SURROUNDING is enforceable here.
     * PATHIME_REQUIRES_SURROUNDING_TEXT names no callback — it is an
     * obligation to keep pathime_context_set_surrounding_text() up to date,
     * which nothing at creation time can verify. Failing to meet it degrades
     * rather than corrupts: an engine that cannot see the text it wants to
     * revise abandons the revision instead of guessing.
     */
    const uint32_t required = pathime_engine_requirements(engine);
    if ((required & PATHIME_REQUIRES_DELETE_SURROUNDING) != 0 &&
        delete_surrounding_text == nullptr) {
        return PATHIME_ERROR_MISSING_CALLBACK;
    }

    pathime_context_t *ctx = nullptr;
    try {
        ctx = new pathime_context();
        ctx->engine = engine;
        ctx->user_data = user_data;
        ctx->commit_text = commit_text;
        ctx->delete_surrounding_text = delete_surrounding_text;
        ctx->composition_changed = composition_changed;

        /*
         * Register with the engine. This list is what makes late option
         * resolution real: an engine-level set walks it and dispatches
         * composition_changed to every context that has not overridden the
         * option (engine.h). Contexts add themselves here and remove
         * themselves in destroy, and the public contract already requires
         * every context to be destroyed before its engine, so it never
         * dangles.
         */
        engine->contexts.push_back(ctx);
    } catch (const std::bad_alloc &) {
        delete ctx;
        return PATHIME_ERROR_OUT_OF_MEMORY;
    }

    /*
     * The backend's per-context handle — HangulInputContext *, anthy_context_t,
     * PyZy::InputContext *, whichever this engine is. Created only now, rather
     * than with the object above, because it is the one step that can fail for
     * a reason the caller did not cause, and because it needs the options
     * reader, which needs the context to exist first.
     *
     * On failure the context is unregistered and deleted and out_ctx is left
     * untouched: nothing is published to the caller until every step has
     * succeeded.
     */
    if (engine->backend != nullptr) {
        const pathime::ContextOptions options(ctx);
        ctx->backend = engine->backend->create_context(options);
        if (ctx->backend == nullptr) {
            std::vector<pathime_context_t *> &siblings = engine->contexts;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), ctx),
                           siblings.end());
            delete ctx;
            return PATHIME_ERROR_BACKEND;
        }
    }

    /*
     * A new context starts unfocused, with empty composition data and no
     * surrounding text — context.h's member initializers say all three. What
     * remains is to publish the flat composition value, because
     * pathime_context_composition() is documented never NULL for a valid
     * context and its zero-length pathime_str_t members must point at "" and
     * not at NULL. With force false and nothing changed this dispatches no
     * callback, which is right: creation is not a composition change.
     */
    pathime::refresh_composition(ctx, false);

    *out_ctx = ctx;
    return PATHIME_OK;
}

void pathime_context_destroy(pathime_context_t *ctx)
{
    if (ctx == nullptr) {
        return;
    }

    /*
     * Composition state is discarded, never committed, and no callback is
     * dispatched — including composition_changed. A client that wants the
     * preedit finalized commits it before destroying, exactly as it decides
     * for itself what happens on focus loss.
     */

    if (ctx->engine != nullptr) {
        std::vector<pathime_context_t *> &siblings = ctx->engine->contexts;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), ctx),
                       siblings.end());
    }

    /* The backend handle goes with the object — each of the three is one owned
     * handle, caller-destroyed (docs/adapter-findings.md, Finding 3), and unique_ptr in
     * context.h is where that ownership is stated. */
    delete ctx;
}

pathime_engine_t *pathime_context_engine(const pathime_context_t *ctx)
{
    return ctx != nullptr ? ctx->engine : nullptr;
}

void *pathime_context_user_data(const pathime_context_t *ctx)
{
    return ctx != nullptr ? ctx->user_data : nullptr;
}

uint32_t pathime_context_requirements(const pathime_context_t *ctx)
{
    /* As at the engine level, a handle that does not exist has an honest
     * answer: no callback obligation can arise from it. */
    if (ctx == nullptr) {
        return 0;
    }

    /*
     * The same one option that drives pathime_engine_requirements(), resolved
     * with the context in hand instead of nullptr — which is the whole of the
     * difference between the two calls, and is what makes this cheap enough to
     * be worth having rather than a thing a client derives.
     *
     * Passing ctx also brings in the capping rule in options.cc's
     * resolve_number(): a context whose client cannot delete resolves
     * PATHIME_HANGUL_PREEDIT_NONE down to _SYLLABLE, so this reports what the
     * context is actually doing. That is the right answer *here* and the wrong
     * one at the engine level, where the uncapped value is what
     * pathime_context_create() must test a new client against — see the note
     * in engine.cc.
     */
    if (pathime::resolve_option_number(ctx->engine, ctx, PATHIME_OPT_HANGUL_PREEDIT) ==
        PATHIME_HANGUL_PREEDIT_NONE) {
        return PATHIME_REQUIRES_SURROUNDING_TEXT | PATHIME_REQUIRES_DELETE_SURROUNDING;
    }
    return 0;
}

/* ===========================================================================
 * Key input
 * ======================================================================== */

pathime_status_t pathime_context_process_key(pathime_context_t *ctx,
                                             const pathime_key_event_t *event,
                                             bool *out_handled)
{
    /*
     * out_handled is the one out-parameter this API documents as *always*
     * written, so it is written before any other work and therefore before
     * every error return below. A client whose fallback path keys off it alone
     * — ignoring the status entirely — is then correct: an event the library
     * rejected is an event it did not handle, and the client should process it
     * as it normally would.
     */
    if (out_handled != nullptr) {
        *out_handled = false;
    }

    if (ctx == nullptr || event == nullptr || out_handled == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }
    if (!known_struct_size(kKeyEventStructSizes, event->struct_size)) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }
    if (!pathime::initialized()) {
        return PATHIME_ERROR_NOT_INITIALIZED;
    }
    if (!ctx->focused) {
        return PATHIME_ERROR_NOT_FOCUSED;
    }

    /*
     * Value legality, last, and in the key layer rather than here — keys.cc is
     * where "what a valid event is" is settled once, so that three adapters
     * cannot each answer it differently.
     */
    pathime::KeyEvent key;
    const pathime_status_t decoded = pathime::key_event_from_public(event, &key);
    if (decoded != PATHIME_OK) {
        return decoded;
    }

    /*
     * The mutating sequence. The ordering is the whole point of this file: the
     * backend finishes mutating before anything is assembled, and everything
     * is assembled before any callback is dispatched.
     *
     * The backends accept only finished input (docs/adapter-findings.md, Finding 6), so
     * everything above this call is ours — and for anthy the composing front
     * end runs inside its adapter, which is why there is no separate step for
     * it here.
     */
    ctx->output.clear();

    const pathime::ContextOptions options(ctx);
    const pathime::ContextSurroundingText doc(ctx);
    bool handled = false;
    if (ctx->backend != nullptr) {
        handled = ctx->backend->process_key(key, options, doc, &ctx->model, &ctx->output);
    }

    /*
     * Assemble, materialize, dispatch — including when the event is reported
     * unhandled, since an engine may absorb a key into its composition state,
     * emit the resulting text, and still hand the original key back for the
     * client to act on, with the ordering already correct. "Handled" describes
     * the incoming event only and is independent of what was produced.
     */
    pathime::refresh_composition(ctx, false);

    *out_handled = handled;
    return PATHIME_OK;
}

/* ===========================================================================
 * Composition
 * ======================================================================== */

const pathime_composition_t *pathime_context_composition(const pathime_context_t *ctx)
{
    /* Never NULL for a valid context: the flat value is rebuilt in place by
     * refresh_composition() and lives inside the context, so there is always
     * one to hand back. Callback-safe — a plain read of an object the library
     * finished writing before it dispatched anything. */
    return ctx != nullptr ? &ctx->composition : nullptr;
}

/* ===========================================================================
 * Client text
 * ======================================================================== */

pathime_status_t pathime_context_set_surrounding_text(pathime_context_t *ctx,
                                                      pathime_str_t text,
                                                      size_t cursor)
{
    if (ctx == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }
    /* A NULL pointer with a nonzero length is a malformed slice. NULL with
     * length zero is the empty document, which is a meaningful thing for a
     * client to report and is not the same as reporting nothing at all — see
     * has_surrounding in context.h. */
    if (text.bytes == nullptr && text.len != 0) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }
    if (!pathime::initialized()) {
        return PATHIME_ERROR_NOT_INITIALIZED;
    }

    /*
     * Focus is deliberately not required. Focus gates input and only input,
     * and the header lists supplying surrounding text among the operations
     * that work regardless of it.
     */

    size_t scalars = 0;
    if (!pathime::utf8_validate(text.bytes, text.len, &scalars)) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    /*
     * The two arguments are in different units, and this is the function where
     * confusing them is easiest: text.len sizes a buffer and is in bytes,
     * cursor locates a position and is in Unicode scalar values. Bounding the
     * cursor against the byte length would silently accept out-of-range
     * positions for any non-ASCII document — which is most of the surrounding
     * text an IME is given.
     */
    if (cursor > scalars) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    /* The library copies what it needs; the caller's buffer is borrowed for
     * the duration of the call only. */
    try {
        ctx->surrounding_text.assign(text.bytes != nullptr ? text.bytes : "", text.len);
    } catch (const std::bad_alloc &) {
        /*
         * Unlike most PATHIME_ERROR_OUT_OF_MEMORY paths this one leaves no
         * indeterminate composition behind, so ctx->indeterminate is not set:
         * surrounding text is the client's document rather than composition
         * state, and nothing else has been touched. The half-written snapshot
         * is dropped outright, because "the client has supplied nothing" is a
         * state the engine already knows how to handle — it abandons revisions
         * it cannot see the text for rather than guessing.
         */
        ctx->surrounding_text.clear();
        ctx->surrounding_cursor = 0;
        ctx->has_surrounding = false;
        return PATHIME_ERROR_OUT_OF_MEMORY;
    }

    ctx->surrounding_cursor = cursor;
    ctx->has_surrounding = true;

    /*
     * No refresh: this changes no composition state, produces no output, and
     * so has no callback to dispatch. What it does change is the frame of
     * reference for delete_surrounding_text, which is read at dispatch time.
     */
    return PATHIME_OK;
}

/* ===========================================================================
 * Focus and reset
 * ======================================================================== */

pathime_status_t pathime_context_set_focused(pathime_context_t *ctx, bool focused)
{
    if (ctx == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }
    if (!pathime::initialized()) {
        return PATHIME_ERROR_NOT_INITIALIZED;
    }

    /* Redundant transitions are no-ops. */
    if (ctx->focused == focused) {
        return PATHIME_OK;
    }

    /*
     * This assignment is the entire implementation, and that is the point.
     * Focus gates input and only input: everything else in this context —
     * composition, candidates, surrounding text, options — is preserved
     * exactly, so refocusing resumes where the user left off. Losing focus
     * neither commits nor discards and dispatches no callback; a client that
     * wants the preedit finalized or thrown away decides that for itself,
     * before dropping focus.
     *
     * The reference engines each answered this differently, and the answer is
     * genuinely arbitrary — which is why the API fixes one behaviour and
     * writes down why rather than calling it engine-dependent or negotiable.
     * It is the model case for preferring a determinate rule to a deferral, so
     * it is implemented literally: nothing else happens here.
     */
    ctx->focused = focused;
    return PATHIME_OK;
}

bool pathime_context_is_focused(const pathime_context_t *ctx)
{
    /*
     * No error channel, for the reason pathime_context_option_is_set() has
     * none: every case this cannot answer — a null handle, a library that was
     * never started — reads the same way, which is that this context is not
     * taking input.
     */
    if (ctx == nullptr || !pathime::initialized()) {
        return false;
    }
    return ctx->focused;
}

pathime_status_t pathime_context_reset(pathime_context_t *ctx)
{
    if (ctx == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }
    if (!pathime::initialized()) {
        return PATHIME_ERROR_NOT_INITIALIZED;
    }

    /* Focus is not required: reset is the documented recovery path after a
     * failure status, and a client must be able to take it whether or not the
     * field is still focused. */

    const bool was_indeterminate = ctx->indeterminate;

    /*
     * The backend's own reset — hangul_ic_reset(), anthy_reset_context(),
     * PyZy::InputContext::reset(). It does not commit: an engine that must
     * preserve text puts it in the Output explicitly, as part of handling the
     * reset, and the dispatch below delivers it. The three backends differ on
     * flush semantics; the docs/ mapping notes have the details rather than
     * this file re-deriving them.
     */
    ctx->output.clear();
    if (ctx->backend != nullptr) {
        ctx->backend->reset(&ctx->model, &ctx->output);
    }

    ctx->model.clear();
    ctx->indeterminate = false;

    /*
     * force is true on the recovery path: a context whose composition state is
     * indeterminate cannot be said to have been "already empty", and the
     * client's view of it must be replaced either way. Otherwise the change
     * detection inside refresh_composition() decides, which is what keeps the
     * header's "unless the composition was already empty" promise exact.
     */
    pathime::refresh_composition(ctx, was_indeterminate);
    return PATHIME_OK;
}

/* ===========================================================================
 * Post-mutation assembly
 * ======================================================================== */

namespace pathime {

int64_t ContextOptions::number(pathime_option_t option) const
{
    /*
     * The whole of the adapter-facing options surface: one call into the one
     * place resolution lives. An adapter never learns that options have levels
     * or tiers, and never caches — which is what makes the header's "a change
     * takes effect immediately" true without any invalidation protocol.
     */
    return resolve_option_number(ctx_->engine, ctx_, option);
}

namespace {

/**
 * Does the scalar range [cursor + @a offset, cursor + @a offset + @a count)
 * lie entirely within the snapshot?
 *
 * The one predicate behind both ContextSurroundingText::can_delete_before()
 * and refresh_composition()'s dispatch condition — the adapter's question
 * before the fact and the library's check after it are the same test, so they
 * cannot disagree.
 *
 * Positions are scalar indices throughout, which is what makes this cheap:
 * surrounding_cursor is already an index rather than a byte offset, so the
 * scalars available before the cursor are the cursor itself, and only the
 * total needs counting.
 */
bool range_within_snapshot(const pathime_context_t *ctx, ptrdiff_t offset, size_t count)
{
    if (count == 0) {
        return true;
    }
    if (!ctx->has_surrounding) {
        return false;
    }

    const ptrdiff_t cursor = static_cast<ptrdiff_t>(ctx->surrounding_cursor);
    const ptrdiff_t start = cursor + offset;
    if (start < 0) {
        return false;
    }

    const size_t total = utf8_scalar_count(ctx->surrounding_text.c_str(),
                                           ctx->surrounding_text.size());
    /* Written as a subtraction against the total rather than as
     * `start + count <= total` so that a large count cannot overflow. */
    const size_t ustart = static_cast<size_t>(start);
    return ustart <= total && count <= total - ustart;
}

}  // namespace

bool ContextSurroundingText::can_delete_before(size_t count) const
{
    /*
     * Nothing to revise is never blocked, and this is answered before the
     * snapshot is consulted so that an adapter with an empty revision does not
     * have to care whether the client supplies surrounding text at all.
     */
    if (count == 0) {
        return true;
    }

    /*
     * Otherwise: exactly the conditions the dispatch applies. Checked here
     * rather than inferred, because an adapter asking "will this land" must
     * get the dispatch's real answer — a view that said yes where the dispatch
     * says no would produce exactly the document corruption the header's
     * recovery rule exists to prevent.
     */
    if (ctx_->delete_surrounding_text == nullptr) {
        return false;
    }
    return range_within_snapshot(ctx_, -static_cast<ptrdiff_t>(count), count);
}

void refresh_composition(pathime_context_t *ctx, bool force)
{
    /*
     * The order below is the contract, not a convenience, and it is why this
     * step is one function rather than something each entry point does for
     * itself. Everything a callback may observe has to be finished before the
     * first callback runs, because the callback-safe set at the top of the
     * public header is exactly the set of non-mutating queries and their whole
     * promise is that they read finished state and cannot re-enter a backend.
     */

    /* --- 1. Materialize candidates up to the cap --------------------------
     *
     * Before any callback, never during one. pyzy's hasCandidate(i) is lazy
     * and mutating, so pathime_context_candidate() is only the plain array
     * read the header promises if every candidate the
     * PATHIME_OPT_MAX_CANDIDATES cap allows has already been fetched. That is
     * the obligation from the API round, and this call is where it is
     * discharged; candidates.cc owns the rest.
     *
     * It runs before the projection because the projection publishes the
     * count, and a count published before the list it describes would be a lie
     * for exactly as long as it took to fix.
     */
    materialize_candidates(ctx);

    /* --- 2. Project the structured model onto the flat value ---------------
     *
     * Rebuilt wholesale, never patched incrementally. The pathime_str_t
     * members point into this context's own storage, which is what backs the
     * header's promise that they stay valid until the next call mutating this
     * context; std::string::c_str() is never NULL and is "" when the string is
     * empty, which is exactly what a zero-length pathime_str_t must point at.
     *
     * Whether anything changed is decided here, by comparing what the
     * projection produced against what was published last time. One string
     * comparison per keystroke is the price of an exact answer, and it buys
     * the header's promise that composition_changed means the composition
     * changed — a promise a client leans on to avoid redrawing.
     */
    const std::string previous_preedit = ctx->preedit;
    const size_t previous_settled = ctx->composition.preedit_settled;
    const size_t previous_candidates = ctx->composition.candidate_count;
    const size_t previous_cursor = ctx->composition.candidate_cursor;

    pathime::project_composition(ctx->model, &ctx->preedit, &ctx->composition);
    ctx->composition.candidate_count = ctx->model.candidates.size();
    ctx->composition.candidate_cursor = ctx->model.cursor;

    /*
     * The cursor is part of the comparison, not just part of the value. A
     * client draws its highlight from this struct and is told to trust nothing
     * it set itself, which only works if a cursor that moved is announced —
     * and a move with no other effect is possible in principle, for a backend
     * that tracks the hover without previewing it. Today every backend that
     * has a cursor also rewrites the active span, so this term never decides
     * the answer alone; it is here so that the promise does not depend on that
     * remaining true.
     */
    const bool changed = force ||
                         ctx->preedit != previous_preedit ||
                         ctx->composition.preedit_settled != previous_settled ||
                         ctx->composition.candidate_count != previous_candidates ||
                         ctx->composition.candidate_cursor != previous_cursor;

    /* --- 4. Dispatch, in the fixed order ----------------------------------
     *
     * delete_surrounding_text before commit_text within one dispatch, and
     * composition_changed always last. The first half is fixed by the header
     * to make a deletion unambiguous: it is expressed against the document as
     * the engine last saw it, so it must reach the client before any commit in
     * the same dispatch moves the text out from under that frame of reference.
     * The second half is what lets a client trust that after every call its
     * view of the composition matches the engine's.
     */

    /*
     * Only where the snapshot actually covers the range. An engine wanting to
     * revise text the current snapshot does not cover abandons the revision
     * instead — it treats what is already in the document as final and
     * continues from the next input as if starting fresh — so a request that
     * does not fit is dropped rather than guessed at. That is the same
     * recovery ibus-hangul performs when its caret-sanity check fails, and it
     * is why no key is refused and no error is reported.
     *
     * The range test is spelled out rather than assumed. The header does
     * promise the engine "only ever asks to delete text it can see", and an
     * adapter is expected to have asked SurroundingTextView::can_delete_before()
     * before requesting — but that promise is the library's to keep, and the
     * cost of keeping it here is one comparison against a bug that would
     * otherwise delete the wrong text in a client's document. This condition
     * and ContextSurroundingText::can_delete_before() are one predicate in two
     * places; they change together.
     */
    if (ctx->output.has_deletion && ctx->output.delete_count != 0 &&
        ctx->delete_surrounding_text != nullptr && ctx->has_surrounding &&
        range_within_snapshot(ctx, ctx->output.delete_offset,
                              ctx->output.delete_count)) {
        ctx->delete_surrounding_text(ctx->user_data,
                                     ctx->output.delete_offset,
                                     ctx->output.delete_count);
    }

    if (!ctx->output.commit.empty()) {
        /* Never NULL here: pathime_context_create() rejects a client without
         * it. Copied out first, because the callback is entitled to call back
         * into the callback-safe queries and must not see a half-cleared
         * Output. */
        const std::string commit = ctx->output.commit;
        ctx->output.clear();

        /*
         * A commit invalidates the surrounding-text snapshot the moment it
         * lands: the document now contains text the snapshot does not describe.
         * Until the client supplies a fresh one the engine cannot see what it
         * just inserted, which is precisely the obligation
         * PATHIME_REQUIRES_SURROUNDING_TEXT describes as stronger than it
         * sounds.
         */
        ctx->has_surrounding = false;
        ctx->surrounding_text.clear();
        ctx->surrounding_cursor = 0;

        pathime_str_t text;
        text.bytes = commit.c_str();
        text.len = commit.size();
        ctx->commit_text(ctx->user_data, text);
    }
    ctx->output.clear();

    if (changed && ctx->composition_changed != nullptr) {
        /* Last, and only if the client supplied it — one with no way to
         * display composition state may omit it. By the time this runs the
         * composition it is handed is complete and every candidate the cap
         * allows is already in hand, which is the entire basis on which
         * pathime_context_candidate() is callback-safe. */
        ctx->composition_changed(ctx->user_data, &ctx->composition);
    }
}

}  // namespace pathime
