/*
 * The Korean engine, driven the way a client drives it: through
 * <pathime/pathime.h> and nothing else.
 *
 * This is the first vertical slice — key event in, composition and commit out —
 * and the first test that exercises every layer at once: pathime_init()'s
 * global hooks, the engine registry, the key layer, the libhangul adapter, the
 * structured composition model, the projection, and the callback dispatch
 * order. Everything the unit suites check in isolation has to still be true
 * when they are stacked.
 *
 * Hangul is the right engine to prove the stack on. It produces no candidates
 * at all — hanja was cut in the API review round — so the composition it
 * exercises is exactly the settled/active spine, without the candidate
 * machinery on top. It is also the only engine whose composition is defined by
 * key *position* rather than by character, which is what layout_key exists for.
 *
 * Expected values here were taken from libhangul's own behaviour and are real
 * Korean: typing g-k-s on the two-set layout composes 한, one jamo at a time.
 */

#include <string.h>

#include "api_test_util.h"

#if !PATHIME_WITH_HANGUL

int main(void)
{
    return pt_skip("api.engine_hangul", "this build does not contain libhangul");
}

#else

/* 한 U+D55C, 글 U+AE00 — the syllables this test composes. */
#define HAN  "\xED\x95\x9C"
#define GEUL "\xEA\xB8\x80"

/* The intermediate forms of 한: ㅎ U+314E, 하 U+D558. */
#define HIEUH "\xE3\x85\x8E"
#define HA    "\xED\x95\x98"

/*
 * What the client saw. Callbacks append to this in the order they arrive, so
 * the dispatch order the header fixes — every deletion before any commit,
 * composition_changed always last — is checkable rather than assumed.
 */
typedef struct {
    char commits[512];     /* every commit_text payload, concatenated */
    int commit_count;
    int changed_count;
    char order[64];        /* one char per callback: 'd', 'c', 'x' */
    int order_len;

    /* What composition_changed last reported, copied out during the call. */
    char last_preedit[256];
    size_t last_settled;
    size_t last_candidates;
} client_log_t;

static void log_order(client_log_t *log, char c)
{
    if (log->order_len < (int)sizeof(log->order) - 1) {
        log->order[log->order_len++] = c;
        log->order[log->order_len] = '\0';
    }
}

static void on_commit(void *user_data, pathime_str_t text)
{
    client_log_t *log = (client_log_t *)user_data;
    log_order(log, 'c');
    log->commit_count++;
    if (strlen(log->commits) + text.len < sizeof(log->commits)) {
        memcpy(log->commits + strlen(log->commits), text.bytes, text.len);
    }
    /* The library promises everything it produces is NUL-terminated even
     * though len is authoritative. */
    PT_CHECK(text.bytes[text.len] == '\0');
}

static void on_delete(void *user_data, ptrdiff_t offset, size_t count)
{
    client_log_t *log = (client_log_t *)user_data;
    log_order(log, 'd');
    (void)offset;
    (void)count;
}

static void on_changed(void *user_data, const pathime_composition_t *composition)
{
    client_log_t *log = (client_log_t *)user_data;
    log_order(log, 'x');
    log->changed_count++;
    log->last_settled = composition->preedit_settled;
    log->last_candidates = composition->candidate_count;
    if (composition->preedit.len < sizeof(log->last_preedit)) {
        memcpy(log->last_preedit, composition->preedit.bytes, composition->preedit.len);
        log->last_preedit[composition->preedit.len] = '\0';
    }
}

static void log_reset(client_log_t *log)
{
    memset(log, 0, sizeof(*log));
}

/* Press one printable US-QWERTY key: keysym and layout_key are the same for
 * an unshifted ASCII key, which is the ordinary case for a US keyboard. */
static bool press(pathime_context_t *ctx, uint32_t keysym)
{
    pathime_key_event_t event;
    bool handled = false;
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.keysym = keysym;
    event.layout_key = keysym;
    PT_CHECK_STATUS(pathime_context_process_key(ctx, &event, &handled), PATHIME_OK);
    return handled;
}

/* The current preedit as a NUL-terminated C string, for comparison. */
static const char *preedit_of(pathime_context_t *ctx)
{
    const pathime_composition_t *c = pathime_context_composition(ctx);
    return c->preedit.bytes;
}

static void check_str(const char *what, const char *got, const char *want)
{
    pt_checks++;
    if (strcmp(got, want) != 0) {
        PT_FAILF("%s: got \"%s\", expected \"%s\"", what, got, want);
    }
}

/* ---------------------------------------------------------------------- */

/*
 * Syllable mode, the default: each syllable is committed as it finishes, so
 * the preedit only ever holds the syllable in progress.
 */
static void test_syllable_composition(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = NULL;

    log_reset(&log);
    memset(&client, 0, sizeof(client));
    client.struct_size = sizeof(client);
    client.commit_text = on_commit;
    client.delete_surrounding_text = on_delete;
    client.composition_changed = on_changed;

    PT_CHECK_STATUS(pathime_context_create(engine, &client, &log, &ctx), PATHIME_OK);
    PT_CHECK(ctx != NULL);

    /* A new context starts unfocused, and focus gates input and only input. */
    {
        pathime_key_event_t event;
        bool handled = true;
        memset(&event, 0, sizeof(event));
        event.struct_size = sizeof(event);
        event.keysym = 'g';
        PT_CHECK_STATUS(pathime_context_process_key(ctx, &event, &handled),
                        PATHIME_ERROR_NOT_FOCUSED);
        /* out_handled is documented as always written, including on error, so
         * a client's fallback path is correct even if it ignores the status. */
        PT_CHECK(handled == false);
    }
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, true), PATHIME_OK);

    /* g-k-s composes 한 one jamo at a time: ㅎ, then 하, then 한. */
    PT_CHECK(press(ctx, 'g'));
    check_str("preedit after g", preedit_of(ctx), HIEUH);
    PT_CHECK(press(ctx, 'k'));
    check_str("preedit after k", preedit_of(ctx), HA);
    PT_CHECK(press(ctx, 's'));
    check_str("preedit after s", preedit_of(ctx), HAN);

    /* Nothing has been committed yet, and the client was told three times. */
    PT_CHECK_SIZE(strlen(log.commits), 0);
    PT_CHECK(log.changed_count == 3);
    check_str("callback order so far", log.order, "xxx");

    /*
     * Hangul has no candidates at all, so the list is empty at every point and
     * both candidate entry points say so rather than pretending otherwise.
     */
    {
        pathime_str_t cand;
        const pathime_composition_t *c = pathime_context_composition(ctx);
        PT_CHECK_SIZE(c->candidate_count, 0);
        PT_CHECK_STATUS(pathime_context_candidate(ctx, 0, &cand),
                        PATHIME_ERROR_INVALID_ARGUMENT);
        PT_CHECK_STATUS(pathime_context_select_candidate(ctx, 0),
                        PATHIME_ERROR_INVALID_ARGUMENT);
    }

    /*
     * Space is not a jamo key, so libhangul declines it and the held syllable
     * flushes. The key comes back unhandled — the client inserts its own space
     * — while the composition it closed is committed first. That is the
     * header's point that "handled" describes the incoming event only and is
     * independent of the output produced.
     */
    log_reset(&log);
    PT_CHECK(!press(ctx, ' '));
    check_str("commit on space", log.commits, HAN);
    check_str("preedit after space", preedit_of(ctx), "");

    /*
     * The dispatch order, which is the whole reason refresh_composition is one
     * function: commit_text before composition_changed, always.
     */
    check_str("callback order across a commit", log.order, "cx");

    pathime_context_destroy(ctx);
}

/*
 * Word mode: finished syllables are held in the preedit instead of being
 * committed, with preedit_settled marking how many are done. This is the mode
 * that exercises the settled/active split — the part of the composition model
 * libhangul itself has no representation for, since it only ever exposes the
 * trailing mutable syllable.
 */
static void test_word_mode_settled_boundary(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = NULL;
    const pathime_composition_t *c = NULL;

    log_reset(&log);
    memset(&client, 0, sizeof(client));
    client.struct_size = sizeof(client);
    client.commit_text = on_commit;
    client.composition_changed = on_changed;

    PT_CHECK_STATUS(pathime_context_create(engine, &client, &log, &ctx), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_HANGUL_PREEDIT,
                                                   PATHIME_HANGUL_PREEDIT_WORD),
                    PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, true), PATHIME_OK);

    /* 한 completes and is held, not committed; 글 begins. */
    press(ctx, 'g');
    press(ctx, 'k');
    press(ctx, 's');
    press(ctx, 'r');

    c = pathime_context_composition(ctx);
    check_str("word-mode preedit", c->preedit.bytes, HAN "\xE3\x84\xB1");
    PT_CHECK_SIZE(strlen(log.commits), 0);

    /*
     * The assertion this test exists for. One syllable is settled. Its UTF-8
     * is three bytes, and preedit_settled must be 1 — the scalar count — not 3.
     * Every position in this API is in Unicode scalar values, and this is the
     * first place in the stack where a byte count would look plausible.
     */
    PT_CHECK_SIZE(c->preedit_settled, 1);
    PT_CHECK(c->preedit.len == 6);

    press(ctx, 'm');
    press(ctx, 'f');
    c = pathime_context_composition(ctx);
    check_str("word-mode preedit, two syllables", c->preedit.bytes, HAN GEUL);
    PT_CHECK_SIZE(c->preedit_settled, 1);
    PT_CHECK(c->preedit.len == 6);

    /* Space closes the word: the whole of it commits in one callback. */
    log_reset(&log);
    PT_CHECK(!press(ctx, ' '));
    PT_CHECK(log.commit_count == 1);
    check_str("word-mode commit", log.commits, HAN GEUL);
    check_str("preedit after word commit", preedit_of(ctx), "");
    PT_CHECK_SIZE(pathime_context_composition(ctx)->preedit_settled, 0);

    pathime_context_destroy(ctx);
}

/*
 * Backspace walks back through the composition rather than deleting a
 * character: it is a separate libhangul entry point, not an ascii value, and
 * in word mode it has to cross from the active syllable into the settled
 * prefix that only this library knows about.
 */
static void test_backspace(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = NULL;

    log_reset(&log);
    memset(&client, 0, sizeof(client));
    client.struct_size = sizeof(client);
    client.commit_text = on_commit;
    client.composition_changed = on_changed;

    PT_CHECK_STATUS(pathime_context_create(engine, &client, &log, &ctx), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_HANGUL_PREEDIT,
                                                   PATHIME_HANGUL_PREEDIT_WORD),
                    PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, true), PATHIME_OK);

    press(ctx, 'g');
    press(ctx, 'k');
    press(ctx, 's');   /* 한 */
    press(ctx, 'r');   /* 한ㄱ */

    /* Inside the active syllable, backspace removes one jamo — libhangul's
     * own granularity, since the syllable is still in its buffer. */
    PT_CHECK(press(ctx, PATHIME_KEY_BACKSPACE));
    check_str("after one backspace", preedit_of(ctx), HAN);

    /*
     * Crossing into the settled prefix changes the granularity, and that is
     * the intended behaviour rather than an accident: libhangul has already
     * let go of 한, so all this library holds is its text, and the whole
     * syllable goes at once. The header anticipates exactly this — it says the
     * visible difference between the preedit modes is "the granularity of
     * backspace and undo".
     */
    PT_CHECK(press(ctx, PATHIME_KEY_BACKSPACE));
    check_str("after backspace into settled", preedit_of(ctx), "");

    /* An empty composition has nothing left to consume, so the key is
     * declined and the client handles it as an ordinary backspace. */
    PT_CHECK(!press(ctx, PATHIME_KEY_BACKSPACE));
    PT_CHECK_SIZE(strlen(log.commits), 0);

    pathime_context_destroy(ctx);
}

/*
 * Composition is defined by key position, not by character — the one engine
 * for which layout_key is load-bearing. Shift reaches the doubled jamo.
 */
static void test_key_position_and_shift(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = NULL;
    pathime_key_event_t event;
    bool handled = false;

    log_reset(&log);
    memset(&client, 0, sizeof(client));
    client.struct_size = sizeof(client);
    client.commit_text = on_commit;
    client.composition_changed = on_changed;

    PT_CHECK_STATUS(pathime_context_create(engine, &client, &log, &ctx), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, true), PATHIME_OK);

    /*
     * Shift+Q on a US keyboard: keysym 'Q', layout_key 'q', PATHIME_MOD_SHIFT.
     * The engine recombines the two itself and reaches ㅃ U+3143, the doubled
     * bieup. Note the modifier is reported alongside the keysym and never
     * folded into it — 'Q' does not imply Shift and Shift does not upper-case.
     */
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.keysym = 'Q';
    event.layout_key = 'q';
    event.modifiers = PATHIME_MOD_SHIFT;
    PT_CHECK_STATUS(pathime_context_process_key(ctx, &event, &handled), PATHIME_OK);
    PT_CHECK(handled);
    check_str("Shift+Q reaches the doubled jamo", preedit_of(ctx), "\xE3\x85\x83");

    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    /*
     * A chorded key is a client shortcut and must be declined rather than
     * absorbed — the main reason modifiers reach the engine at all.
     */
    press(ctx, 'g');
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.keysym = 'c';
    event.layout_key = 'c';
    event.modifiers = PATHIME_MOD_CONTROL;
    handled = true;
    PT_CHECK_STATUS(pathime_context_process_key(ctx, &event, &handled), PATHIME_OK);
    PT_CHECK(handled == false);

    pathime_context_destroy(ctx);
}

/*
 * Reset discards without committing, and focus loss preserves state exactly —
 * the model case for the project's rule of preferring a determinate behaviour
 * to a deferral.
 */
static void test_reset_and_focus(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = NULL;

    log_reset(&log);
    memset(&client, 0, sizeof(client));
    client.struct_size = sizeof(client);
    client.commit_text = on_commit;
    client.composition_changed = on_changed;

    PT_CHECK_STATUS(pathime_context_create(engine, &client, &log, &ctx), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, true), PATHIME_OK);

    press(ctx, 'g');
    press(ctx, 'k');
    check_str("preedit before reset", preedit_of(ctx), HA);

    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);
    check_str("preedit after reset", preedit_of(ctx), "");
    /* Reset does not commit — the text is discarded, not preserved. */
    PT_CHECK_SIZE(strlen(log.commits), 0);
    /* It was non-empty, so the client is told. */
    PT_CHECK(log.changed_count == 1);

    /* Resetting an already-empty composition dispatches nothing. */
    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);
    PT_CHECK(log.changed_count == 0);

    /*
     * Focus loss neither commits nor discards. Composition state is preserved
     * exactly, so refocusing resumes where the user left off, and no callback
     * is dispatched.
     */
    press(ctx, 'g');
    press(ctx, 'k');
    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, false), PATHIME_OK);
    PT_CHECK(log.changed_count == 0);
    PT_CHECK_SIZE(strlen(log.commits), 0);
    check_str("preedit survives focus loss", preedit_of(ctx), HA);

    /* Redundant transitions are no-ops. */
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, false), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, true), PATHIME_OK);
    PT_CHECK(log.changed_count == 0);
    check_str("preedit survives refocus", preedit_of(ctx), HA);

    /* And composition continues from where it was. */
    press(ctx, 's');
    check_str("composition resumes", preedit_of(ctx), HAN);

    pathime_context_destroy(ctx);
}

/*
 * The layout option, and the reason it must be validated before use:
 * libhangul does not report an unknown keyboard id — it stores NULL and
 * crashes on the next key — so the adapter validates every id it maps.
 */
static void test_layout_option(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = NULL;
    int64_t layout = -1;

    log_reset(&log);
    memset(&client, 0, sizeof(client));
    client.struct_size = sizeof(client);
    client.commit_text = on_commit;
    client.composition_changed = on_changed;

    PT_CHECK_STATUS(pathime_context_create(engine, &client, &log, &ctx), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, true), PATHIME_OK);

    PT_CHECK_STATUS(pathime_context_get_option_int(ctx, PATHIME_OPT_HANGUL_LAYOUT, &layout),
                    PATHIME_OK);
    PT_CHECK(layout == PATHIME_HANGUL_LAYOUT_2SET);

    /* Every one of the nine built-in layouts must be selectable and must
     * compose without crashing — the id strings are not guessable from the
     * enum names, so this is the check that they were looked up rather than
     * invented. */
    for (int64_t i = PATHIME_HANGUL_LAYOUT_2SET; i <= PATHIME_HANGUL_LAYOUT_AHNMATAE; i++) {
        PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_HANGUL_LAYOUT, i),
                        PATHIME_OK);
        PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);
        press(ctx, 'g');
        press(ctx, 'k');
        press(ctx, 's');
        PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);
    }

    /* A value outside the enum is rejected and changes nothing. */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_HANGUL_LAYOUT, 99),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    /* Hangul produces no candidates, so the cap reports itself unsupported
     * rather than accepting a value that would mean nothing. */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_MAX_CANDIDATES, 10),
                    PATHIME_ERROR_UNSUPPORTED);

    pathime_context_destroy(ctx);
}

int main(void)
{
    pathime_engine_t *engine = NULL;

    PT_CHECK_STATUS(pathime_init(NULL), PATHIME_OK);

    /* libhangul is the one backend with no process-global prerequisite that
     * can fail, so if this build contains it, it is available. */
    PT_CHECK(pathime_has_engine(PATHIME_ENGINE_HANGUL));
    PT_CHECK_STATUS(pathime_engine_create(PATHIME_ENGINE_HANGUL, &engine), PATHIME_OK);
    PT_CHECK(engine != NULL);
    PT_CHECK(pathime_engine_id(engine) == PATHIME_ENGINE_HANGUL);

    /* Nothing requires surrounding text until PATHIME_HANGUL_PREEDIT_NONE is
     * the resolved value, and it is not the default. */
    PT_CHECK(pathime_engine_requirements(engine) == 0);

    if (engine != NULL) {
        test_syllable_composition(engine);
        test_word_mode_settled_boundary(engine);
        test_backspace(engine);
        test_key_position_and_shift(engine);
        test_reset_and_focus(engine);
        test_layout_option(engine);
    }

    pathime_engine_destroy(engine);
    pathime_shutdown();
    return pt_report("api.engine_hangul");
}

#endif /* PATHIME_WITH_HANGUL */
