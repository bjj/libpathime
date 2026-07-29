/*
 * The candidate pump: everything candidate-shaped that the core owns.
 *
 * What belongs here is the seam between context.cc, which sequences a mutating
 * call, and candidates.cc, which owns the list and the hover.
 */

#ifndef PATHIME_CANDIDATES_H
#define PATHIME_CANDIDATES_H

#include <cstddef>

struct pathime_context;

namespace pathime {

/**
 * Fill ctx->model.candidates with every candidate PATHIME_OPT_MAX_CANDIDATES
 * allows, dropping any past it, and clamp the cursor into what survives.
 *
 * Called by refresh_composition() after the backend has finished mutating and
 * before any callback is dispatched. That ordering is the eager-materialization
 * obligation and is load-bearing rather than tidy: pathime_context_candidate()
 * is documented callback-safe, which is only true if the list is complete
 * before composition_changed goes out.
 */
void materialize_candidates(pathime_context *ctx);

}  // namespace pathime

#endif /* PATHIME_CANDIDATES_H */
