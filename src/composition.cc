/*
 * Implementation of the structured composition model's projection to
 * pathime_composition_t. See composition.h for the model and the reasoning
 * behind its shape.
 *
 * There is very little here, and that is the point: the model was chosen so
 * that the projection is a concatenation and a scalar count. Every rule the
 * API fixes about the flat value — that preedit_settled is the boundary
 * between the settled prefix and the still-mutable region, that positions are
 * scalar values and never bytes, that an empty field means "not present" —
 * falls out of the shape rather than being enforced by code here.
 */

#include "composition.h"

#include "utf8.h"

namespace pathime {

void project_composition(const Composition &model,
                         std::string *preedit_storage,
                         pathime_composition_t *out)
{
    /*
     * Built into storage the context owns, because the pathime_str_t members
     * below borrow from it with the ordinary lifetime — valid until the next
     * call that mutates this context. Assigning rather than swapping keeps the
     * capacity across refreshes, which matters on a path that runs once per
     * keystroke.
     */
    preedit_storage->assign(model.settled);
    preedit_storage->append(model.active);
    preedit_storage->append(model.tail);

    /* The library owns this struct, so it reports the size it wrote rather
     * than reading one the caller supplied. */
    out->struct_size = sizeof(pathime_composition_t);

    out->preedit.bytes = preedit_storage->c_str();
    out->preedit.len = preedit_storage->size();

    /*
     * In scalar values, not bytes — the one place this projection could
     * silently be wrong, since the two agree for the ASCII a test is most
     * likely to reach for and diverge for every script the library actually
     * serves. The settled prefix is well-formed UTF-8 by construction: every
     * path that writes it copies through utf8.h.
     */
    out->preedit_settled = utf8_scalar_count(model.settled.data(), model.settled.size());

    /*
     * candidate_count is deliberately not written here. Materialization
     * happens after this assembly and before any callback (context.cc
     * sequences it, candidates.cc performs it), so the count is only known
     * later; writing a stale one now would be worse than leaving it to its
     * owner.
     */
}

}  // namespace pathime
