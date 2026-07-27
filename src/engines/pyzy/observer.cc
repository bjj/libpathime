/*
 * Implementation of the dirty-flag Observer declared in observer.h.
 *
 * Every function here is deliberately as small as it looks. The whole design
 * of Finding 5 rests on these bodies doing nothing but record: they execute
 * inside pyzy's own mutation frames, where the three preedit segments, the
 * candidate list and the input buffer are not all consistent with each other
 * yet. Anything that read them from in here would be reading a half-applied
 * state; the adapter reads them once, afterward, when pyzy has finished.
 */

#include "engines/pyzy/observer.h"

namespace pathime {

void PyzyObserver::clear()
{
    input_text = false;
    cursor = false;
    preedit = false;
    auxiliary = false;
    candidates = false;
    committed = false;
    commit_text.clear();
}

/*
 * The InputContext * every callback receives is the context that fired it. It
 * is ignored throughout: one observer serves exactly one context (it is a
 * member of the PyzyContext that owns that context), so the argument can only
 * ever be the one we already have, and consulting it would be the beginning of
 * assembling state from inside a callback.
 */

void PyzyObserver::commitText(PyZy::InputContext *, const std::string &text)
{
    /* Appended, not assigned: nothing in pyzy's contract limits this to one
     * call per mutation, and the payload is the one thing here that would be
     * lost rather than merely re-derived. The text is copied out of pyzy's
     * buffer at this point — backend.h rule 1 — because by the time the
     * adapter looks, pyzy has reset the context that produced it. */
    commit_text += text;
    committed = true;
}

void PyzyObserver::inputTextChanged(PyZy::InputContext *)
{
    input_text = true;
}

void PyzyObserver::cursorChanged(PyZy::InputContext *)
{
    cursor = true;
}

void PyzyObserver::preeditTextChanged(PyZy::InputContext *)
{
    preedit = true;
}

void PyzyObserver::auxiliaryTextChanged(PyZy::InputContext *)
{
    auxiliary = true;
}

void PyzyObserver::candidatesChanged(PyZy::InputContext *)
{
    candidates = true;
}

}  // namespace pathime
