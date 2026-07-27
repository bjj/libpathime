/*
 * The options machinery, shared by the engine- and context-level entry points
 * in engine.cc and context.cc:
 *
 *  - the static descriptor table — one row per pathime_option_t carrying its
 *    name, value kind, per-engine support, default, and level — backing
 *    pathime_option_count/name and pathime_engine_option_info();
 *  - the two-level value store (engine defaults, per-context overrides) with
 *    set/reset/is_set semantics;
 *  - the kind-typed setter/getter plumbing, including the kind-mismatch and
 *    unsupported-for-this-engine rejections.
 *
 * The inventory itself is settled: it is the Options section of
 * include/pathime/pathime.h, and each option's backend meaning is documented
 * in docs/*-options.md. One claim to re-verify while implementing:
 * PATHIME_OPT_PINYIN_FUZZY/_CORRECTION are scoped out of bopomofo on
 * reasoning that was not traced all the way through the bopomofo-to-pinyin
 * tables (TODO.md §1, "One claim to re-check"); widening support later is
 * additive and harmless.
 */

#ifndef LIBPATHIME_SRC_OPTIONS_H
#define LIBPATHIME_SRC_OPTIONS_H

#include <pathime/pathime.h>

namespace pathime {

/* Descriptor table and store types to be defined. */

}  // namespace pathime

#endif /* LIBPATHIME_SRC_OPTIONS_H */
