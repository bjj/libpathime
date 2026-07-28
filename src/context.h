/*
 * The input context object — the per-context layer of the two-layer lifetime
 * (TODO.md §2, Finding 3): one independently editable client destination and
 * the engine state belonging to it.
 *
 * This header defines `struct pathime_context` because candidates.cc and
 * options.cc both reach inside it, and it carries the storage behind the two
 * lifetime promises the public header makes about borrowed data:
 *
 *  - pathime_context_composition() and pathime_context_candidate() hand back
 *    pointers valid "until the next call that mutates the same input context".
 *    Everything they point into is owned here, and rebuilt in place by the
 *    post-mutation assembly step in context.cc.
 *  - Everything a backend returns is volatile until its next mutating call
 *    (Finding 4), so it is copied into these members at the seam, never
 *    aliased.
 */

#ifndef LIBPATHIME_SRC_CONTEXT_H
#define LIBPATHIME_SRC_CONTEXT_H

#include <cstddef>
#include <memory>
#include <string>

#include <pathime/pathime.h>

#include "backend.h"
#include "composition.h"
#include "options.h"

/**
 * An input context handle. Defined in the global namespace to complete the
 * incomplete type the public header's typedef declares.
 */
struct pathime_context {
    /** The engine serving this context. Outlives it, by the public contract. */
    pathime_engine_t *engine = nullptr;

    /** Passed back unchanged to every callback and by pathime_context_user_data(). */
    void *user_data = nullptr;

    /*
     * The client's callbacks, resolved once at creation.
     *
     * The table itself is borrowed and, the header promises, stays valid and
     * unchanged for the context's lifetime — so resolving it once is licensed,
     * and it is the honest way to apply pathime_client_t::struct_size: a
     * member the caller's struct is too small to contain is nullptr here, and
     * every later read is a plain pointer test rather than repeated offset
     * arithmetic. A nullptr means the client does not support that operation.
     */
    void (*commit_text)(void *, pathime_str_t) = nullptr;
    void (*delete_surrounding_text)(void *, ptrdiff_t, size_t) = nullptr;
    void (*composition_changed)(void *, const pathime_composition_t *) = nullptr;

    /**
     * Focus gates input and only input: an unfocused context rejects
     * process_key and select_candidate with PATHIME_ERROR_NOT_FOCUSED, while
     * reading, configuring and resetting work regardless. Losing focus neither
     * commits nor discards — composition state below is preserved exactly.
     */
    bool focused = false;

    /**
     * Set when a call failed partway through, leaving composition state
     * indeterminate (the header's Status section: failures, as opposed to
     * rejections). It changes exactly one behaviour — pathime_context_reset()
     * dispatches composition_changed unconditionally rather than only when the
     * composition was non-empty, because a client's view of an indeterminate
     * context must be replaced either way — and reset clears it.
     */
    bool indeterminate = false;

    /**
     * The flat value handed out by pathime_context_composition() and passed to
     * composition_changed. Recomputed in place from the structured model on
     * every change, never patched incrementally (Finding 1); its pathime_str_t
     * members point into the two strings below.
     */
    pathime_composition_t composition{};

    /** Backing storage for composition.preedit. */
    std::string preedit;

    /**
     * The structured composition state (composition.h) — the model of record.
     * The flat value above is projected from it after every change and never
     * patched incrementally, which is what keeps the two from drifting.
     *
     * Its `candidates` vector is also what pathime_context_candidate() reads.
     * The list is materialized to the PATHIME_OPT_MAX_CANDIDATES cap before
     * composition_changed is dispatched, which is what makes that function
     * callback-safe — a real obligation rather than a formality, because
     * pyzy's hasCandidate(i) is lazy and mutating. See candidates.cc.
     */
    pathime::Composition model;

    /**
     * What the backend asked for during the call in progress: text to commit,
     * and any deletion of client text. Consumed and cleared by the dispatch at
     * the end of that call, so it is empty at rest.
     */
    pathime::Output output;

    /**
     * The client text snapshot from pathime_context_set_surrounding_text(),
     * and the insertion position within it in Unicode scalar values. This is
     * the frame of reference for delete_surrounding_text, and it goes stale on
     * every commit the engine performs — see PATHIME_REQUIRES_SURROUNDING_TEXT.
     *
     * @a has_surrounding distinguishes "the client supplied an empty document"
     * from "the client has supplied nothing", which decide differently: an
     * engine that cannot see the text it wants to revise abandons the revision
     * rather than guessing.
     */
    std::string surrounding_text;
    size_t surrounding_cursor = 0;
    bool has_surrounding = false;

    /** The context level of the two-level store: tier 1, overriding the engine. */
    pathime::OptionStore options;

    /**
     * The adapter's per-context handle, wrapping whatever its vendor library
     * has — HangulInputContext *, anthy_context_t, PyZy::InputContext *. One
     * owned handle each, destroyed with this context.
     */
    std::unique_ptr<pathime::ContextBackend> backend;
};

namespace pathime {

/**
 * The OptionReader an adapter is handed: read access to this context's
 * resolved option values, with none of the machinery behind them.
 *
 * It resolves through all four tiers and applies the capability capping, so an
 * adapter sees the effective value and nothing else — it never learns that
 * options have levels, and never has to invalidate a cached copy, which is why
 * backend.h makes options pulled rather than pushed.
 */
class ContextOptions : public OptionReader {
public:
    explicit ContextOptions(const pathime_context_t *ctx) : ctx_(ctx) {}
    int64_t number(pathime_option_t option) const override;

private:
    const pathime_context_t *ctx_;
};

/**
 * The SurroundingTextView an adapter is handed: the one question it may ask
 * about the client's document, answered from this context's snapshot.
 *
 * Its predicate is deliberately the same one refresh_composition() applies
 * when it decides whether to dispatch a deletion, so an adapter that asks
 * before requesting gets a truthful answer rather than an optimistic one.
 * Keeping the two in agreement is this class's whole job — if the dispatch
 * condition changes, this changes with it.
 */
class ContextSurroundingText : public SurroundingTextView {
public:
    explicit ContextSurroundingText(const pathime_context_t *ctx) : ctx_(ctx) {}
    bool can_delete_before(size_t count) const override;

private:
    const pathime_context_t *ctx_;
};

/**
 * Rebuild the flat composition value from the structured model, materialize
 * candidates up to the cap, and dispatch composition_changed — in that order,
 * and only after the backend has finished mutating.
 *
 * This is the discipline context.cc exists to enforce, and every mutating
 * entry point ends here: the ordering is what lets a callback observe one
 * consistent, fully materialized state, which is the entire basis of the
 * callback-safe set documented at the top of the public header.
 *
 * @param force Dispatch even when nothing changed. Used by
 *              pathime_context_reset() on the recovery path, where an
 *              indeterminate context cannot be said to have been "already
 *              empty".
 */
void refresh_composition(pathime_context_t *ctx, bool force);

}  // namespace pathime

#endif /* LIBPATHIME_SRC_CONTEXT_H */
