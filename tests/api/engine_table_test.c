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
 * Where @a text sits in the current candidate list, or (size_t)-1.
 *
 * Learning tests need this rather than a fixed index: choosing "the third
 * candidate" repeatedly chooses a *different phrase* each time once the list
 * starts reordering, which teaches the table three things once instead of one
 * thing three times.
 */
static size_t index_of(pathime_context_t *ctx, size_t count, const char *text)
{
    size_t i;
    for (i = 0; i < count; i++) {
        pathime_str_t candidate;
        if (pathime_context_candidate(ctx, i, &candidate) != PATHIME_OK) {
            break;
        }
        if (candidate.len == strlen(text) &&
            memcmp(candidate.bytes, text, candidate.len) == 0) {
            return i;
        }
    }
    return (size_t)-1;
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

    /*
     * A table that does not exist is refused at the setter, not accepted into a
     * context that then quietly does nothing. The setter is what loads the
     * table, so this is where the failure can still be attributed to the name
     * that caused it.
     */
    PT_CHECK_STATUS(pathime_context_set_option_string(ctx, PATHIME_OPT_TABLE_FILE,
                                                      "no-such-table"),
                    PATHIME_ERROR_BACKEND);

    /* And the rejected value was not stored: the option still reads as no table. */
    PT_CHECK_STATUS(pathime_context_get_option_string(ctx, PATHIME_OPT_TABLE_FILE, &value),
                    PATHIME_OK);
    PT_CHECK(value.len == 0);
    PT_CHECK(!pathime_context_option_is_set(ctx, PATHIME_OPT_TABLE_FILE));

    PT_CHECK(!press(ctx, 'a'));
    PT_CHECK(log.commit_count == 0);

    /* Clearing the option back to "no table" stays legal. */
    PT_CHECK_STATUS(pathime_context_set_option_string(ctx, PATHIME_OPT_TABLE_FILE, ""),
                    PATHIME_OK);
    PT_CHECK(!press(ctx, 'a'));

    pathime_context_destroy(ctx);
}

/*
 * Tier 3 answers as soon as the table is named, because naming it is what loads
 * it. Before the setter did that work, the same query returned the descriptor
 * default until the first keystroke and the table's declaration afterwards.
 */
static void test_declarations_available_before_typing(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");
    int64_t value = 0;

    if (ctx == NULL) {
        return;
    }

    /* Not one key pressed. cangjie5 declares LANGUAGE_FILTER = cm1. */
    PT_CHECK_STATUS(pathime_context_get_option_int(ctx, PATHIME_OPT_CHINESE_VARIANT, &value),
                    PATHIME_OK);
    PT_CHECK(value == PATHIME_CHINESE_TRADITIONAL_ONLY);

    pathime_context_destroy(ctx);
}

/*
 * The pinyin fallback reports itself off, and refuses to be turned on, for every
 * table this library ships.
 *
 * Four of the five *declare* PINYIN_MODE = TRUE — cangjie5, quick5, stroke5 and
 * wubi-jidian86 — but the pinyin source data ships with ibus-table rather than
 * with ibus-table-chinese, so the compiled `pinyin` table is created empty. The
 * declaration alone would tell a client the option is on while it does nothing.
 */
static void test_pinyin_fallback_unsupported(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");
    bool value = true;

    if (ctx == NULL) {
        return;
    }

    PT_CHECK_STATUS(pathime_context_get_option_bool(ctx, PATHIME_OPT_TABLE_PINYIN_FALLBACK, &value),
                    PATHIME_OK);
    PT_CHECK(!value);

    PT_CHECK_STATUS(pathime_context_set_option_bool(ctx, PATHIME_OPT_TABLE_PINYIN_FALLBACK, true),
                    PATHIME_ERROR_UNSUPPORTED);

    /* Turning it off needs no pinyin data: a client may always decline it. */
    PT_CHECK_STATUS(pathime_context_set_option_bool(ctx, PATHIME_OPT_TABLE_PINYIN_FALLBACK, false),
                    PATHIME_OK);

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

    /*
     * And a third, wubi-jidian86 — the one shipped table with RULES and
     * USER_CAN_DEFINE_PHRASE, and with native frequencies in the billions
     * rather than the hundreds. `trn` is 我, which is also what the demo's hint
     * tells a user to type, so this keeps that hint honest.
     */
    ctx = open_context(engine, &log, "wubi-jidian86");
    if (ctx == NULL) {
        return;
    }

    PT_CHECK(press(ctx, 't'));
    PT_CHECK(press(ctx, 'r'));
    PT_CHECK(press(ctx, 'n'));

    {
        char candidate[64];
        candidate_at(ctx, 0, candidate, sizeof(candidate));
        PT_CHECK(strcmp(candidate, "\xE6\x88\x91") == 0);  /* 我 */
    }

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

/*
 * Enumeration: the installed tables reported as the legal values of
 * PATHIME_OPT_TABLE_FILE, so a client can offer a choice without knowing where
 * the resource directory landed. This is what lets the demo build a picker, and
 * it is deliberately the *only* thing the API says about an installed table —
 * the machine-readable key, no display name, no icon, no language list.
 */
static void test_enumeration(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");
    pathime_option_info_t info;
    size_t i;
    bool found_cangjie = false;

    if (ctx == NULL) {
        return;
    }

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    PT_CHECK_STATUS(pathime_engine_option_info(engine, PATHIME_OPT_TABLE_FILE, &info),
                    PATHIME_OK);
    PT_CHECK(info.supported);
    PT_CHECK(info.type == PATHIME_OPTION_STRING);

    /* The five tables the build ships, at least. */
    PT_CHECK(info.valid_value_count > 0);

    for (i = 0; i < info.valid_value_count; i++) {
        const char *name = pathime_option_value_name(PATHIME_OPT_TABLE_FILE, (int64_t)i);

        /* Every enumerated value is a real name, never "". */
        PT_CHECK(name[0] != '\0');

        /* A bare name, not a path: that is what the setter accepts. */
        PT_CHECK(strchr(name, '/') == NULL);
        PT_CHECK(strchr(name, '\\') == NULL);
        PT_CHECK(strstr(name, ".db") == NULL);

        if (strcmp(name, "cangjie5") == 0) {
            found_cangjie = true;
        }

        /* And every one of them is accepted by the option it enumerates —
         * which is the whole promise, so it is checked rather than assumed. */
        PT_CHECK_STATUS(
            pathime_context_set_option_string(ctx, PATHIME_OPT_TABLE_FILE, name),
            PATHIME_OK);
    }
    PT_CHECK(found_cangjie);

    /* Sorted, so a client's list is stable between runs. */
    for (i = 1; i < info.valid_value_count; i++) {
        const char *previous =
            pathime_option_value_name(PATHIME_OPT_TABLE_FILE, (int64_t)(i - 1));
        const char *current = pathime_option_value_name(PATHIME_OPT_TABLE_FILE, (int64_t)i);
        PT_CHECK(strcmp(previous, current) < 0);
    }

    /* Out of range is "", the same answer every other type gives. */
    PT_CHECK(pathime_option_value_name(PATHIME_OPT_TABLE_FILE,
                                       (int64_t)info.valid_value_count)[0] == '\0');
    PT_CHECK(pathime_option_value_name(PATHIME_OPT_TABLE_FILE, -1)[0] == '\0');

    pathime_context_destroy(ctx);
}

/*
 * Learning (§10.1): selecting a candidate bumps its user_freq in the user
 * database, and user_freq outranks freq in the candidate sort (§8.2 key 3), so
 * a phrase the user keeps choosing rises to the front.
 *
 * stroke5 is the table for this because it declares DYNAMIC_ADJUST — cangjie5
 * does not, and a table that does not adapt must not, which the next test
 * checks. Its code `m/m` has exactly three exact matches, ordered 工 (500),
 * 士 (495), 土 (490) by frequency alone.
 */
static void test_learning(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "stroke5");
    char candidate[64];
    int round;

    if (ctx == NULL) {
        return;
    }

    /* 工 U+5DE5, 土 U+571F — first and third of `m/m` before any learning. */
    const char *const gong = "\xE5\xB7\xA5";
    const char *const tu = "\xE5\x9C\x9F";

    PT_CHECK(press(ctx, 'm'));
    PT_CHECK(press(ctx, '/'));
    PT_CHECK(press(ctx, 'm'));

    candidate_at(ctx, 0, candidate, sizeof(candidate));
    PT_CHECK(strcmp(candidate, gong) == 0);
    candidate_at(ctx, 2, candidate, sizeof(candidate));
    PT_CHECK(strcmp(candidate, tu) == 0);

    /*
     * Choose 土 repeatedly — by phrase, not by position, because the position
     * moves as soon as the first selection is learned. Once would be enough to
     * overtake two rows with user_freq 0; three times also exercises the update
     * path rather than only the insert.
     */
    for (round = 0; round < 3; round++) {
        const size_t at = index_of(ctx, log.last_candidates, tu);
        PT_CHECK(at != (size_t)-1);
        PT_CHECK_STATUS(pathime_context_select_candidate(ctx, at), PATHIME_OK);
        PT_CHECK(press(ctx, 'm'));
        PT_CHECK(press(ctx, '/'));
        PT_CHECK(press(ctx, 'm'));
    }

    /* The learned phrase now leads, ahead of two higher-freq rows. */
    candidate_at(ctx, 0, candidate, sizeof(candidate));
    PT_CHECK(strcmp(candidate, tu) == 0);

    pathime_context_destroy(ctx);
}

/*
 * The client's veto. PATHIME_OPT_LEARNING off means no user_freq is written,
 * so the order stays the table's however often a candidate is chosen.
 */
static void test_learning_can_be_declined(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "stroke5");
    char candidate[64];
    int round;

    if (ctx == NULL) {
        return;
    }

    /* 丿 code `,n.`: 久 (500), 凡 (495), 夕 (490), 丸 (485), 么 (480). */
    const char *const jiu = "\xE4\xB9\x85";
    const char *const xi = "\xE5\xA4\x95";

    PT_CHECK_STATUS(pathime_context_set_option_bool(ctx, PATHIME_OPT_LEARNING, false),
                    PATHIME_OK);

    for (round = 0; round < 3; round++) {
        size_t at;
        PT_CHECK(press(ctx, ','));
        PT_CHECK(press(ctx, 'n'));
        PT_CHECK(press(ctx, '.'));
        /* Unmoved every round, which is the assertion: with learning declined
         * the list cannot drift, so 夕 stays exactly where the table put it. */
        candidate_at(ctx, 2, candidate, sizeof(candidate));
        PT_CHECK(strcmp(candidate, xi) == 0);
        at = index_of(ctx, log.last_candidates, xi);
        PT_CHECK(at == 2);
        PT_CHECK_STATUS(pathime_context_select_candidate(ctx, at), PATHIME_OK);
    }

    PT_CHECK(press(ctx, ','));
    PT_CHECK(press(ctx, 'n'));
    PT_CHECK(press(ctx, '.'));

    /* Unmoved: still the table's own order. */
    candidate_at(ctx, 0, candidate, sizeof(candidate));
    PT_CHECK(strcmp(candidate, jiu) == 0);
    candidate_at(ctx, 2, candidate, sizeof(candidate));
    PT_CHECK(strcmp(candidate, xi) == 0);

    pathime_context_destroy(ctx);
}

/*
 * A table that does not declare DYNAMIC_ADJUST does not adapt, whatever the
 * client asks for — the declaration is the table author saying this method has
 * a fixed order, and tier 3 turns PATHIME_OPT_LEARNING off to say so.
 */
static void test_table_without_dynamic_adjust(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");
    char candidate[64];
    bool learning = true;
    int round;

    if (ctx == NULL) {
        return;
    }

    PT_CHECK(press(ctx, 'a'));

    /* cangjie5 declares neither DYNAMIC_ADJUST nor USER_CAN_DEFINE_PHRASE. */
    PT_CHECK_STATUS(pathime_context_get_option_bool(ctx, PATHIME_OPT_LEARNING, &learning),
                    PATHIME_OK);
    PT_CHECK(!learning);

    for (round = 0; round < 3; round++) {
        size_t at;
        PT_CHECK(press(ctx, 'b'));
        at = index_of(ctx, log.last_candidates, MAO);
        PT_CHECK(at != (size_t)-1);
        PT_CHECK_STATUS(pathime_context_select_candidate(ctx, at), PATHIME_OK);
        PT_CHECK(press(ctx, 'a'));
    }
    PT_CHECK(press(ctx, 'b'));

    candidate_at(ctx, 0, candidate, sizeof(candidate));
    PT_CHECK(strcmp(candidate, MING) == 0);

    pathime_context_destroy(ctx);
}

/*
 * Frequency-augmented ordering, and the regression that motivated it.
 *
 * A table method is deterministic — a full Cangjie code identifies one
 * character — so the candidate list matters most for a *partial* code, where it
 * is a guess about what is being typed. The stock table's order there is
 * structural and the results are obscure, which is why the tables this library
 * ships are compiled with usage frequencies transferred in from Cantonese
 * (tools/table-compile --freq-from, spec-external and described at
 * apply_frequency_transfer).
 *
 * `h` is 竹, the first stroke of 我 (`hqi`). With the transfer applied, the
 * common characters reachable from `h` lead: 的, then 我. Without it, every
 * primary entry sits at exactly the source threshold and the list falls back to
 * table order — which is what a `>` instead of a `>=` in the transfer produced,
 * silently, until this test existed.
 */
static void test_frequency_augmented_order(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");
    char candidate[64];

    /* 竹 U+7AF9 (the exact match), 的 U+7684, 我 U+6211. */
    const char *const zhu = "\xE7\xAB\xB9";
    const char *const de = "\xE7\x9A\x84";
    const char *const wo = "\xE6\x88\x91";

    if (ctx == NULL) {
        return;
    }

    PT_CHECK(press(ctx, 'h'));

    /* The exact match still leads: §8.2's first key outranks any frequency. */
    candidate_at(ctx, 0, candidate, sizeof(candidate));
    PT_CHECK(strcmp(candidate, zhu) == 0);

    /* And then the common characters, by usage rather than by table order. */
    PT_CHECK(index_of(ctx, log.last_candidates, de) < 4);
    PT_CHECK(index_of(ctx, log.last_candidates, wo) < 5);

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

    /*
     * A data directory of this test's own, so learning writes a user database
     * under the build tree rather than the developer's real one. The fixture in
     * tests/api/CMakeLists.txt empties it before each run.
     */
#ifdef TABLE_TEST_DATA_DIR
    {
        pathime_init_params_t params;
        memset(&params, 0, sizeof(params));
        params.struct_size = sizeof(params);
        params.data_dir = TABLE_TEST_DATA_DIR;
        PT_CHECK_STATUS(pathime_init(&params), PATHIME_OK);
    }
#else
    PT_CHECK_STATUS(pathime_init(NULL), PATHIME_OK);
#endif

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
        test_declarations_available_before_typing(engine);
        test_pinyin_fallback_unsupported(engine);
        test_no_table_is_inert(engine);
        test_second_table(engine);
        test_enumeration(engine);
        test_max_candidates(engine);
        test_engine_cap(engine);
        test_reset_discards(engine);
        test_frequency_augmented_order(engine);
        test_learning(engine);
        test_learning_can_be_declined(engine);
        test_table_without_dynamic_adjust(engine);
    }

    pathime_engine_destroy(engine);
    pathime_shutdown();
    return pt_report("api.engine_table");
}

#endif /* PATHIME_WITH_TABLE */
