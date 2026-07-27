/*
 * The options surface a client can reach without an engine handle: the
 * introspection walk the header's own example performs, the option-name
 * contract, and the entry points documented to answer for a NULL handle rather
 * than to reject.
 *
 * That is a smaller set than it sounds, and the reason is worth stating. No
 * engine adapter exists yet, so pathime_engine_create() cannot succeed and a C
 * client has no way to obtain the handle every remaining option entry point
 * takes. The descriptor content itself — per-engine `supported`, the value
 * kinds, defaults, bounds and valid_values, the two-level resolution, the
 * kind-typed setters and their rejections — is therefore covered by
 * core.options, which compiles the internal sources directly and builds the
 * handles by hand. Nothing about the options machinery is going untested; it is
 * simply tested from the side of the boundary that can currently reach it.
 *
 * What is here is genuinely client-facing, and two claims in it are load
 * bearing beyond this file: pathime_option_count() and pathime_option_name()
 * are documented as static table lookups usable *before* pathime_init(), which
 * is what lets a client build its settings interface before deciding to start
 * the library, and option names are ABI — a client stores them as its own
 * configuration keys.
 */

#include <string.h>

#include "api_test_util.h"

/* The header's density promise: option ids are dense and append-only, so every
 * option is a value in [0, pathime_option_count()). */
#define PT_OPTION_COUNT ((size_t)PATHIME_OPT_TABLE_PINYIN_FALLBACK + 1)

/* ---------------------------------------------------------------------------
 * Introspection
 * ------------------------------------------------------------------------- */

static void check_option_count(void)
{
    /*
     * The count is what makes the walk below reach options a client's own
     * header never named — the whole mechanism by which a settings interface
     * stays useful against a library newer than the client. Tying it to the
     * last enumerator rather than to a literal is what keeps this check honest
     * when an option is appended.
     */
    PT_CHECK(pathime_option_count() == PT_OPTION_COUNT);
}

/**
 * Whether @a name obeys the documented shape of an option name: lowercase
 * letters, digits and hyphens only, with no leading or trailing hyphen.
 *
 * The rule matters because these strings are storage keys a client writes into
 * its own configuration. A name that acquired an uppercase letter or a space
 * would still work in this process and break somewhere else — in a case-folding
 * registry, an INI section header, a shell-quoted argument — long after the
 * change that caused it.
 */
static int name_is_well_formed(const char *name)
{
    size_t i;
    size_t len;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    len = strlen(name);
    if (name[0] == '-' || name[len - 1] == '-') {
        return 0;
    }
    for (i = 0; i < len; i++) {
        const char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return 0;
        }
    }
    return 1;
}

static void check_option_names(void)
{
    const char *names[PT_OPTION_COUNT];
    size_t i;
    size_t j;

    for (i = 0; i < PT_OPTION_COUNT; i++) {
        const pathime_option_t option = (pathime_option_t)i;
        names[i] = pathime_option_name(option);

        /* Never NULL and never empty: "" is reserved as the answer for a value
         * that is not an option id, so a real option must never produce it or
         * the two become indistinguishable. */
        PT_CHECK(names[i] != NULL);
        PT_CHECK(names[i] != NULL && names[i][0] != '\0');
        PT_CHECK(name_is_well_formed(names[i]));
    }

    /*
     * All distinct. This is the check with teeth: the names are ABI storage
     * keys, so a collision would silently merge two clients' settings — one
     * option's value written and the other's read back, with no error anywhere
     * to notice it by. Exhaustive and exact, over 32 entries; one check per
     * option, so a failure names the colliding pair rather than an index into
     * a quadratic sweep.
     */
    for (i = 0; i < PT_OPTION_COUNT; i++) {
        pt_checks++;
        for (j = i + 1; j < PT_OPTION_COUNT; j++) {
            if (strcmp(names[i], names[j]) == 0) {
                PT_FAILF("option %u and %u share the name \"%s\"",
                         (unsigned)i, (unsigned)j, names[i]);
            }
        }
    }

    /* The one name the header states outright, in its description of what
     * pathime_option_name() returns. */
    PT_CHECK(strcmp(pathime_option_name(PATHIME_OPT_CHINESE_VARIANT),
                    "chinese-variant") == 0);
}

static void check_option_name_rejects(void)
{
    /*
     * "A value that is not an option id yields "", which is never a valid
     * option name." Returning a name for one of these would be worse than
     * useless to the walk above: a client iterating past the end it was told
     * about would store settings under a key naming nothing.
     */
    PT_CHECK(strcmp(pathime_option_name((pathime_option_t)PT_OPTION_COUNT), "") == 0);
    PT_CHECK(strcmp(pathime_option_name((pathime_option_t)(PT_OPTION_COUNT + 1)), "") == 0);
    PT_CHECK(strcmp(pathime_option_name((pathime_option_t)9999), "") == 0);

    /* Negative values too. The enum's underlying type is signed here, and a
     * bounds test written as a single unsigned comparison is the only kind that
     * catches this without a second branch to forget. */
    PT_CHECK(strcmp(pathime_option_name((pathime_option_t)-1), "") == 0);
    PT_CHECK(strcmp(pathime_option_name((pathime_option_t)-9999), "") == 0);
}

/**
 * The header's own example loop, run verbatim. It is quoted in the
 * documentation of pathime_option_count() as the way a client walks the whole
 * inventory, so it is worth executing rather than paraphrasing.
 */
static void check_header_example_walk(void)
{
    size_t i;
    size_t seen = 0;

    for (i = 0; i < pathime_option_count(); i++) {
        const pathime_option_t opt = (pathime_option_t)i;
        const char *name = pathime_option_name(opt);
        PT_CHECK(name != NULL && name[0] != '\0');
        seen++;
    }
    PT_CHECK(seen == PT_OPTION_COUNT);
}

/* ---------------------------------------------------------------------------
 * is_set with no handle
 * ------------------------------------------------------------------------- */

static void check_is_set_without_handle(void)
{
    /*
     * "False for everything the question cannot be asked of: a NULL handle, a
     * value that is not an option id, an option this engine does not implement,
     * and any call made before pathime_init() has succeeded. There is no error
     * channel because the useful reading of all of those is the same — no value
     * has been set here."
     *
     * The absence of an error channel is exactly why this needs testing: a
     * crash or an uninitialized read here has nowhere to surface, and a client
     * building a settings interface calls these for every option it displays,
     * including ones it holds no handle for yet.
     */
    PT_CHECK(!pathime_engine_option_is_set(NULL, PATHIME_OPT_LEARNING));
    PT_CHECK(!pathime_engine_option_is_set(NULL, PATHIME_OPT_MAX_CANDIDATES));
    PT_CHECK(!pathime_engine_option_is_set(NULL, PATHIME_OPT_TABLE_FILE));
    PT_CHECK(!pathime_engine_option_is_set(NULL, (pathime_option_t)PT_OPTION_COUNT));
    PT_CHECK(!pathime_engine_option_is_set(NULL, (pathime_option_t)9999));
    PT_CHECK(!pathime_engine_option_is_set(NULL, (pathime_option_t)-1));

    PT_CHECK(!pathime_context_option_is_set(NULL, PATHIME_OPT_LEARNING));
    PT_CHECK(!pathime_context_option_is_set(NULL, PATHIME_OPT_MAX_CANDIDATES));
    PT_CHECK(!pathime_context_option_is_set(NULL, PATHIME_OPT_TABLE_FILE));
    PT_CHECK(!pathime_context_option_is_set(NULL, (pathime_option_t)PT_OPTION_COUNT));
    PT_CHECK(!pathime_context_option_is_set(NULL, (pathime_option_t)9999));
    PT_CHECK(!pathime_context_option_is_set(NULL, (pathime_option_t)-1));
}

/* ---------------------------------------------------------------------------
 * The descriptor query with no handle
 * ------------------------------------------------------------------------- */

static void check_option_info_rejects(void)
{
    pathime_option_info_t info;

    /*
     * Unlike the two above, this one has an error channel and uses it. A NULL
     * engine is PATHIME_ERROR_INVALID_ARGUMENT rather than
     * PATHIME_ERROR_NOT_INITIALIZED even before init, which is the library-wide
     * order — arguments first, then state — and the useful half of it: the
     * caller can fix the argument without knowing what the library's lifetime
     * is doing.
     */
    info.struct_size = sizeof info;
    PT_CHECK_STATUS(pathime_engine_option_info(NULL, PATHIME_OPT_LEARNING, &info),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    /* The out-parameter is checked with the same weight as the handle: a
     * descriptor query with nowhere to put its answer is not a query. */
    PT_CHECK_STATUS(pathime_engine_option_info(NULL, PATHIME_OPT_LEARNING, NULL),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    /* Nothing was written into the struct — struct_size is the caller's own
     * value, untouched. */
    PT_CHECK(info.struct_size == sizeof info);

    /* A bad struct_size alongside a NULL handle is still the handle's
     * rejection; both are PATHIME_ERROR_INVALID_ARGUMENT, so what this pins
     * down is that neither ordering crashes. */
    info.struct_size = 0;
    PT_CHECK_STATUS(pathime_engine_option_info(NULL, PATHIME_OPT_LEARNING, &info),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    /*
     * The rest of this entry point — the struct_size in-and-out protocol, and
     * every field it reports for each of the 32 options against each of the 5
     * engine ids — needs an engine handle, which the public API cannot yet
     * produce. core.options covers it against hand-built handles.
     */
}

/* ---------------------------------------------------------------------------
 * The two states
 * ------------------------------------------------------------------------- */

/** Everything that must answer identically before and after pathime_init(). */
static void run_pre_init_safe_checks(void)
{
    check_option_count();
    check_option_names();
    check_option_name_rejects();
    check_header_example_walk();
    check_is_set_without_handle();
    check_option_info_rejects();
}

int main(void)
{
    const char *before[PT_OPTION_COUNT];
    size_t i;

    /*
     * Before pathime_init(). The count and the names are documented as static
     * table lookups usable here, and the two is_set functions are documented to
     * answer false rather than to fail. Running the whole set in this state
     * first is the point: a lookup that quietly depended on initialized global
     * state would pass everywhere else and fail only in the one place a client
     * is promised it works.
     */
    run_pre_init_safe_checks();

    for (i = 0; i < PT_OPTION_COUNT; i++) {
        before[i] = pathime_option_name((pathime_option_t)i);
    }

    PT_CHECK_STATUS(pathime_init(NULL), PATHIME_OK);

    /* After. Same answers, every one of them. */
    run_pre_init_safe_checks();

    /*
     * And the same *strings*, not merely equal ones. The header promises a name
     * never changes once an option ships; a name that were rebuilt per call
     * would also break the documented static lifetime a client relies on when
     * it holds the pointer.
     */
    for (i = 0; i < PT_OPTION_COUNT; i++) {
        const char *after = pathime_option_name((pathime_option_t)i);
        PT_CHECK(after == before[i]);
        PT_CHECK(strcmp(after, before[i]) == 0);
    }

    pathime_shutdown();

    /* After shutdown too — these read no global state at all, so a shutdown
     * must not take them with it. */
    run_pre_init_safe_checks();

    return pt_report("api.options");
}
