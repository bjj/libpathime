/*
 * The engine layer: pathime_engine_* — what one engine shares across all of
 * its contexts (the middle layer of TODO.md §2, Finding 3). Owns:
 *
 *  - the engine registry, keyed by pathime_engine_id_t and gated on the
 *    PATHIME_WITH_* macros from <pathime/config.h> — pathime_has_engine()
 *    reads it, pathime_engine_create() consults it (one backend, pyzy,
 *    supplies two engine ids: PINYIN and BOPOMOFO);
 *  - engine handle lifecycle and pathime_engine_id();
 *  - pathime_engine_requirements() — today only hangul's PREEDIT_NONE mode
 *    sets a PATHIME_REQUIRES_* bit;
 *  - the engine level of the two-level option store (the machinery itself is
 *    options.cc's).
 */

#include <pathime/pathime.h>

bool pathime_has_engine(pathime_engine_id_t id)
{
    /* Documented as false for every engine before pathime_init() has
     * succeeded — and pathime_init() is not implemented yet, so this is
     * unconditionally false, honestly. The real registry (PATHIME_WITH_*
     * gating plus runtime prerequisites such as dictionaries) replaces this
     * body when init.cc lands. */
    (void)id;
    return false;
}
