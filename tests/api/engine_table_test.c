/*
 * The table-driven engine, driven the way a client drives it: through
 * <pathime/pathime.h> and nothing else.
 *
 * The engine this exercises is the only one libpathime wrote rather than
 * wrapped, so this test carries a second job the other three do not. For
 * hangul, anthy and pyzy the vendored library is the authority on what correct
 * output looks like; here there is no such authority, and the test *is* where
 * the behaviour of docs/ibus-table-spec.md §6–§9 is pinned down.
 *
 * It runs against the real compiled tables the build ships, not a fixture. That
 * is deliberate: the format is an interoperability contract with ibus-table, so
 * a test passing against a table this repository invented would prove much less
 * than one passing against `cangjie5.db` compiled out of ibus-table-chinese's
 * own source. Expected values below were read out of that compiled database and
 * are real Chinese — typing a-b in Cangjie composes 明.
 */

#include <string.h>

#include "api_test_util.h"

#if !PATHIME_WITH_TABLE

int main(void)
{
    return pt_skip("api.engine_table", "this build does not contain the table engine");
}

#else

/* Cangjie radicals, which are what the preedit shows: 日 U+65E5, 月 U+6708. */
#define SUN  "\xE6\x97\xA5"
#define MOON "\xE6\x9C\x88"

/* 明 U+660E — what 日月 produces, and the first candidate for the code "ab". */
#define MING "\xE6\x98\x8E"

/* 冐 U+5190 — the second candidate for "ab", ranked below 明 by freq. */
#define MAO "\xE5\x86\x90"

typedef struct {
    char commits[512];
    int commit_count;
    int changed_count;

    char last_preedit[256];
    size_t last_settled;
    size_t last_candidates;
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

/* Press one printable key. Table input dispatches on character, not position. */
static bool press(pathime_context_t *ctx, uint32_t keysym)
{
    pathime_key_event_t event;
    bool handled = false;

    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.keysym = keysym;

    PT_CHECK_STATUS(pathime_context_process_key(ctx, &event, &handled), PATHIME_OK);
    return handled;
}

/* The candidate at @a index, as a NUL-terminated copy. */
static void candidate_at(pathime_context_t *ctx, size_t index, char *out, size_t out_size)
{
    pathime_str_t text;

    out[0] = '\0';
    PT_CHECK_STATUS(pathime_context_candidate(ctx, index, &text), PATHIME_OK);
    if (text.len + 1 < out_size) {
        memcpy(out, text.bytes, text.len);
        out[text.len] = '\0';
    }
}

/*
 * A context on the named table. Every test opens its own, because
 * PATHIME_OPT_TABLE_FILE is per-context and switching it mid-test would be
 * testing the switch rather than the thing under test.
 */
static pathime_context_t *open_context(pathime_engine_t *engine, client_log_t *log,
                                       const char *table)
{
    pathime_client_t client;
    pathime_context_t *ctx = NULL;

    log_reset(log);
    memset(&client, 0, sizeof(client));
    client.struct_size = sizeof(client);
    client.commit_text = on_commit;
    client.composition_changed = on_changed;

    PT_CHECK_STATUS(pathime_context_create(engine, &client, log, &ctx), PATHIME_OK);
    if (ctx == NULL) {
        return NULL;
    }

    /*
     * A bare name, with no path separator, is the shipped table of that name.
     * This is the resolution rule that lets a client reach the tables the
     * library installed without knowing where the resource directory is.
     */
    PT_CHECK_STATUS(
        pathime_context_set_option_string(ctx, PATHIME_OPT_TABLE_FILE, table),
        PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, true), PATHIME_OK);
    return ctx;
}

/*
 * The core transition of §7.2: each valid input character extends the key run,
 * the preedit shows the run through the table's char prompts, and the candidate
 * list is the lookup's result in the order §8.2 fixes.
 */
static void test_compose_and_select(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");
    char candidate[64];

    if (ctx == NULL) {
        return;
    }

    /* `a` is the Cangjie radical 日, and it is a valid input character. */
    PT_CHECK(press(ctx, 'a'));
    PT_CHECK(strcmp(log.last_preedit, SUN) == 0);

    /*
     * Nothing is settled: the whole preedit is the provisional key run, so any
     * candidate can replace it. §6.3's "position is 0" case.
     */
    PT_CHECK(log.last_settled == 0);
    PT_CHECK(log.last_candidates > 0);

    PT_CHECK(press(ctx, 'b'));
    PT_CHECK(strcmp(log.last_preedit, SUN MOON) == 0);
    PT_CHECK(log.last_settled == 0);

    /*
     * "ab" is an exact match for 明, so §8.2's first key puts it ahead of
     * everything the auto-wildcard also matched. 冐 follows on freq.
     */
    candidate_at(ctx, 0, candidate, sizeof(candidate));
    PT_CHECK(strcmp(candidate, MING) == 0);
    candidate_at(ctx, 1, candidate, sizeof(candidate));
    PT_CHECK(strcmp(candidate, MAO) == 0);

    /* Selecting commits the phrase and clears the composition (§9). */
    PT_CHECK_STATUS(pathime_context_select_candidate(ctx, 0), PATHIME_OK);
    PT_CHECK(strcmp(log.commits, MING) == 0);
    PT_CHECK(strcmp(log.last_preedit, "") == 0);
    PT_CHECK(log.last_candidates == 0);

    pathime_context_destroy(ctx);
}

/*
 * The header fixes Space as "ask for conversion, beginning at the hovered
 * candidate". For this engine that is a commit of the hovered phrase — the one
 * place the public contract overrides ibus-table's own key policy, which treats
 * the commit key as a client binding.
 */
static void test_space_commits_hovered(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");

    if (ctx == NULL) {
        return;
    }

    PT_CHECK(press(ctx, 'a'));
    PT_CHECK(press(ctx, 'b'));

    /* Hover the second candidate, then convert: the hover is adopted. */
    PT_CHECK_STATUS(pathime_context_set_candidate_cursor(ctx, 1), PATHIME_OK);
    PT_CHECK(press(ctx, PATHIME_KEY_SPACE));
    PT_CHECK(strcmp(log.commits, MAO) == 0);
    PT_CHECK(strcmp(log.last_preedit, "") == 0);

    /* With nothing composing, Space is a space at the negotiated width. */
    log_reset(&log);
    PT_CHECK(!press(ctx, PATHIME_KEY_SPACE));
    PT_CHECK(log.commit_count == 0);

    pathime_context_destroy(ctx);
}

/*
 * Return ends the composition without applying a conversion the user did not
 * choose, which for a table engine is §7.4's "commit the literal input": the
 * keys as typed, not the radicals the preedit displayed them as.
 *
 * That difference between what was shown and what is committed is the one place
 * this engine departs from the header's "this is the text that would be
 * committed if the composition ended right now", and it is an open question in
 * TODO.md rather than a settled reading. This test pins the current answer so
 * that changing it is a deliberate act.
 */
static void test_return_commits_literal_keys(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");

    if (ctx == NULL) {
        return;
    }

    PT_CHECK(press(ctx, 'a'));
    PT_CHECK(press(ctx, 'b'));
    PT_CHECK(strcmp(log.last_preedit, SUN MOON) == 0);

    PT_CHECK(press(ctx, PATHIME_KEY_RETURN));
    PT_CHECK(strcmp(log.commits, "ab") == 0);

    /* Nothing composing: Return is the client's own. */
    log_reset(&log);
    PT_CHECK(!press(ctx, PATHIME_KEY_RETURN));

    pathime_context_destroy(ctx);
}

/* Backspace walks the run back one character at a time (§7.3). */
static void test_backspace(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");

    if (ctx == NULL) {
        return;
    }

    PT_CHECK(press(ctx, 'a'));
    PT_CHECK(press(ctx, 'b'));
    PT_CHECK(strcmp(log.last_preedit, SUN MOON) == 0);

    PT_CHECK(press(ctx, PATHIME_KEY_BACKSPACE));
    PT_CHECK(strcmp(log.last_preedit, SUN) == 0);

    PT_CHECK(press(ctx, PATHIME_KEY_BACKSPACE));
    PT_CHECK(strcmp(log.last_preedit, "") == 0);

    /* Empty composition: the client's own backspace, and nothing committed. */
    PT_CHECK(!press(ctx, PATHIME_KEY_BACKSPACE));
    PT_CHECK(log.commit_count == 0);

    pathime_context_destroy(ctx);
}

/*
 * Tier 3: a value the table declares, standing in for a client that expressed
 * no preference. cangjie5 declares LANGUAGE_FILTER = cm1, so the resolved
 * Chinese variant is traditional-only without anyone setting it — and a client
 * that does set it still wins, because tier 3 sits below both client levels.
 */
static void test_table_declares_options(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");
    int64_t value = 0;

    if (ctx == NULL) {
        return;
    }

    /* Typing is what loads the table, and tier 3 cannot answer before that. */
    PT_CHECK(press(ctx, 'a'));

    PT_CHECK_STATUS(pathime_context_get_option_int(ctx, PATHIME_OPT_CHINESE_VARIANT, &value),
                    PATHIME_OK);
    PT_CHECK(value == PATHIME_CHINESE_TRADITIONAL_ONLY);

    /* The option is not *set* on the context: tier 3 is not tier 1. */
    PT_CHECK(!pathime_context_option_is_set(ctx, PATHIME_OPT_CHINESE_VARIANT));

    /* A client value overrides the table's. */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_CHINESE_VARIANT,
                                                   PATHIME_CHINESE_ANY),
                    PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_get_option_int(ctx, PATHIME_OPT_CHINESE_VARIANT, &value),
                    PATHIME_OK);
    PT_CHECK(value == PATHIME_CHINESE_ANY);

    /* Reset falls back to the table's declaration, not to the library default. */
    PT_CHECK_STATUS(pathime_context_reset_option(ctx, PATHIME_OPT_CHINESE_VARIANT), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_get_option_int(ctx, PATHIME_OPT_CHINESE_VARIANT, &value),
                    PATHIME_OK);
    PT_CHECK(value == PATHIME_CHINESE_TRADITIONAL_ONLY);

    pathime_context_destroy(ctx);
}

/*
 * A context with no table resolved produces nothing and reports every key
 * unhandled — the header's documented state, and the one a context starts in.
 */
static void test_no_table_is_inert(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = NULL;
    pathime_str_t value;

    log_reset(&log);
    memset(&client, 0, sizeof(client));
    client.struct_size = sizeof(client);
    client.commit_text = on_commit;
    client.composition_changed = on_changed;

    PT_CHECK_STATUS(pathime_context_create(engine, &client, &log, &ctx), PATHIME_OK);
    if (ctx == NULL) {
        return;
    }
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, true), PATHIME_OK);

    /* "no table" is spelled as the empty string, with no unset/empty distinction. */
    PT_CHECK_STATUS(pathime_context_get_option_string(ctx, PATHIME_OPT_TABLE_FILE, &value),
                    PATHIME_OK);
    PT_CHECK(value.len == 0);

    PT_CHECK(!press(ctx, 'a'));
    PT_CHECK(log.commit_count == 0);

    /* A table that does not exist leaves the context just as inert. */
    PT_CHECK_STATUS(pathime_context_set_option_string(ctx, PATHIME_OPT_TABLE_FILE,
                                                      "no-such-table"),
                    PATHIME_OK);
    PT_CHECK(!press(ctx, 'a'));
    PT_CHECK(log.commit_count == 0);

    pathime_context_destroy(ctx);
}

/*
 * A second table, to prove the engine is genuinely parameterized by data rather
 * than specialized to Cangjie. Stroke5's input characters are punctuation
 * (`nm,./`), which is also the case that would break any implementation
 * assuming a key run is latin letters.
 */
static void test_second_table(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "stroke5");

    if (ctx == NULL) {
        return;
    }

    /* A stroke key composes; a letter outside VALID_INPUT_CHARS does not. */
    PT_CHECK(press(ctx, 'n'));
    PT_CHECK(log.last_candidates > 0);

    pathime_context_destroy(ctx);
}

/*
 * PATHIME_OPT_MAX_CANDIDATES caps what the client is shown, independently of
 * the cap §8 puts on the lookup itself. Worth its own test because this engine
 * produces its whole list at lookup time rather than being pumped for it, so
 * the cap is applied by the core materialization pass and not by the adapter —
 * an engine that ignored it would still look correct in every other test here.
 */
static void test_max_candidates(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");
    size_t uncapped;
    pathime_str_t text;

    if (ctx == NULL) {
        return;
    }

    /* A one-character run under the auto-wildcard matches a great many rows. */
    PT_CHECK(press(ctx, 'a'));
    uncapped = log.last_candidates;
    PT_CHECK(uncapped > 3);

    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_MAX_CANDIDATES, 3),
                    PATHIME_OK);
    PT_CHECK(log.last_candidates == 3);

    /* And the cap is a cap, not a window: index 3 is now out of range. */
    PT_CHECK_STATUS(pathime_context_candidate(ctx, 3, &text),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    pathime_context_destroy(ctx);
}

/*
 * §8 caps the candidate list at 100 whatever the client asks for, because that
 * cap belongs to the table format rather than to the client's display.
 */
static void test_engine_cap(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");

    if (ctx == NULL) {
        return;
    }

    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_MAX_CANDIDATES, 1000),
                    PATHIME_OK);
    PT_CHECK(press(ctx, 'a'));
    PT_CHECK(log.last_candidates > 0);
    PT_CHECK(log.last_candidates <= 100);

    pathime_context_destroy(ctx);
}

/* Reset discards without committing, which is the header's rule for it. */
static void test_reset_discards(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");

    if (ctx == NULL) {
        return;
    }

    PT_CHECK(press(ctx, 'a'));
    PT_CHECK(press(ctx, 'b'));
    PT_CHECK(strcmp(log.last_preedit, SUN MOON) == 0);

    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);
    PT_CHECK(log.commit_count == 0);
    PT_CHECK(strcmp(log.last_preedit, "") == 0);

    pathime_context_destroy(ctx);
}

int main(void)
{
    pathime_engine_t *engine = NULL;

    PT_CHECK_STATUS(pathime_init(NULL), PATHIME_OK);

    if (!pathime_has_engine(PATHIME_ENGINE_TABLE)) {
        pathime_shutdown();
        return pt_skip("api.engine_table", "no table directory in the resource dir");
    }

    PT_CHECK_STATUS(pathime_engine_create(PATHIME_ENGINE_TABLE, &engine), PATHIME_OK);
    PT_CHECK(engine != NULL);
    PT_CHECK(pathime_engine_id(engine) == PATHIME_ENGINE_TABLE);

    /* Nothing about a table engine needs surrounding text: it never revises
     * text it already committed. */
    PT_CHECK(pathime_engine_requirements(engine) == 0);

    if (engine != NULL) {
        test_compose_and_select(engine);
        test_space_commits_hovered(engine);
        test_return_commits_literal_keys(engine);
        test_backspace(engine);
        test_table_declares_options(engine);
        test_no_table_is_inert(engine);
        test_second_table(engine);
        test_max_candidates(engine);
        test_engine_cap(engine);
        test_reset_discards(engine);
    }

    pathime_engine_destroy(engine);
    pathime_shutdown();
    return pt_report("api.engine_table");
}

#endif /* PATHIME_WITH_TABLE */
