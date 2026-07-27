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
 */

#include <pathime/pathime.h>
