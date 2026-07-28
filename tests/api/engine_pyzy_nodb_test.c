/*
 * api.engine_pyzy_nodb — pyzy reports itself unavailable when its database is
 * missing, and takes nothing else down with it.
 *
 * The negative half of api.engine_pyzy. Detecting the condition at all takes
 * care: PyZy::InputContext::init() returns void, and beneath it
 * Database::init() constructs the singleton whether or not open() found
 * anything (Database.cc:202-208, 729-734), so a pyzy with no database answers
 * every question exactly as a working one does and then converts nothing. The
 * adapter closes that by testing for the file in front of init(); this is what
 * proves it.
 *
 * Two things are asserted, and the second matters as much as the first:
 *
 *   1. Pinyin and Bopomofo are absent, and absent *together* — they are one
 *      backend behind two ids, so they must rise and fall as a pair.
 *   2. pathime_init() still succeeds, and hangul is still there. A backend's
 *      global init failing is per-engine, not fatal, and this is the test that
 *      would catch a wiring in which one missing data file took the whole
 *      library down.
 *
 * The premise — that there is no database — is arranged through
 * pathime_init_params_t::resource_dir, pointed at a directory this build makes
 * and never puts anything in. That is the same field a client uses to say
 * where its data lives, so the test reaches its condition the way a client
 * would rather than by manipulating its surroundings.
 */

#include <string.h>

#include "api_test_util.h"

#if !PATHIME_WITH_PYZY

int main(void)
{
    return pt_skip("api.engine_pyzy_nodb", "this build does not contain pyzy");
}

#else

int main(void)
{
    pathime_init_params_t params;
    pathime_engine_t *engine = NULL;

    memset(&params, 0, sizeof params);
    params.struct_size = sizeof params;
    /*
     * The two directories do different jobs, and this test is where that shows.
     * data_dir is a writable scratch directory: pyzy's user cache and config
     * live there, and pointing it anywhere changes nothing about availability,
     * which is itself part of what is being asserted — a client cannot make a
     * missing database appear by choosing a different profile directory.
     * resource_dir is where the database would be, and it is empty.
     */
    params.data_dir = PATHIME_NODB_DATA_DIR;
    params.resource_dir = PATHIME_NODB_RESOURCE_DIR;

    PT_CHECK_STATUS(pathime_init(&params), PATHIME_OK);

    /* 1. Absent, and absent together. */
    PT_CHECK(!pathime_has_engine(PATHIME_ENGINE_PINYIN));
    PT_CHECK(!pathime_has_engine(PATHIME_ENGINE_BOPOMOFO));
    PT_CHECK(pathime_has_engine(PATHIME_ENGINE_PINYIN) ==
             pathime_has_engine(PATHIME_ENGINE_BOPOMOFO));

    /*
     * And the report is not merely advisory: the header says
     * pathime_engine_create() returns PATHIME_ERROR_UNKNOWN_ENGINE whenever
     * pathime_has_engine() is false, so the two must agree. This is also the
     * call that would crash if availability were being probed by attempting a
     * conversion instead of by testing for the file — with no database open
     * pyzy's m_db is NULL and the query path dereferences it.
     */
    PT_CHECK_STATUS(pathime_engine_create(PATHIME_ENGINE_PINYIN, &engine),
                    PATHIME_ERROR_UNKNOWN_ENGINE);
    PT_CHECK(engine == NULL);
    PT_CHECK_STATUS(pathime_engine_create(PATHIME_ENGINE_BOPOMOFO, &engine),
                    PATHIME_ERROR_UNKNOWN_ENGINE);
    PT_CHECK(engine == NULL);

    /*
     * 2. Per-engine, not fatal. hangul has no process-global setup at all in
     * this build (docs/design-history.md §2, Finding 3), so it is the one backend whose
     * availability cannot fail at runtime — which makes it the right witness
     * that pyzy's failure stayed pyzy's.
     */
#if PATHIME_WITH_HANGUL
    PT_CHECK(pathime_has_engine(PATHIME_ENGINE_HANGUL));
#endif

    pathime_shutdown();

    /*
     * Balanced shutdown after a refused init is the other half of the fix:
     * pyzy_global_init() returns before calling PyZy::InputContext::init(), so
     * pyzy_global_shutdown() must not call finalize(). If it did, this would
     * be an unbalanced Database::finalize() and the process would come apart
     * here rather than at the assertion above.
     */
    return pt_report("api.engine_pyzy_nodb");
}

#endif /* PATHIME_WITH_PYZY */
