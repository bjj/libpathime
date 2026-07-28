/*
 * The structured composition model and its projection to the flat public
 * value. This is docs/design-history.md §3, question 1, answered — designed against all
 * three mapping docs at once, as docs/source-layout.md required.
 *
 * ---------------------------------------------------------------------------
 * The shape, and why it is this one
 * ---------------------------------------------------------------------------
 *
 * Every backend keeps state the flat {preedit, preedit_settled,
 * candidates} value cannot hold (docs/design-history.md §2, Finding 1). Laid side by side,
 * though, the three describe the same three-part picture:
 *
 *              settled                active              tail
 *   pyzy       selectedText()      conversionText()     restText()
 *   anthy      segments < active   segment[active]      segments > active
 *   hangul     finished syllables  trailing syllable    (always empty)
 *
 * So the model is three strings and one candidate list:
 *
 *   settled   text the engine has decided and will not revisit, but has not
 *             yet committed to the client.
 *   active    the leftmost unsettled span — the one, and only one, the
 *             candidate list describes.
 *   tail      input after the active span, not yet under consideration.
 *
 * The projection is then almost free: preedit is the three concatenated, and
 * preedit_settled is the scalar count of `settled`. That is exactly the API's
 * definition — the boundary between the settled prefix and the still-mutable
 * region — and it is what makes greedy left-to-right resolution work: settling
 * the active span means appending it to `settled` and promoting the next span
 * out of `tail`.
 *
 * Two things are deliberately absent, and their absence is the phone-keyboard
 * target's decision rather than an oversight. There is no segment *array* and
 * no active index: anthy has N segments but the API exposes one span at a
 * time, so the adapter keeps its own index privately and reports the three
 * strings. And there is no navigation or resizing, so nothing here can address
 * a span other than the active one.
 *
 * ---------------------------------------------------------------------------
 * The candidate cursor
 * ---------------------------------------------------------------------------
 *
 * `cursor` is docs/design-history.md §2, Finding 2 given a home. Neither anthy nor pyzy
 * durably records which candidate the user is hovering before commit — anthy
 * records only at anthy_commit_segment() time — so it is ours, and it belongs
 * here rather than in pathime_context because it is per *active span*: when a
 * span settles the next one becomes active and starts hovering its own first
 * candidate again.
 *
 * ---------------------------------------------------------------------------
 * Ownership
 * ---------------------------------------------------------------------------
 *
 * Every string here is owned. Everything a backend returns is borrowed and
 * volatile — valid only until its next mutating call (Finding 4) — so it is
 * copied in at the seam. utf8.h owns the conversions that copying needs.
 */

#ifndef LIBPATHIME_SRC_COMPOSITION_H
#define LIBPATHIME_SRC_COMPOSITION_H

#include <cstddef>
#include <string>
#include <vector>

#include <pathime/pathime.h>

namespace pathime {

/**
 * The structured composition state of one input context. Owned by the context;
 * mutated by its backend during a call; projected to the flat public value
 * afterward by context.cc, which never patches that value incrementally.
 */
struct Composition {
    /**
     * Text the engine has settled and will not revisit. Uncommitted: it is
     * still preedit as far as the client is concerned, and the API's
     * preedit_settled is its length in scalar values.
     *
     * Hangul word mode accumulates finished syllables here, which is the one
     * place the library holds text libhangul itself has already let go of.
     */
    std::string settled;

    /**
     * The leftmost unsettled span — the only span the candidate list describes
     * and the only one a selection can address.
     */
    std::string active;

    /**
     * Input after the active span. Present for anthy (later segments) and pyzy
     * (restText); always empty for hangul, which never has more than one span
     * in flight.
     */
    std::string tail;

    /**
     * Alternatives for `active`, in the order the client sees them.
     * Materialized eagerly up to PATHIME_OPT_MAX_CANDIDATES before any
     * callback is dispatched — see candidates.cc for why that is an obligation
     * rather than an optimization.
     */
    std::vector<std::string> candidates;

    /**
     * Which candidate the user is currently hovering, as an index into
     * `candidates`. Meaningless when `candidates` is empty. Reset to 0
     * whenever a new span becomes active.
     */
    size_t cursor = 0;

    /** True if the context has nothing to show: no preedit at all. */
    bool empty() const
    {
        return settled.empty() && active.empty() && tail.empty();
    }

    /** Discard everything. Does not commit; that is the caller's decision. */
    void clear()
    {
        settled.clear();
        active.clear();
        tail.clear();
        candidates.clear();
        cursor = 0;
    }

    /**
     * Settle the active span: append it to `settled` and leave `active` empty
     * for the backend to refill from `tail`. Drops the candidate list and the
     * cursor, which described the span that just stopped being active.
     *
     * This is greedy left-to-right resolution in one function, and it is the
     * only way text moves from unsettled to settled.
     */
    void settle_active()
    {
        settled += active;
        active.clear();
        candidates.clear();
        cursor = 0;
    }

    /** The whole preedit, in display order. */
    std::string preedit() const { return settled + active + tail; }
};

/**
 * What a backend produced during one mutating call, beyond the composition
 * itself: text to insert into the client's document, and any deletion of text
 * already there.
 *
 * These are requests, not results. context.cc dispatches them through
 * pathime_client in the order the public header fixes — every
 * delete_surrounding_text before any commit_text, and composition_changed
 * last — so a backend states what it wants without knowing that order.
 *
 * Kept beside the composition rather than inside it because it is not state:
 * it describes one call's worth of output and is consumed and cleared by the
 * dispatch that follows.
 */
struct Output {
    /** Text to insert. Empty means no commit was produced. */
    std::string commit;

    /**
     * A deletion of already-inserted client text, expressed against the
     * snapshot from pathime_context_set_surrounding_text() with the origin at
     * the cursor that call reported. Only PATHIME_HANGUL_PREEDIT_NONE produces
     * these today.
     */
    bool has_deletion = false;
    ptrdiff_t delete_offset = 0;
    size_t delete_count = 0;

    bool empty() const { return commit.empty() && !has_deletion; }

    void clear()
    {
        commit.clear();
        has_deletion = false;
        delete_offset = 0;
        delete_count = 0;
    }

    /** Request deletion of @a count scalars starting @a offset from the cursor. */
    void request_deletion(ptrdiff_t offset, size_t count)
    {
        has_deletion = true;
        delete_offset = offset;
        delete_count = count;
    }
};

/**
 * Write @a model's flat projection into @a out, with the preedit text copied
 * into @a preedit_storage so the returned pathime_str_t points at storage the
 * context owns.
 *
 * Recomputed wholesale on every change rather than patched, which is what
 * keeps the flat value and the model from drifting: there is exactly one place
 * the projection rules live and it runs every time.
 *
 * @a out's candidate_count is *not* written here — candidates.cc owns it,
 * because materialization happens between the assembly and the dispatch.
 */
void project_composition(const Composition &model,
                         std::string *preedit_storage,
                         pathime_composition_t *out);

}  // namespace pathime

#endif /* LIBPATHIME_SRC_COMPOSITION_H */
