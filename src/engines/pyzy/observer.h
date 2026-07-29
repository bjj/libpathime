/*
 * The dirty-flag Observer. pyzy fires six granular Observer callbacks
 * synchronously *during* a mutation call; anthy and libhangul are
 * pull-only. This small per-context Observer only sets dirty flags — it
 * never assembles state or dispatches anything — and the post-call assembly
 * step in context.cc reads the flags to build one atomic composition value.
 * No event loop is involved. (ibus-pinyin does not buffer its observer
 * callbacks, so it is deliberately not the model here.)
 *
 * ---------------------------------------------------------------------------
 * What the flags are for, and why they are not just a "something changed" bit
 * ---------------------------------------------------------------------------
 *
 * The adapter reads them the moment the mutating call returns and copies only
 * the parts pyzy said changed. That matters because every accessor hands back
 * a reference into an internal buffer (docs/pyzy-mapping.md, "Encoding") and
 * copying at the seam is rule 1 of backend.h — so a flag saved is a string
 * copy saved on the library's hottest path.
 *
 * The observed callback order, for one PyZy::InputContext::insert() on a
 * FULL_PINYIN context with the bundled android.db, is:
 *
 *     inputTextChanged, cursorChanged, candidatesChanged,
 *     preeditTextChanged, auxiliaryTextChanged
 *
 * and a selectCandidate() that exhausts the input appends commitText() *after*
 * those five, by which point pyzy has already reset itself — which is exactly
 * why nothing may be assembled from inside a callback. Order is recorded here
 * as an observation, not relied upon: the adapter reads flags after the call,
 * so any order produces the same composition.
 *
 * commitText carries a payload, so it is the one callback that captures rather
 * than merely marks. It is appended to, because nothing in pyzy's contract
 * promises it fires at most once per mutation.
 */

#ifndef LIBPATHIME_SRC_ENGINES_PYZY_OBSERVER_H
#define LIBPATHIME_SRC_ENGINES_PYZY_OBSERVER_H

#include <string>

#include <PyZy/InputContext.h>

namespace pathime {

/**
 * One per PyzyContext, living as long as the PyZy::InputContext it was passed
 * to — pyzy stores the pointer and calls back through it for the whole life of
 * the context, so it is a member of the adapter rather than a temporary.
 *
 * Every method is trivial and non-throwing by construction. That is a
 * requirement, not an accident: these run inside pyzy's own call frames, part
 * way through mutations it has not finished, and pyzy is not exception-safe.
 */
class PyzyObserver final : public PyZy::InputContext::Observer {
public:
    /* --- the flags, one per callback ----------------------------------- */

    /** inputTextChanged: the raw ASCII input buffer changed. */
    bool input_text = false;

    /** cursorChanged: the byte cursor into that raw input moved. */
    bool cursor = false;

    /** preeditTextChanged: any of selectedText/conversionText/restText moved. */
    bool preedit = false;

    /** auxiliaryTextChanged: auxiliaryText() changed. */
    bool auxiliary = false;

    /** candidatesChanged: the list was regenerated, so anything already
     *  materialized from it is stale and must be dropped, not appended to. */
    bool candidates = false;

    /** commitText fired; @a commit_text holds everything it carried. */
    bool committed = false;
    std::string commit_text;

    /** True if pyzy said anything at all happened during the last call. */
    bool dirty() const
    {
        return input_text || cursor || preedit || auxiliary || candidates || committed;
    }

    /**
     * Drop every flag and the captured commit text. Called immediately before
     * each mutating call so that what is read afterward describes that call
     * and nothing earlier.
     */
    void clear();

    /* --- PyZy::InputContext::Observer ----------------------------------- */

    void commitText(PyZy::InputContext *context, const std::string &text) override;
    void inputTextChanged(PyZy::InputContext *context) override;
    void cursorChanged(PyZy::InputContext *context) override;
    void preeditTextChanged(PyZy::InputContext *context) override;
    void auxiliaryTextChanged(PyZy::InputContext *context) override;
    void candidatesChanged(PyZy::InputContext *context) override;
};

}  // namespace pathime

#endif /* LIBPATHIME_SRC_ENGINES_PYZY_OBSERVER_H */
