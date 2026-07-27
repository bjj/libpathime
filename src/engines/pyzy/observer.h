/*
 * The dirty-flag Observer (Finding 5). pyzy fires six granular Observer
 * callbacks synchronously *during* a mutation call; anthy and libhangul are
 * pull-only. This small per-context Observer only sets dirty flags — it
 * never assembles state or dispatches anything — and the post-call assembly
 * step in context.cc reads the flags to build one atomic composition value.
 * No event loop is involved. (ibus-pinyin does not buffer its observer
 * callbacks, so it is deliberately not the model here.)
 */

#ifndef LIBPATHIME_SRC_ENGINES_PYZY_OBSERVER_H
#define LIBPATHIME_SRC_ENGINES_PYZY_OBSERVER_H

namespace pathime {

/* Observer to be defined against PyZy::InputContext::Observer. */

}  // namespace pathime

#endif /* LIBPATHIME_SRC_ENGINES_PYZY_OBSERVER_H */
