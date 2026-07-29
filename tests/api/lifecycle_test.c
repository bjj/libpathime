/*
 * Library lifetime, seen from the client's side of the boundary: the
 * pathime_init() / pathime_shutdown() pairing, init-params validation, the two
 * initialization rejections, and every handle entry point a client can reach
 * without holding a handle.
 *
 * Which engines this build can actually supply is not something this file
 * decides. It asks pathime_has_engine() and branches, because that query is the
 * whole point of its own existence — a backend compiled in is not the same as
 * its dictionaries having opened — and a test that hardcoded the answer would
 * need editing for every build configuration and every installation it met. So
 * engine creation is covered either way: as a success and a round-trip where an
 * engine is available, as PATHIME_ERROR_UNKNOWN_ENGINE where it is not.
 *
 * What is absent, and absent rather than forgotten: everything downstream of an
 * input context — context creation and its _MISSING_CALLBACK rejection,
 * process_key, composition state surviving a
 * reset. Those say nothing meaningful without a live adapter behind them,
 * so they belong to the per-engine suites in tests/api/engine_*.c, and the
 * option machinery that hangs off a context belongs to
 * tests/core/options_test.cc, which builds engine and context handles directly
 * and drives the public entry points against them.
 *
 * Nothing here is a skip: every claim below is live and must hold.
 *
 * One entry point is deliberately never called with NULL. pathime_engine_id()
 * is documented in src/engine.cc as un-guarded on purpose — pathime_engine_id_t
 * has no "none" value, so any answer it could invent for a NULL handle would
 * name a real engine and mislead the caller. A valid handle is part of its
 * contract, so passing NULL is a client bug rather than a case to pin down.
 */

#if !defined(_WIN32)
/* mkdir()/rmdir() are POSIX, not ISO C, and this file is compiled as strict
 * C11 on some configurations. Requested before the first include, as glibc
 * requires. */
#  define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <stddef.h>
#include <string.h>

#if defined(_WIN32)
#  include <direct.h>
#  define pt_mkdir(path) _mkdir(path)
#  define pt_rmdir(path) _rmdir(path)
#else
#  include <sys/stat.h>
#  include <unistd.h>
#  define pt_mkdir(path) mkdir((path), 0700)
#  define pt_rmdir(path) rmdir(path)
#endif

#include "api_test_util.h"

/* ---------------------------------------------------------------------------
 * Shared data
 * ------------------------------------------------------------------------- */

#define PT_ENGINE_COUNT 5

static const pathime_engine_id_t kEngineIds[PT_ENGINE_COUNT] = {
    PATHIME_ENGINE_HANGUL,
    PATHIME_ENGINE_ANTHY,
    PATHIME_ENGINE_PINYIN,
    PATHIME_ENGINE_BOPOMOFO,
    PATHIME_ENGINE_TABLE
};

/*
 * Whether this build contains the *backend* behind each engine id, read from
 * the generated <pathime/config.h>. The mapping is not one-to-one and that is
 * the point of listing it: PATHIME_WITH_PYZY covers both PATHIME_ENGINE_PINYIN
 * and PATHIME_ENGINE_BOPOMOFO, since one backend supplies both.
 */
static const int kBackendCompiledIn[PT_ENGINE_COUNT] = {
    PATHIME_WITH_HANGUL,
    PATHIME_WITH_ANTHY,
    PATHIME_WITH_PYZY,
    PATHIME_WITH_PYZY,
    PATHIME_WITH_TABLE
};

/*
 * A pointer value no library call may ever write over an out-parameter it
 * rejected. It addresses real storage so that comparing it is defined, and it
 * is never dereferenced.
 */
static char pt_sentinel_storage;
#define PT_ENGINE_SENTINEL ((pathime_engine_t *)(void *)&pt_sentinel_storage)

/* A directory this test creates for itself, relative to the working directory
 * ctest runs it in. Deterministic, and outside no path the user cares about. */
static const char kDataDir[] = "pathime_lifecycle_test_data";

/* ---------------------------------------------------------------------------
 * pathime_init() / pathime_shutdown()
 * ------------------------------------------------------------------------- */

static void test_init_shutdown_pairing(void)
{
    int round;

    /*
     * The pairing is repeatable, not once-per-process. A client that tears the
     * library down and brings it back up — a plugin unloaded and reloaded, a
     * test harness between cases — must get a working library the second time,
     * which is only true if shutdown really releases the global flag rather
     * than merely marking it.
     */
    for (round = 0; round < 3; round++) {
        PT_CHECK_STATUS(pathime_init(NULL), PATHIME_OK);

        /* A second successful init without an intervening shutdown changes
         * nothing and says so. */
        PT_CHECK_STATUS(pathime_init(NULL), PATHIME_ERROR_ALREADY_INITIALIZED);
        PT_CHECK_STATUS(pathime_init(NULL), PATHIME_ERROR_ALREADY_INITIALIZED);

        pathime_shutdown();
    }

    /*
     * Arguments are validated before state, library-wide, and this is the one
     * place the two orders give visibly different answers: a second call
     * carrying a malformed struct_size reports the argument the caller can
     * actually fix rather than the state it is in.
     */
    PT_CHECK_STATUS(pathime_init(NULL), PATHIME_OK);
    {
        pathime_init_params_t bad;
        memset(&bad, 0, sizeof bad);
        bad.struct_size = 0;
        bad.data_dir = NULL;
        PT_CHECK_STATUS(pathime_init(&bad), PATHIME_ERROR_INVALID_ARGUMENT);
    }
    /* And that rejection left the library initialized, as every rejection
     * leaves everything exactly as it was. */
    PT_CHECK_STATUS(pathime_init(NULL), PATHIME_ERROR_ALREADY_INITIALIZED);
    pathime_shutdown();
}

static void test_shutdown_unpaired(void)
{
    /*
     * Documented as a no-op when the library is not initialized, so that a
     * caller which does not track whether pathime_init() succeeded can put it
     * on its failure path unconditionally. That is only useful if it also
     * survives being called twice, which is what an unwinding failure path
     * routinely does.
     */
    pathime_shutdown();
    pathime_shutdown();

    /* Having absorbed two unpaired shutdowns, the library still initializes. */
    PT_CHECK_STATUS(pathime_init(NULL), PATHIME_OK);
    pathime_shutdown();
    pathime_shutdown();
    PT_CHECK_STATUS(pathime_init(NULL), PATHIME_OK);
    pathime_shutdown();
}

static void test_init_params(void)
{
    pathime_init_params_t params;

    /*
     * Zeroed first, and that is not decoration. `struct_size == sizeof` is the
     * caller telling the library that every member of this layout is filled in,
     * so the library reads all of them — including resource_dir, which it
     * dereferences to reject an empty one (src/init.cc). Setting only the two
     * members a case is about would leave that one holding whatever was on the
     * stack: NULL on a lucky run, a wild pointer on an unlucky one. Every other
     * caller of pathime_init() in this tree zeroes the struct for the same
     * reason.
     */
    memset(&params, 0, sizeof params);

    /* struct_size set correctly with every other member at its default. NULL
     * data_dir is how "use the platform default" is spelled. */
    params.struct_size = sizeof params;
    params.data_dir = NULL;
    PT_CHECK_STATUS(pathime_init(&params), PATHIME_OK);
    pathime_shutdown();

    /*
     * A struct_size the library does not recognize is PATHIME_ERROR_INVALID_ARGUMENT.
     * The struct has exactly one layout, so exactly one value is recognized:
     * zero is the uninitialized struct a caller forgot to fill in, and
     * sizeof + 8 is a caller whose header is newer than this library, who has
     * set fields this library would silently ignore. Both are refused rather
     * than served on a guess.
     */
    params.struct_size = 0;
    params.data_dir = NULL;
    PT_CHECK_STATUS(pathime_init(&params), PATHIME_ERROR_INVALID_ARGUMENT);

    params.struct_size = sizeof params + 8;
    PT_CHECK_STATUS(pathime_init(&params), PATHIME_ERROR_INVALID_ARGUMENT);

    params.struct_size = sizeof params - 1;
    PT_CHECK_STATUS(pathime_init(&params), PATHIME_ERROR_INVALID_ARGUMENT);

    /*
     * A non-NULL but empty data_dir is refused. NULL already means "use the
     * default", so "" cannot also mean it without the two becoming
     * indistinguishable to a client that built the string itself and got an
     * empty one — which is precisely the case worth catching, since that client
     * would otherwise silently write its user's learned words somewhere it
     * never chose. The header does not spell this case out; src/init.cc decides
     * it, and this pins the decision down.
     */
    params.struct_size = sizeof params;
    params.data_dir = "";
    PT_CHECK_STATUS(pathime_init(&params), PATHIME_ERROR_INVALID_ARGUMENT);

    /* An explicit directory the client owns. Created here rather than assumed,
     * so the test depends on no path outside its own working directory. */
    {
        const int made = pt_mkdir(kDataDir);
        PT_CHECK(made == 0 || errno == EEXIST);
    }
    params.data_dir = kDataDir;
    PT_CHECK_STATUS(pathime_init(&params), PATHIME_OK);
    pathime_shutdown();
    pt_rmdir(kDataDir);
}

static void test_failed_init_is_retryable(void)
{
    pathime_init_params_t params;

    memset(&params, 0, sizeof params);

    /*
     * "A call that failed leaves the library uninitialized, so it may simply be
     * retried." A rejection that half-published the global state would make the
     * next call answer PATHIME_ERROR_ALREADY_INITIALIZED and strand the client
     * with a library it never successfully started.
     */
    params.struct_size = 12345;
    params.data_dir = NULL;
    PT_CHECK_STATUS(pathime_init(&params), PATHIME_ERROR_INVALID_ARGUMENT);

    /* Still uninitialized: pathime_has_engine() answers false for everything
     * before initialization, so it reads the flag the failed call must not have
     * set. */
    PT_CHECK(!pathime_has_engine(PATHIME_ENGINE_HANGUL));

    PT_CHECK_STATUS(pathime_init(NULL), PATHIME_OK);
    pathime_shutdown();

    /* The same with the other rejection reason, to show the retry property
     * belongs to the failure path and not to one particular check. */
    params.struct_size = sizeof params;
    params.data_dir = "";
    PT_CHECK_STATUS(pathime_init(&params), PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(pathime_init(NULL), PATHIME_OK);
    pathime_shutdown();
}

/* ---------------------------------------------------------------------------
 * pathime_has_engine()
 * ------------------------------------------------------------------------- */

static void test_has_engine(void)
{
    int i;

    /* Before init, false for everything — including for a value that is not an
     * engine id. abi_test.c makes the same claim on a library that has never
     * been initialized at all; this one makes it again after the init/shutdown
     * cycles above, where a leaked flag would show. */
    for (i = 0; i < PT_ENGINE_COUNT; i++) {
        PT_CHECK(!pathime_has_engine(kEngineIds[i]));
    }
    PT_CHECK(!pathime_has_engine((pathime_engine_id_t)9999));
    PT_CHECK(!pathime_has_engine((pathime_engine_id_t)-1));

    PT_CHECK_STATUS(pathime_init(NULL), PATHIME_OK);

    for (i = 0; i < PT_ENGINE_COUNT; i++) {
        /*
         * The durable claim, and the only one this test can make without
         * hardcoding which adapters a particular build contains: an engine can
         * only be available if the backend behind it was compiled in. Stated as
         * an implication against <pathime/config.h>, so a build configured
         * without pyzy must still answer false for Pinyin and Bopomofo, and so
         * the check holds for every build configuration rather than one.
         *
         * The converse is deliberately not asserted. A backend being compiled
         * in is not the same as its runtime prerequisites — anthy's
         * dictionaries, pyzy's database, the table engine's table directory —
         * having opened; pathime_has_engine() exists precisely so a client does
         * not have to guess at that difference, and a test that hardcoded the
         * answer would have to be edited for every installation it met.
         */
        PT_CHECK(!pathime_has_engine(kEngineIds[i]) || kBackendCompiledIn[i]);

        /* Idempotent: it reads a registry, and asking twice cannot change it. */
        PT_CHECK(pathime_has_engine(kEngineIds[i]) ==
                 pathime_has_engine(kEngineIds[i]));
    }

    /* PATHIME_ENGINE_TABLE gets no special case: it is a real engine, covered
     * by the loop above like every other id. Whether it is available depends on
     * the build and on whether its table directory resolved, which is exactly
     * what pathime_has_engine() is for. */

    /* Not an engine id at all, this time with the library initialized and real
     * state to consult. There is no error channel here, so the only correct
     * answer is false. */
    PT_CHECK(!pathime_has_engine((pathime_engine_id_t)9999));
    PT_CHECK(!pathime_has_engine((pathime_engine_id_t)-1));
    PT_CHECK(!pathime_has_engine((pathime_engine_id_t)(PATHIME_ENGINE_TABLE + 1)));

    pathime_shutdown();
}

/* ---------------------------------------------------------------------------
 * pathime_engine_create() / _destroy()
 * ------------------------------------------------------------------------- */

static void test_engine_create(void)
{
    pathime_engine_t *engine;
    int i;

    /*
     * Uninitialized. The out-parameter is checked before the initialization
     * state, so a NULL out-parameter reports the argument rather than the
     * state — the library-wide order, and the more useful of the two answers
     * because the caller can fix the argument without knowing anything about
     * the library's lifetime.
     */
    PT_CHECK_STATUS(pathime_engine_create(PATHIME_ENGINE_HANGUL, NULL),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    engine = PT_ENGINE_SENTINEL;
    PT_CHECK_STATUS(pathime_engine_create(PATHIME_ENGINE_HANGUL, &engine),
                    PATHIME_ERROR_NOT_INITIALIZED);
    /* "Receives the new engine on success, untouched on failure." A library
     * that wrote NULL here instead would look identical to one that wrote a
     * handle, to a caller checking the pointer rather than the status. */
    PT_CHECK(engine == PT_ENGINE_SENTINEL);

    /* An id that names no engine, before init, is still the initialization
     * rejection: state is consulted before the registry. */
    engine = PT_ENGINE_SENTINEL;
    PT_CHECK_STATUS(pathime_engine_create((pathime_engine_id_t)9999, &engine),
                    PATHIME_ERROR_NOT_INITIALIZED);
    PT_CHECK(engine == PT_ENGINE_SENTINEL);

    PT_CHECK_STATUS(pathime_init(NULL), PATHIME_OK);

    PT_CHECK_STATUS(pathime_engine_create(PATHIME_ENGINE_HANGUL, NULL),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    for (i = 0; i < PT_ENGINE_COUNT; i++) {
        /*
         * "Returns PATHIME_ERROR_UNKNOWN_ENGINE if pathime_has_engine() is
         * false for @a id." The header defines the error in terms of the query,
         * so the two are required to agree exactly — which makes this the
         * pairing check as much as it is a creation check, and is why the
         * branch is taken from pathime_has_engine() rather than from a list of
         * ids this test believes in.
         */
        engine = PT_ENGINE_SENTINEL;
        if (pathime_has_engine(kEngineIds[i])) {
            PT_CHECK_STATUS(pathime_engine_create(kEngineIds[i], &engine),
                            PATHIME_OK);
            PT_CHECK(engine != PT_ENGINE_SENTINEL);
            PT_CHECK(engine != NULL);

            if (engine != NULL && engine != PT_ENGINE_SENTINEL) {
                /* The id round-trips. This is the one call in the API whose
                 * whole purpose is to save a client juggling several engines
                 * from carrying the id alongside every handle, so it had better
                 * carry it. */
                PT_CHECK(pathime_engine_id(engine) == kEngineIds[i]);

                /*
                 * A freshly created engine is in its default configuration, and
                 * no default configuration of any engine asks anything of its
                 * client: only PATHIME_HANGUL_PREEDIT_NONE sets a
                 * PATHIME_REQUIRES_* bit, and it is not the default. The
                 * configured case belongs to core.options, which can set the
                 * option on a Hangul engine whether or not this build has one.
                 */
                PT_CHECK(pathime_engine_requirements(engine) == 0);

                pathime_engine_destroy(engine);
            }
        } else {
            PT_CHECK_STATUS(pathime_engine_create(kEngineIds[i], &engine),
                            PATHIME_ERROR_UNKNOWN_ENGINE);
            /* "Receives the new engine on success, untouched on failure." */
            PT_CHECK(engine == PT_ENGINE_SENTINEL);
        }
    }

    /* A value that is not an engine id is the same rejection, not a crash and
     * not a distinct code: to a caller both mean "do not try to create this". */
    engine = PT_ENGINE_SENTINEL;
    PT_CHECK_STATUS(pathime_engine_create((pathime_engine_id_t)9999, &engine),
                    PATHIME_ERROR_UNKNOWN_ENGINE);
    PT_CHECK(engine == PT_ENGINE_SENTINEL);

    engine = PT_ENGINE_SENTINEL;
    PT_CHECK_STATUS(pathime_engine_create((pathime_engine_id_t)-1, &engine),
                    PATHIME_ERROR_UNKNOWN_ENGINE);
    PT_CHECK(engine == PT_ENGINE_SENTINEL);

    pathime_shutdown();
}

static void test_destroy_null(void)
{
    /*
     * Both destroyers document NULL as a no-op, which is what lets a client
     * unwind a half-built set of handles without tracking how far it got. They
     * must hold whether or not the library is initialized, since the unwind
     * that needs them most is the one after pathime_init() itself failed.
     */
    pathime_engine_destroy(NULL);
    pathime_engine_destroy(NULL);
    pathime_context_destroy(NULL);
    pathime_context_destroy(NULL);

    PT_CHECK_STATUS(pathime_init(NULL), PATHIME_OK);
    pathime_engine_destroy(NULL);
    pathime_context_destroy(NULL);
    pathime_shutdown();

    /* Reaching here at all is the check: nothing above may crash. */
    PT_CHECK(1);
}

/* ---------------------------------------------------------------------------
 * The rejection gate on the context entry points
 * ------------------------------------------------------------------------- */

/**
 * Every context entry point with an error channel, called with a NULL handle.
 * This file never creates a context — that is the per-engine suites' ground —
 * so NULL is the only handle it has, but the point of the sweep is not the NULL:
 * it is that each of these has a determinate answer rather than a crash, and
 * that the answer is the same before and after pathime_init().
 *
 * The order the library documents is arguments first, then initialization, so
 * every one of these is PATHIME_ERROR_INVALID_ARGUMENT in both states. That the
 * uninitialized run does not instead report PATHIME_ERROR_NOT_INITIALIZED is
 * the substance of the check: a library that tested its global state first
 * would tell a caller to call pathime_init() when the actual bug is the NULL in
 * its hand.
 */
static void check_null_context_calls(void)
{
    pathime_str_t text;
    pathime_str_t out;
    pathime_key_event_t event;
    pathime_context_t *ctx;
    bool handled;

    text.bytes = "abc";
    text.len = 3;

    event.struct_size = sizeof event;
    event.keysym = 'a';
    event.layout_key = 'a';
    event.modifiers = 0;

    ctx = (pathime_context_t *)(void *)&pt_sentinel_storage;
    PT_CHECK_STATUS(pathime_context_create(NULL, NULL, NULL, &ctx),
                    PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK(ctx == (pathime_context_t *)(void *)&pt_sentinel_storage);

    /* out_handled is the one out-parameter the API documents as *always*
     * written, including on a rejection, so a client whose fallback path keys
     * off it alone stays correct. Seeded true so that "not written" and
     * "written false" are distinguishable. */
    handled = true;
    PT_CHECK_STATUS(pathime_context_process_key(NULL, &event, &handled),
                    PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK(!handled);

    handled = true;
    PT_CHECK_STATUS(pathime_context_process_key(NULL, NULL, &handled),
                    PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK(!handled);

    /* No out_handled at all is still a rejection and still not a crash. */
    PT_CHECK_STATUS(pathime_context_process_key(NULL, &event, NULL),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    out.bytes = NULL;
    out.len = 12345;
    PT_CHECK_STATUS(pathime_context_candidate(NULL, 0, &out),
                    PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK(out.len == 12345);
    PT_CHECK_STATUS(pathime_context_candidate(NULL, 0, NULL),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    PT_CHECK_STATUS(pathime_context_select_candidate(NULL, 0),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    PT_CHECK_STATUS(pathime_context_set_candidate_cursor(NULL, 0),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    PT_CHECK_STATUS(pathime_context_set_surrounding_text(NULL, text, 0),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    PT_CHECK_STATUS(pathime_context_commit(NULL), PATHIME_ERROR_INVALID_ARGUMENT);

    PT_CHECK_STATUS(pathime_context_reset(NULL), PATHIME_ERROR_INVALID_ARGUMENT);

    /*
     * The queries with no error channel answer for a NULL handle instead of
     * rejecting, and each answer is the one a caller can act on: no engine, no
     * user data, no composition, no callback obligation. pathime_engine_id() is
     * the deliberate exception and is not called here — see the file comment.
     */
    PT_CHECK(pathime_context_engine(NULL) == NULL);
    PT_CHECK(pathime_context_user_data(NULL) == NULL);
    PT_CHECK(pathime_context_composition(NULL) == NULL);
    PT_CHECK(pathime_engine_requirements(NULL) == 0);
    PT_CHECK(pathime_context_requirements(NULL) == 0);
}

static void test_null_handle_gate(void)
{
    check_null_context_calls();

    PT_CHECK_STATUS(pathime_init(NULL), PATHIME_OK);
    check_null_context_calls();
    pathime_shutdown();

    /* And once more after shutdown, because the gate is a property of the
     * entry points rather than of a particular moment in the library's life. */
    check_null_context_calls();
}

/* ---------------------------------------------------------------------------
 * Final state
 * ------------------------------------------------------------------------- */

static void test_leaves_library_down(void)
{
    /* Every function above shut the library down behind itself. If one did not,
     * this init reports PATHIME_ERROR_ALREADY_INITIALIZED and the test that
     * leaked is the one to look at. */
    PT_CHECK_STATUS(pathime_init(NULL), PATHIME_OK);
    pathime_shutdown();
}

int main(void)
{
    test_init_shutdown_pairing();
    test_shutdown_unpaired();
    test_init_params();
    test_failed_init_is_retryable();
    test_has_engine();
    test_engine_create();
    test_destroy_null();
    test_null_handle_gate();
    test_leaves_library_down();

    return pt_report("api.lifecycle");
}
