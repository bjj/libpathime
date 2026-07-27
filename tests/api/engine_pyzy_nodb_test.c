/*
 * api.engine_pyzy_nodb — pyzy reports itself unavailable when its database is
 * missing, and takes nothing else down with it.
 *
 * The negative half of api.engine_pyzy, and the regression test for the check
 * TODO.md §4a asked for. Until that check existed, a pyzy with no database was
 * indistinguishable from a working one: PyZy::InputContext::init() returns
 * void, and beneath it Database::init() constructs the singleton whether or
 * not open() found anything (Database.cc:202-208, 729-734), so
 * pathime_has_engine() said true and every conversion afterwards produced
 * nothing. src/engines/pyzy/pyzy_backend.cc's pyzy_database_present() closes
 * that by testing for the file in front of init(); this is what proves it.
 *
 * Two things are asserted, and the second matters as much as the first:
 *
 *   1. Pinyin and Bopomofo are absent, and absent *together* — they are one
 *      backend behind two ids, so they must rise and fall as a pair.
 *   2. pathime_init() still succeeds, and hangul is still there. A backend's
 *      global init failing is per-engine, not fatal (TODO.md §5); this test is
 *      what would catch a regression back to the original wiring, in which one
 *      missing data file took the whole library down.
 *
 * Registration is conditional, because the assertion is only true when there
 * genuinely is no database to find, and "no database" is a property of the
 * machine rather than of the build. tests/api/CMakeLists.txt probes pyzy's
 * compiled-in PKGDATADIR at configure time and registers this test only when
 * that probe comes up empty; the working directory it runs in is one this
 * build creates and never stages a main.db into. On a machine with pyzy
 * installed system-wide the test is not registered at all, which is honest —
 * there, the condition it describes does not hold.
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
     * A data_dir is required, and this one is deliberately not the database's
     * home: pyzy's user cache and config live under data_dir, but the *system*
     * database it needs to convert at all does not. Pointing data_dir at a
     * writable scratch directory therefore changes nothing about availability,
     * which is itself part of what is being asserted — a client cannot make a
     * missing database appear by choosing a different profile directory.
     */
    params.data_dir = PATHIME_NODB_DATA_DIR;

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
     * this build (TODO.md §2 Finding 3), so it is the one backend whose
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
