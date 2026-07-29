/*
 * The parts of the ABI that are promised to hold before pathime_init() is
 * ever called: version lockstep, status-string totality, and the compile-time
 * contracts a C client is allowed to lean on. Everything here is live — this
 * is the test that proves the library target links and exports at all.
 */

#include <string.h>

#include "api_test_util.h"

/* Promised by the header: PATHIME_OK is zero; every other value is an error. */
_Static_assert(PATHIME_OK == 0, "PATHIME_OK must be zero");

/* Status values are assigned explicitly and are part of the ABI. */
_Static_assert(PATHIME_ERROR_ALREADY_INITIALIZED == 6, "rejection block ends at 6");
_Static_assert(PATHIME_ERROR_BACKEND == 8, "failure block ends at 8");

int main(void)
{
    /* In-tree, header and library are built together, so the runtime version
     * must be in lockstep with the macros — a mismatch means the library
     * forgot to return the header's constants. */
    PT_CHECK(pathime_version() == PATHIME_VERSION);
    PT_CHECK(pathime_version_string() != NULL);
    PT_CHECK(strcmp(pathime_version_string(), PATHIME_VERSION_STRING) == 0);

    /* pathime_status_string(): never NULL, including for values this library
     * does not define, which describe themselves as unknown. */
    for (int code = PATHIME_OK; code <= PATHIME_ERROR_BACKEND; code++) {
        const char *s = pathime_status_string((pathime_status_t)code);
        PT_CHECK(s != NULL && s[0] != '\0');
    }
    {
        const char *unknown = pathime_status_string((pathime_status_t)9999);
        PT_CHECK(unknown != NULL && unknown[0] != '\0');
        PT_CHECK(strcmp(unknown, pathime_status_string(PATHIME_OK)) != 0);
    }

    /* pathime_has_engine() is pre-init-safe and documented false for every
     * engine before pathime_init() has succeeded, and false for a value that
     * is not an engine id at all. (Its post-init agreement with config.h
     * belongs to lifecycle_test, which is what calls pathime_init().) */
    PT_CHECK(!pathime_has_engine(PATHIME_ENGINE_HANGUL));
    PT_CHECK(!pathime_has_engine(PATHIME_ENGINE_ANTHY));
    PT_CHECK(!pathime_has_engine(PATHIME_ENGINE_PINYIN));
    PT_CHECK(!pathime_has_engine(PATHIME_ENGINE_BOPOMOFO));
    PT_CHECK(!pathime_has_engine(PATHIME_ENGINE_TABLE));
    PT_CHECK(!pathime_has_engine((pathime_engine_id_t)9999));

    return pt_report("api.abi");
}
