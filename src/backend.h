/*
 * The internal engine interface — the seam between the core (everything
 * directly in src/) and the adapters (everything under src/engines/). Core
 * code never names a vendor type; engine code never touches the public API
 * surface; everything an adapter provides crosses through here.
 *
 * Its shape follows from the composition model (composition.h), as
 * docs/source-layout.md said it would: the interface exists mostly to move
 * that structure and the events that change it.
 *
 * ---------------------------------------------------------------------------
 * What the interface is, and what it deliberately is not
 * ---------------------------------------------------------------------------
 *
 * Two layers, matching the two the API already has (TODO.md §2, Finding 3):
 * an EngineBackend holds what one input method shares across its contexts and
 * makes ContextBackends; a ContextBackend is one composition in flight. The
 * process-global layer is *not* here — it is init.cc's, driven once per
 * process through backend_global_init() below, because a per-instance
 * interface is the wrong shape for something that happens once.
 *
 * A backend is handed **finished input** (Finding 6). The key layer is ours:
 * validation, modifiers, the PATHIME_KEY_* keys, and any composing front end
 * sit above this interface. What crosses it is a KeyEvent the adapter is free
 * to reject, because "handled" is a judgement only the backend can make — but
 * a backend never sees a key release, an unfocused context, or a malformed
 * event.
 *
 * A backend mutates the Composition **in place** and appends to an Output. It
 * does not dispatch callbacks, does not know the client exists, does not
 * decide ordering, and does not materialize candidates beyond what it is asked
 * for. Those are all core obligations, and keeping them out of the adapters is
 * what stops three backends from each inventing their own answer.
 *
 * ---------------------------------------------------------------------------
 * The rules an adapter must obey
 * ---------------------------------------------------------------------------
 *
 *  1. **Copy at the seam.** Everything the three vendor libraries return is
 *     borrowed and volatile — valid only until their next mutating call
 *     (Finding 4). Nothing may be aliased into the Composition; utf8.h has the
 *     conversions, including the UCS-4 one libhangul needs.
 *
 *  2. **Positions are scalar values.** Any offset an adapter puts into an
 *     Output is in Unicode scalar values, never bytes. pyzy's cursor() is a
 *     byte offset into raw ASCII input and is not a position in this sense at
 *     all.
 *
 *  3. **The active span is the only one addressable.** Greedy left-to-right
 *     resolution, no segment navigation or resizing. An adapter with more
 *     structure than that — anthy has N segments and an active index — keeps
 *     it privately and reports the three strings.
 *
 *  4. **Options are pulled, not pushed.** An adapter reads what it needs
 *     through OptionReader when it needs it. Resolution across the four tiers
 *     is options.cc's and is not an adapter's business.
 *
 * Per-backend gotchas (flush semantics, the unknown-keyboard crash, the
 * two-call length protocol) are in the docs/ mapping notes — consult those
 * rather than re-deriving.
 */

#ifndef LIBPATHIME_SRC_BACKEND_H
#define LIBPATHIME_SRC_BACKEND_H

#include <cstdint>
#include <memory>

#include <pathime/pathime.h>

#include "composition.h"

namespace pathime {

/**
 * One key press, after the core key layer has validated it. The fields are
 * pathime_key_event_t's, minus the struct_size a validated event no longer
 * needs — an adapter should not be re-checking an ABI concern.
 *
 * `layout_key` is the physical key as a US-QWERTY keysym, or 0 when the client
 * had none to report. Only Hangul consults it, and only as key position.
 */
struct KeyEvent {
    uint32_t keysym = 0;
    uint32_t layout_key = 0;
    uint32_t modifiers = 0;

    /** True if @a bit (a PATHIME_MOD_* value) is set. */
    bool has(uint32_t bit) const { return (modifiers & bit) != 0; }

    /**
     * The keysym to dispatch on when a backend wants position rather than
     * character — libhangul's whole input model. Falls back to the keysym when
     * the client reported no physical key, which every engine tolerates.
     */
    uint32_t position_key() const { return layout_key != 0 ? layout_key : keysym; }
};

/**
 * Read access to resolved option values, handed to an adapter so it can pull
 * what it needs without knowing that options have levels, tiers, or a store.
 *
 * Implemented by context.cc over resolve_option_number(); an adapter sees only
 * the effective value, already capped where the client's capabilities cap it.
 * Values are pulled at the moment they matter rather than pushed on change,
 * because the alternative is every adapter caching state it must then
 * invalidate — and the header's rule is that a change takes effect
 * immediately, which pulling gives for free.
 */
class OptionReader {
public:
    virtual ~OptionReader() = default;

    /** The resolved value of a BOOL, INT, ENUM or FLAGS option. */
    virtual int64_t number(pathime_option_t option) const = 0;

    /** Convenience for BOOL options. */
    bool flag(pathime_option_t option) const { return number(option) != 0; }
};

/**
 * What an adapter may ask about the client's document before deciding to
 * revise it.
 *
 * Deliberately one question, not a view of the text. An adapter that wants to
 * delete something it committed a moment ago has to know *first* whether the
 * deletion will actually land, because the header's recovery when it will not
 * is not "carry on and hope": it is to abandon the revision, treat what is
 * already in the document as final, and continue as if starting fresh. That
 * recovery is backend-specific — hangul has to reset its input context so the
 * next key begins a new syllable — so it cannot be done for the adapter by the
 * dispatch that drops the request, which runs after process_key() has already
 * decided what to commit.
 *
 * Why not a full document view: nothing needs one. Reading preceding context
 * is a Hanja feature and Hanja is out of scope (TODO.md §1), so exposing the
 * text would be a concept carried for no consumer. If a real one appears this
 * is the place to widen.
 */
class SurroundingTextView {
public:
    virtual ~SurroundingTextView() = default;

    /**
     * True if the client's snapshot is present and covers @a count scalar
     * values immediately before the cursor — that is, if an
     * Output::request_deletion(-count, count) issued now would be dispatched
     * rather than dropped.
     *
     * A @a count of 0 is true whether or not a snapshot exists: an adapter
     * with nothing to revise is never blocked.
     */
    virtual bool can_delete_before(size_t count) const = 0;
};

/**
 * One composition in flight: the per-context handle each vendor library has
 * (HangulInputContext *, anthy_context_t, PyZy::InputContext *), wrapped.
 *
 * Every method may mutate @a model and append to @a out. Neither is cleared
 * first — `out` arrives empty because the caller cleared it, and `model` is
 * the state being evolved. An adapter that produces nothing leaves both alone.
 */
class ContextBackend {
public:
    virtual ~ContextBackend() = default;

    /**
     * Offer one key.
     *
     * @a doc answers whether a revision of already-committed text would reach
     * the client. Only the Hangul adapter under PATHIME_HANGUL_PREEDIT_NONE
     * consults it; every other adapter ignores it, because every other adapter
     * holds its composition in the preedit where the document is not involved.
     *
     * @return true if the backend took responsibility for the event, in the
     *         public API's sense: the client must not also process it through
     *         its normal text-input path. This is independent of whether any
     *         output was produced — a backend may absorb a key and emit
     *         nothing, or decline a key having already committed text.
     */
    virtual bool process_key(const KeyEvent &key,
                             const OptionReader &options,
                             const SurroundingTextView &doc,
                             Composition *model,
                             Output *out) = 0;

    /**
     * Discard transient state and return to neutral. Must not commit
     * implicitly; an adapter that has text it must not lose puts it in @a out
     * explicitly, which is the header's rule for pathime_context_reset().
     *
     * The caller clears @a model afterward regardless, so an adapter need only
     * deal with its own library's state.
     */
    virtual void reset(Composition *model, Output *out) = 0;

    /**
     * Choose candidate @a index of the active span, settling it and advancing
     * greedily to whatever follows.
     *
     * The index is already known to be in range. Returning
     * PATHIME_ERROR_UNSUPPORTED is legitimate for an engine that produces no
     * candidates at all, which is Hangul's case.
     */
    virtual pathime_status_t select_candidate(size_t index,
                                              const OptionReader &options,
                                              Composition *model,
                                              Output *out) = 0;

    /**
     * Move the hover to candidate @a index of the active span, settling
     * nothing. The client navigating its candidate list ends here.
     *
     * The distinction from select_candidate() is what the two exist to draw:
     * this changes what is *shown* and can be undone by moving back, so an
     * implementation updates model->cursor and — for a backend that previews
     * its candidates — rewrites model->active to match. It never commits, so
     * there is no Output.
     *
     * The index is already known to be in range, so an implementation need not
     * bounds-check it. Returning PATHIME_ERROR_UNSUPPORTED is legitimate for a
     * backend with no candidates to hover, which is Hangul's case, and that is
     * the default below: an adapter that does nothing here would otherwise
     * report success and leave the cursor and the preedit disagreeing.
     *
     * The caller re-materializes candidates and re-projects afterward.
     */
    virtual pathime_status_t set_cursor(size_t index,
                                        const OptionReader &options,
                                        Composition *model)
    {
        (void)index;
        (void)options;
        (void)model;
        return PATHIME_ERROR_UNSUPPORTED;
    }

    /**
     * A resolved option value changed for this context. Re-derive whatever
     * depends on it, in place.
     *
     * This does not contradict rule 4. Options are still pulled — nothing is
     * pushed in, and the adapter reads through @a options exactly as it does
     * anywhere else. What this call carries is not a value but a *moment*: the
     * public header promises a change takes effect immediately, and an adapter
     * whose backend has already converted something cannot honour that by
     * pulling next time it is asked, because there may be no next time until
     * the user types again.
     *
     * Default: nothing. That is the honest answer for an adapter that consults
     * its options at the point of use and holds no derived state between keys,
     * which is both hangul and anthy. pyzy overrides it because its properties
     * are pushed into a live PyZy::InputContext and its candidate list is
     * regenerated from them.
     *
     * The caller re-materializes candidates and re-projects afterward, so an
     * implementation only has to update @a model and @a out.
     */
    virtual void options_changed(const OptionReader &options,
                                 Composition *model,
                                 Output *out)
    {
        (void)options;
        (void)model;
        (void)out;
    }

    /**
     * Fill model->candidates for the active span, appending until the backend
     * runs out or the list reaches @a cap, and never reordering what is
     * already there.
     *
     * Separate from the mutating calls because of the eager-materialization
     * obligation: pathime_context_candidate() is documented callback-safe,
     * which is only true if every candidate the cap allows exists before
     * composition_changed is dispatched. pyzy's hasCandidate(i) is lazy *and*
     * mutating, so this is the one method that may legitimately be expensive.
     *
     * Running out before the cap is normal and is not an error.
     */
    virtual void materialize_candidates(size_t cap,
                                        const OptionReader &options,
                                        Composition *model) = 0;
};

/**
 * What one input method shares across every context using it: loaded
 * dictionaries, compiled tables, user history. Comparatively expensive, which
 * is why the public API has engines at all.
 */
class EngineBackend {
public:
    virtual ~EngineBackend() = default;

    /** A fresh composition context, or nullptr if the backend cannot make one. */
    virtual std::unique_ptr<ContextBackend> create_context(const OptionReader &options) = 0;
};

/* ---------------------------------------------------------------------------
 * The process-global layer
 *
 * Driven once per process by init.cc, not by any instance. Each function is
 * defined by the adapter directory for its backend and declared here so that
 * init.cc can call it without naming a vendor type; the gating on
 * PATHIME_WITH_* lives at the call site.
 *
 * Every init hook takes the same pair, both absolute and both NUL-terminated
 * UTF-8: @a data_dir, which that backend may read and write, and
 * @a resource_dir, which holds the read-only files the library ships and which
 * it must only read. A backend needing neither still takes both, so that the
 * hook is one shape rather than three. Returning false means the backend's
 * prerequisites are not there and reaches the client as pathime_has_engine()
 * answering false; it never fails pathime_init().
 * ------------------------------------------------------------------------- */

#if PATHIME_WITH_HANGUL
/**
 * Hangul has no process-global setup in this build and this is a no-op that
 * cannot fail, which is worth stating rather than leaving to be discovered:
 * hangul_init()/hangul_fini() exist only under ENABLE_EXTERNAL_KEYBOARDS,
 * which the top-level CMakeLists turns off, and the nine built-in layouts are
 * static tables. It exists so that init.cc's shape does not have a hole in it.
 */
bool hangul_global_init(const char *data_dir, const char *resource_dir);
void hangul_global_shutdown();

/** A new engine for PATHIME_ENGINE_HANGUL. */
std::unique_ptr<EngineBackend> hangul_create_engine();
#endif

#if PATHIME_WITH_ANTHY
bool anthy_global_init(const char *data_dir, const char *resource_dir);
void anthy_global_shutdown();
std::unique_ptr<EngineBackend> anthy_create_engine();
#endif

#if PATHIME_WITH_PYZY
bool pyzy_global_init(const char *data_dir, const char *resource_dir);
void pyzy_global_shutdown();

/**
 * One backend, two engine ids: pyzy fixes its InputType when its context is
 * created, so the phonetic scheme is decided here rather than by an option.
 * @a id must be PATHIME_ENGINE_PINYIN or PATHIME_ENGINE_BOPOMOFO.
 */
std::unique_ptr<EngineBackend> pyzy_create_engine(pathime_engine_id_t id);
#endif

}  // namespace pathime

#endif /* LIBPATHIME_SRC_BACKEND_H */
