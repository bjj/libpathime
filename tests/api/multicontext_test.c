/*
 * Contexts of *different* engines alive at the same time, keyed alternately.
 *
 * Each engine test in this directory drives one backend, so none of them can
 * ask the question this one exists for: whether a client with a Korean field
 * and a Japanese field open at once — the ordinary case for a phone keyboard
 * or a browser — gets the same answer in each as it would if the other did not
 * exist. The model says it does. `docs/CONCEPTS.md` requires only that calls
 * be serialized, not that they concern one engine, and every backend here is
 * reached through libraries with process-global conversion state, so the claim
 * is about the adapters keeping that state out of the composition rather than
 * about the libraries being reentrant.
 *
 * The method is differential rather than expectation-based, which is what lets
 * one file cover every engine without repeating what the four engine tests
 * already pin. Each script is typed alone first and its result recorded; then
 * every context types its script again, one key at a time, round-robin across
 * all of them. Any difference is a leak, and this test does not need to know
 * what the right Korean or Chinese answer was in order to say so.
 *
 * The two contexts each engine gets type *different* scripts — one the whole
 * thing, one all but its last key. Identical scripts would make the two
 * indistinguishable, and a context served from its neighbour's composition
 * would pass every check here.
 *
 * Nothing commits. That is deliberate: three of the four backends learn on
 * commit, and a test whose two phases must agree exactly cannot afford to
 * change the answer between them.
 */

#include <string.h>

#include "api_test_util.h"

/* Two contexts per engine: the same-engine pair and the cross-engine pairs are
 * both wanted, and one round-robin can carry both. */
#define CONTEXTS_PER_ENGINE 2
#define MAX_ENGINES 5
#define MAX_CONTEXTS (MAX_ENGINES * CONTEXTS_PER_ENGINE)

typedef struct {
    char commits[256];
    int commit_count;
    int changed_count;
    /* The preedit as the *client* was handed it, which is a second route to
     * the same composition and so a cross-check on the first. */
    char last_preedit[256];
} client_log_t;

static void on_commit(void *user_data, pathime_str_t text)
{
    client_log_t *log = (client_log_t *)user_data;
    size_t used = strlen(log->commits);
    log->commit_count++;
    if (used + text.len + 1 < sizeof(log->commits)) {
        memcpy(log->commits + used, text.bytes, text.len);
        log->commits[used + text.len] = '\0';
    }
}

static void on_changed(void *user_data, const pathime_composition_t *composition)
{
    client_log_t *log = (client_log_t *)user_data;
    log->changed_count++;
    if (composition->preedit.len < sizeof(log->last_preedit)) {
        memcpy(log->last_preedit, composition->preedit.bytes, composition->preedit.len);
        log->last_preedit[composition->preedit.len] = '\0';
    }
}

/* One engine's share of the work: what it is, what to type into it, and what
 * typing that alone produced — once per script variant. */
typedef struct {
    pathime_engine_id_t id;
    const char *name;
    const char *keys;
    const char *table_file;   /* PATHIME_OPT_TABLE_FILE, or NULL */

    pathime_engine_t *engine;
    char solo_preedit[CONTEXTS_PER_ENGINE][256];
    size_t solo_candidates[CONTEXTS_PER_ENGINE];
} engine_slot_t;

/*
 * The scripts. Each is the shortest one from that engine's own test that
 * leaves a composition standing without committing anything: g-k-s is 한
 * mid-word, "nihon" is にほn with the n still pending, "nihao" and "su3cl3"
 * are the same Chinese reached two ways, and "ab" is 日月 in Cangjie. Each
 * must be at least CONTEXTS_PER_ENGINE keys long, since the variants are its
 * prefixes.
 */
static engine_slot_t g_slots[MAX_ENGINES + 1] = {
#if PATHIME_WITH_HANGUL
    { PATHIME_ENGINE_HANGUL, "hangul", "gks", NULL, NULL, { { 0 } }, { 0 } },
#endif
#if PATHIME_WITH_ANTHY
    { PATHIME_ENGINE_ANTHY, "anthy", "nihon", NULL, NULL, { { 0 } }, { 0 } },
#endif
#if PATHIME_WITH_PYZY
    { PATHIME_ENGINE_PINYIN, "pinyin", "nihao", NULL, NULL, { { 0 } }, { 0 } },
    { PATHIME_ENGINE_BOPOMOFO, "bopomofo", "su3cl3", NULL, NULL, { { 0 } }, { 0 } },
#endif
#if PATHIME_WITH_TABLE
    { PATHIME_ENGINE_TABLE, "table", "ab", "cangjie5", NULL, { { 0 } }, { 0 } },
#endif
    { PATHIME_ENGINE_HANGUL, NULL, NULL, NULL, NULL, { { 0 } }, { 0 } }
};

static size_t g_count;   /* slots that are actually usable in this build */

/* How many of @a slot's keys variant @a n types: the whole script for the
 * first context, one key less for each context after it. */
static size_t script_len(const engine_slot_t *slot, size_t n)
{
    return strlen(slot->keys) - n;
}

static bool press(pathime_context_t *ctx, char key)
{
    pathime_key_event_t event;
    bool handled = false;
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.keysym = (uint32_t)(unsigned char)key;
    event.layout_key = (uint32_t)(unsigned char)key;
    PT_CHECK_STATUS(pathime_context_process_key(ctx, &event, &handled), PATHIME_OK);
    return handled;
}

static const char *preedit_of(pathime_context_t *ctx)
{
    return pathime_context_composition(ctx)->preedit.bytes;
}

static size_t candidates_of(pathime_context_t *ctx)
{
    return pathime_context_composition(ctx)->candidate_count;
}

/*
 * A context on @a slot's engine, or NULL if the engine cannot serve
 * one. The table engine is the only case that can fail here, and only because
 * its table has to exist on disk; failing is how a build without cangjie5
 * drops that engine rather than failing every assertion below.
 */
static pathime_context_t *open_context(const engine_slot_t *slot,
                                       pathime_client_t *client,
                                       client_log_t *log)
{
    pathime_context_t *ctx = NULL;

    memset(log, 0, sizeof(*log));
    memset(client, 0, sizeof(*client));
    client->struct_size = sizeof(*client);
    client->commit_text = on_commit;
    client->composition_changed = on_changed;

    if (pathime_context_create(slot->engine, client, log, &ctx) != PATHIME_OK) {
        return NULL;
    }
    if (slot->table_file != NULL &&
        pathime_context_set_option_string(ctx, PATHIME_OPT_TABLE_FILE,
                                          slot->table_file) != PATHIME_OK) {
        pathime_context_destroy(ctx);
        return NULL;
    }
    return ctx;
}

/*
 * Phase 1. Every usable engine types each of its scripts into a context of its
 * own, with no other context in existence, and the results are what phase 2
 * has to reproduce. Slots whose engine is absent from this build, or whose
 * context cannot be opened, are dropped here — so what survives is exactly
 * what the rest of the test can speak about.
 */
static void collect_solo_results(void)
{
    size_t kept = 0;
    size_t i;

    for (i = 0; g_slots[i].name != NULL; i++) {
        engine_slot_t slot = g_slots[i];
        bool usable = true;
        size_t n;

        if (!pathime_has_engine(slot.id)) {
            continue;
        }
        if (pathime_engine_create(slot.id, &slot.engine) != PATHIME_OK) {
            continue;
        }

        for (n = 0; n < CONTEXTS_PER_ENGINE && usable; n++) {
            pathime_client_t client;
            client_log_t log;
            pathime_context_t *ctx = open_context(&slot, &client, &log);
            size_t k;

            if (ctx == NULL) {
                usable = false;
                break;
            }

            for (k = 0; k < script_len(&slot, n); k++) {
                PT_CHECK(press(ctx, slot.keys[k]));
            }

            PT_CHECK(strlen(preedit_of(ctx)) < sizeof(slot.solo_preedit[n]));
            strncpy(slot.solo_preedit[n], preedit_of(ctx),
                    sizeof(slot.solo_preedit[n]) - 1);
            slot.solo_preedit[n][sizeof(slot.solo_preedit[n]) - 1] = '\0';
            slot.solo_candidates[n] = candidates_of(ctx);

            /* A script that composed nothing would make every later comparison
             * vacuous — two empty preedits agree whatever leaked. */
            PT_CHECK(slot.solo_preedit[n][0] != '\0');
            /* And nothing committed, which is what keeps the two phases
             * comparable across the engines that learn. */
            PT_CHECK(log.commit_count == 0);

            pathime_context_destroy(ctx);
        }

        if (!usable) {
            pathime_engine_destroy(slot.engine);
            continue;
        }

        /*
         * The variants have to be distinguishable, or a context served from
         * its neighbour's composition would satisfy every check in phase 2.
         */
        PT_CHECK(strcmp(slot.solo_preedit[0], slot.solo_preedit[1]) != 0);

        g_slots[kept++] = slot;
    }

    g_count = kept;
    for (i = kept; i <= MAX_ENGINES; i++) {
        g_slots[i].name = NULL;
        g_slots[i].engine = NULL;
    }
}

int main(void)
{
    pathime_init_params_t params;
    pathime_context_t *ctx[MAX_CONTEXTS];
    client_log_t log[MAX_CONTEXTS];
    pathime_client_t client[MAX_CONTEXTS];
    size_t owner[MAX_CONTEXTS];     /* which slot each context belongs to */
    size_t variant[MAX_CONTEXTS];   /* which script it types */
    size_t live = 0;
    size_t longest = 0;
    size_t i;
    size_t step;
    size_t broadcast = (size_t)-1;

    memset(&params, 0, sizeof(params));
    params.struct_size = sizeof(params);
#ifdef MULTICONTEXT_TEST_DATA_DIR
    params.data_dir = MULTICONTEXT_TEST_DATA_DIR;
#endif
    PT_CHECK_STATUS(pathime_init(&params), PATHIME_OK);

    collect_solo_results();

    /*
     * One engine cannot be interleaved with anything. Reported as a skip
     * rather than a pass, because a green run here would otherwise mean
     * "nothing was checked" in exactly the configuration where that is easiest
     * to miss.
     */
    if (g_count < 2) {
        for (i = 0; i < g_count; i++) {
            pathime_engine_destroy(g_slots[i].engine);
        }
        pathime_shutdown();
        return pt_skip("api.multicontext",
                       "fewer than two engines are usable in this build");
    }

    /* --- phase 2: everything at once, one key at a time ------------------ */

    /*
     * Which engines this run interleaved. Printed because the answer depends
     * on the configuration and on data found at runtime, and a bare "passed"
     * would not distinguish two engines from five.
     */
    printf("api.multicontext: interleaving");
    for (i = 0; i < g_count; i++) {
        printf(" %s", g_slots[i].name);
    }
    printf(" (%d contexts)\n", (int)(g_count * CONTEXTS_PER_ENGINE));

    for (i = 0; i < g_count; i++) {
        size_t n;
        for (n = 0; n < CONTEXTS_PER_ENGINE; n++) {
            if (script_len(&g_slots[i], n) > longest) {
                longest = script_len(&g_slots[i], n);
            }
            ctx[live] = open_context(&g_slots[i], &client[live], &log[live]);
            PT_CHECK(ctx[live] != NULL);
            owner[live] = i;
            variant[live] = n;
            live++;
        }
    }

    /*
     * Round-robin. Every context is mid-composition while every other one is
     * being keyed, which is the state the whole test is about: a context that
     * had to be the only one composing would fail here and nowhere else.
     */
    for (step = 0; step < longest; step++) {
        for (i = 0; i < live; i++) {
            const engine_slot_t *slot = &g_slots[owner[i]];
            if (step < script_len(slot, variant[i])) {
                PT_CHECK(press(ctx[i], slot->keys[step]));
            }
        }
    }

    for (i = 0; i < live; i++) {
        const engine_slot_t *slot = &g_slots[owner[i]];
        const char *want = slot->solo_preedit[variant[i]];

        pt_checks++;
        if (strcmp(preedit_of(ctx[i]), want) != 0) {
            PT_FAILF("%s: interleaved preedit \"%s\", alone \"%s\"",
                     slot->name, preedit_of(ctx[i]), want);
        }
        PT_CHECK_SIZE(candidates_of(ctx[i]), slot->solo_candidates[variant[i]]);

        /* The client's own view agrees with the query. Two routes to one
         * composition, and a context reading its neighbour's would have to
         * corrupt both to keep them agreeing. */
        pt_checks++;
        if (strcmp(log[i].last_preedit, want) != 0) {
            PT_FAILF("%s: client was shown \"%s\", alone \"%s\"",
                     slot->name, log[i].last_preedit, want);
        }

        /* Every key produced one callback, to its own client and no other. */
        PT_CHECK(log[i].changed_count == (int)script_len(slot, variant[i]));
        PT_CHECK(log[i].commit_count == 0);
    }

    /* --- phase 3: an engine-level change, with everything mid-composition - */

    /*
     * Engine-level options are documented as reaching every context of *that*
     * engine that has not overridden them, immediately. With one engine per
     * client this is unobservable; here the other engines' contexts are the
     * control group.
     */
    for (i = 0; i < g_count && broadcast == (size_t)-1; i++) {
        if (g_slots[i].solo_candidates[0] > 1 && g_slots[i].solo_candidates[1] > 1) {
            broadcast = i;
        }
    }

    if (broadcast != (size_t)-1) {
        for (i = 0; i < live; i++) {
            log[i].changed_count = 0;
        }
        PT_CHECK_STATUS(pathime_engine_set_option_int(g_slots[broadcast].engine,
                                                      PATHIME_OPT_MAX_CANDIDATES, 1),
                        PATHIME_OK);

        for (i = 0; i < live; i++) {
            const engine_slot_t *slot = &g_slots[owner[i]];
            if (owner[i] == broadcast) {
                PT_CHECK_SIZE(candidates_of(ctx[i]), 1);
                PT_CHECK(log[i].changed_count == 1);
            } else {
                PT_CHECK_SIZE(candidates_of(ctx[i]), slot->solo_candidates[variant[i]]);
                PT_CHECK(log[i].changed_count == 0);
            }
            /* Nobody's composition was disturbed: capping a list is not a
             * reset, whichever engine the context belongs to. */
            pt_checks++;
            if (strcmp(preedit_of(ctx[i]), slot->solo_preedit[variant[i]]) != 0) {
                PT_FAILF("%s: preedit changed by an engine-level cap", slot->name);
            }
        }

        /* And lifting it restores what each context was showing. */
        PT_CHECK_STATUS(pathime_engine_reset_option(g_slots[broadcast].engine,
                                                    PATHIME_OPT_MAX_CANDIDATES),
                        PATHIME_OK);
        for (i = 0; i < live; i++) {
            PT_CHECK_SIZE(candidates_of(ctx[i]),
                          g_slots[owner[i]].solo_candidates[variant[i]]);
        }
    }

    /* --- teardown, in an order no context is prepared for ---------------- */

    /*
     * Destroyed oldest first, so every destruction happens with other contexts
     * of the same engine and of other engines still composing. Each survivor
     * is checked afterwards, which is the assertion: a context's lifetime is
     * its own.
     */
    for (i = 0; i < live; i++) {
        size_t j;
        pathime_context_destroy(ctx[i]);
        ctx[i] = NULL;
        for (j = i + 1; j < live; j++) {
            const engine_slot_t *slot = &g_slots[owner[j]];
            pt_checks++;
            if (strcmp(preedit_of(ctx[j]), slot->solo_preedit[variant[j]]) != 0) {
                PT_FAILF("%s: preedit changed by another context's destruction",
                         slot->name);
            }
        }
    }

    for (i = 0; i < g_count; i++) {
        pathime_engine_destroy(g_slots[i].engine);
    }
    pathime_shutdown();
    return pt_report("api.multicontext");
}
