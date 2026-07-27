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

#include "context.h"
#include "engine.h"
#include "init.h"

namespace pathime {

/**
 * Fetch every candidate the PATHIME_OPT_MAX_CANDIDATES cap allows into
 * ctx->candidates, and drop any past it. Defined in candidates.cc, which owns
 * everything candidate-shaped; declared here because refresh_composition()
 * below is its only caller and there is no candidates.h to put it in.
 *
 * A src/candidates.h is the tidier home for this the moment the pump has real
 * work to do and more than one entry point to offer — see the note at the top
 * of candidates.cc.
 */
void materialize_candidates(pathime_context_t *ctx);

}  // namespace pathime

namespace {

/* ---------------------------------------------------------------------------
 * UTF-8 validation
 * ------------------------------------------------------------------------- */

/**
 * Validate @a len bytes of UTF-8 and count the Unicode scalar values in them.
 *
 * Rejects everything the public header's Text convention excludes: an embedded
 * NUL (U+0000 is not representable in this API, in either direction),
 * truncated and malformed sequences, overlong forms, surrogate halves, and
 * anything above U+10FFFF. A slice is a run of text, not a buffer that might
 * end early, so @a len is authoritative and a NUL inside it is an error rather
 * than a terminator.
 *
 * TODO(impl): this belongs in utf8.cc with the rest of the encoding boundary
 * (TODO.md §2, Finding 4). utf8.h is deliberately empty until its types are
 * designed alongside the composition representation, so the one caller that
 * needs the check today carries a file-local copy rather than pre-empting that
 * round. Move it, unchanged, when utf8.* lands.
 */
bool utf8_validate_and_count(const char *bytes, size_t len, size_t *out_scalars)
{
    /* The lowest scalar value each sequence length may legally encode; index 0
     * is unused. A value below its entry is an overlong form. */
    static const uint32_t kLowestLegal[5] = { 0u, 0x0u, 0x80u, 0x800u, 0x10000u };

    size_t scalars = 0;
    size_t i = 0;

    while (i < len) {
        const unsigned char lead = static_cast<unsigned char>(bytes[i]);
        size_t seq_len;
        uint32_t scalar;

        if (lead == 0x00u) {
            return false;  /* U+0000 is not representable in this API. */
        } else if (lead < 0x80u) {
            seq_len = 1;
            scalar = lead;
        } else if ((lead & 0xE0u) == 0xC0u) {
            seq_len = 2;
            scalar = lead & 0x1Fu;
        } else if ((lead & 0xF0u) == 0xE0u) {
            seq_len = 3;
            scalar = lead & 0x0Fu;
        } else if ((lead & 0xF8u) == 0xF0u) {
            seq_len = 4;
            scalar = lead & 0x07u;
        } else {
            return false;  /* A stray continuation byte, or a 5-byte form. */
        }

        if (len - i < seq_len) {
            return false;  /* Truncated: the slice ends mid-sequence. */
        }
        for (size_t k = 1; k < seq_len; ++k) {
            const unsigned char cont = static_cast<unsigned char>(bytes[i + k]);
            if ((cont & 0xC0u) != 0x80u) {
                return false;
            }
            scalar = (scalar << 6) | (cont & 0x3Fu);
        }

        if (scalar < kLowestLegal[seq_len]) {
            return false;  /* Overlong. */
        }
        if (scalar > 0x10FFFFu) {
            return false;
        }
        if (scalar >= 0xD800u && scalar <= 0xDFFFu) {
            return false;  /* A surrogate half is not a scalar value. */
        }

        i += seq_len;
        ++scalars;
    }

    if (out_scalars != nullptr) {
        *out_scalars = scalars;
    }
    return true;
}

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
     * TODO(impl): create the backend's per-context handle here
     * (HangulInputContext *, anthy_context_t, PyZy::InputContext *) through
     * backend.h, and on failure unregister, delete, and report
     * PATHIME_ERROR_BACKEND with out_ctx untouched. Its type waits on the
     * composition representation (TODO.md §3, question 1).
     */

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

    /* TODO(impl): destroy the backend's per-context handle before the object
     * goes, once backend.h defines one. Each of the three is one owned handle,
     * caller-destroyed (TODO.md §2, Finding 3). */

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
     * Value legality, last. Keysyms are otherwise passed through unvalidated:
     * the X11 keysym space is open-ended and no useful membership test exists,
     * so an unrecognized value is not an error — engines dispatch on the ones
     * they know and report the rest unhandled, which is also what a client
     * wants for keys it has invented. Zero is the one thing checked, because
     * it names no key at all.
     */
    if (event->keysym == 0) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    /*
     * TODO(impl): this is where the mutating sequence runs, and it is the
     * whole of the engine's input path:
     *
     *   1. Route the event through the engine-agnostic key layer (keys.cc) —
     *      modifiers, the PATHIME_KEY_* named keys, the handled/unhandled
     *      verdict. The backends accept only finished input (TODO.md §2,
     *      Finding 6), so all of this is ours: anthy wants completed kana,
     *      pyzy takes only [a-z] and apostrophe, libhangul takes a US-QWERTY
     *      int with uppercase meaning Shift.
     *   2. For anthy, through the composing front end first
     *      (engines/anthy/romaji.cc), which is the only per-engine state
     *      machine that has to run before its backend sees anything.
     *   3. Let the backend mutate, through backend.h.
     *   4. End in pathime::refresh_composition(ctx, false), which assembles,
     *      materializes and dispatches, in that order — including when the
     *      event is reported unhandled, since an engine may absorb a key into
     *      its composition state, emit the resulting text, and still hand the
     *      original key back.
     *
     * Until that exists every key is reported unhandled and PATHIME_OK is
     * returned. Nothing was mutated, so there is nothing to assemble and no
     * callback to dispatch, and a client sees exactly the behaviour the header
     * documents for a key no engine acts on. out_handled is already false from
     * the top of the function.
     */
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
    if (!utf8_validate_and_count(text.bytes, text.len, &scalars)) {
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
    const bool was_empty = ctx->preedit.empty() &&
                           ctx->auxiliary.empty() &&
                           ctx->candidates.empty();

    /*
     * TODO(impl): the backend's own reset goes here — hangul_ic_reset(),
     * anthy_reset_context(), PyZy::InputContext::reset() — through backend.h.
     * One thing precedes it: reset does not commit, so an engine that must
     * preserve text commits it explicitly first, through ctx->commit_text, as
     * part of handling the reset. The three backends differ on flush
     * semantics; consult docs/[engine]-mapping.md rather than re-deriving
     * them.
     */

    /*
     * Clearing the flat storage *is* the reset today. Once the structured
     * model exists (composition.h, TODO.md §3 question 1) it is that model
     * which is cleared here, and the flat value follows from the assembly step
     * inside refresh_composition() rather than being cleared directly.
     */
    ctx->preedit.clear();
    ctx->auxiliary.clear();
    ctx->candidates.clear();
    ctx->composition.preedit_settled = 0;
    ctx->indeterminate = false;

    /*
     * force is true on the recovery path: a context whose composition state is
     * indeterminate cannot be said to have been "already empty", and the
     * client's view of it must be replaced either way.
     *
     * The second term is the placeholder half. refresh_composition() decides
     * for itself whether anything changed, but only from what the assembly
     * step tells it — and because the clearing above happens here rather than
     * inside that step, it cannot see it. When assembly lands this reduces to
     * `was_indeterminate` alone, and the header's "unless the composition was
     * already empty" promise is kept by the change detection.
     */
    pathime::refresh_composition(ctx, was_indeterminate || !was_empty);
    return PATHIME_OK;
}

/* ===========================================================================
 * Post-mutation assembly
 * ======================================================================== */

namespace pathime {

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

    /* --- 1. Assemble the flat value from the structured model -------------
     *
     * TODO(impl): project the structured composition (composition.cc) into
     * ctx->preedit, ctx->auxiliary and ctx->composition.preedit_settled. Every
     * backend keeps state the flat value cannot hold (TODO.md §2, Finding 1):
     * anthy has N segments, each with its own candidate array, plus an
     * active-segment index; pyzy's preedit is three parts with the middle one
     * provisional and its own focused-candidate index; libhangul exposes only
     * the trailing mutable syllable, so the settled prefix is accumulated on
     * our side. pyzy contributes through its observer's dirty flags rather
     * than by being polled (Finding 5), which is what reconciles its push
     * model with the other two pull-only ones into one atomic value.
     *
     * preedit_settled is set here too: it is the boundary between the settled
     * prefix and the still-mutable region, in Unicode scalar values, and it
     * may never exceed the scalar count of the assembled preedit.
     */

    /* --- 2. Materialize candidates up to the cap --------------------------
     *
     * Before any callback, never during one. pyzy's hasCandidate(i) is lazy
     * and mutating, so pathime_context_candidate() is only the plain array
     * read the header promises if every candidate the
     * PATHIME_OPT_MAX_CANDIDATES cap allows has already been fetched. That is
     * the obligation from the API round, and this call is where it is
     * discharged; candidates.cc owns the rest.
     */
    materialize_candidates(ctx);

    /* --- 3. Republish the flat value --------------------------------------
     *
     * Rebuilt in place, never patched incrementally. The pathime_str_t members
     * point into this context's own storage, which is what backs the header's
     * promise that they stay valid until the next call mutating this context;
     * std::string::c_str() is never NULL and is "" when the string is empty,
     * which is exactly what a zero-length pathime_str_t must point at.
     *
     * struct_size is filled in by the library because the library owns this
     * struct — the caller never allocates one — so the value reports how many
     * bytes of it this build knows how to write.
     */
    ctx->composition.struct_size = sizeof(pathime_composition_t);
    ctx->composition.preedit.bytes = ctx->preedit.c_str();
    ctx->composition.preedit.len = ctx->preedit.size();
    ctx->composition.auxiliary.bytes = ctx->auxiliary.c_str();
    ctx->composition.auxiliary.len = ctx->auxiliary.size();
    ctx->composition.candidate_count = ctx->candidates.size();

    /*
     * TODO(impl): the assembly step reports whether anything actually changed
     * — pyzy through the observer's dirty flags, the pull-only backends by
     * comparing what they project. Until there is a structured model to
     * assemble from, nothing here can have changed, so `force` alone decides
     * and the comparison it stands in for would cost two string copies on the
     * hottest path in the library to always answer false.
     */
    const bool changed = force;

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

    /* TODO(impl): dispatch any pending deletion through
     * ctx->delete_surrounding_text, with offset relative to
     * ctx->surrounding_cursor in scalar values and a count that is never 0,
     * and only where ctx->has_surrounding covers the range. An engine wanting
     * to revise text the current snapshot does not cover abandons the revision
     * instead: it discards the state that was to be revised and treats what is
     * already in the document as final, requesting no deletion and refusing no
     * key. The pending record itself waits on backend.h. */

    /* TODO(impl): dispatch any pending commit through ctx->commit_text, which
     * is never NULL here — pathime_context_create() rejects a client without
     * it. A commit invalidates the surrounding-text snapshot the moment it
     * lands, so ctx->has_surrounding is cleared alongside: until the client
     * supplies a fresh one the engine can no longer see the text it just
     * inserted. */

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
