/*
 * The table-driven engine, driven the way a client drives it: through
 * <pathime/pathime.h> and nothing else.
 *
 * The engine this exercises is the only one libpathime wrote rather than
 * wrapped, so this test carries a second job the other three do not. For
 * hangul, anthy and pyzy the vendored library is the authority on what correct
 * output looks like; here there is no such authority, and the test *is* where
 * this engine's key-event behaviour is pinned down.
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

/*
 * The current preedit as a NUL-terminated C string.
 *
 * Most tests here read log.last_preedit, which is the same string seen from
 * the client's side. This one is for the places that need a context's preedit
 * *while another context is the one being keyed*, where no callback has fired
 * for the context being asked about.
 */
static const char *preedit_of(pathime_context_t *ctx)
{
    return pathime_context_composition(ctx)->preedit.bytes;
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
    return ctx;
}

/*
 * The core transition: each valid input character extends the key run,
 * the preedit shows the run through the table's char prompts, and the candidate
 * list is the lookup's result in the order docs/ibus-table-mapping.md §8.2
 * fixes.
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
     * candidate can replace it: the "display position is 0" case.
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

    /* Selecting commits the phrase and clears the composition. */
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

    /*
     * With nothing composing, Space is a space at the negotiated width — and it
     * is *handled* even at half width, where the character is unchanged. A CJK
     * table claims every printable ASCII key so that the two look-behind
     * substitutions can see the document; test_punctuation_width below is what
     * that buys.
     */
    log_reset(&log);
    PT_CHECK(press(ctx, PATHIME_KEY_SPACE));
    PT_CHECK(strcmp(log.commits, " ") == 0);

    /* Full-width Latin makes the same key U+3000. */
    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_LATIN_WIDTH,
                                                   PATHIME_WIDTH_FULL),
                    PATHIME_OK);
    PT_CHECK(press(ctx, PATHIME_KEY_SPACE));
    PT_CHECK(strcmp(log.commits, "\xE3\x80\x80") == 0);

    pathime_context_destroy(ctx);
}

/*
 * Full-width punctuation, which the table engine takes from the shared layer in
 * src/punctuation.h rather than from spec §11.4 — one option cannot mean two
 * things across the two Chinese engines, and the four characters where the
 * references disagree are named there.
 *
 * The variant that picks the table is not set by this test. It arrives through
 * tier 3 from each table's own LANGUAGE_FILTER, which is what makes the same
 * key give different brackets below without a client saying anything.
 */
static void test_punctuation_width(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");

    if (ctx == NULL) {
        return;
    }

    /* Full-width punctuation with half-width Latin is the documented default. */

    /* cangjie5 declares LANGUAGE_FILTER = cm1, so brackets are the corner pair. */
    log_reset(&log);
    PT_CHECK(press(ctx, '['));
    PT_CHECK(strcmp(log.commits, "\xE3\x80\x8C") == 0); /* 「 */

    /* The quote keys alternate opening and closing. */
    log_reset(&log);
    PT_CHECK(press(ctx, '"'));
    PT_CHECK(strcmp(log.commits, "\xE2\x80\x9C") == 0); /* “ */
    log_reset(&log);
    PT_CHECK(press(ctx, '"'));
    PT_CHECK(strcmp(log.commits, "\xE2\x80\x9D") == 0); /* ” */

    /*
     * And the digit look-behind: "1.5" survives. This is the case that forces a
     * CJK table to claim even the keys it passes through unchanged — at the
     * default half-width Latin the `1` converts to itself, and an engine that
     * declined it would never disarm the decimal-point rule.
     */
    log_reset(&log);
    PT_CHECK(press(ctx, '1'));
    PT_CHECK(press(ctx, '.'));
    PT_CHECK(press(ctx, '5'));
    PT_CHECK(strcmp(log.commits, "1.5") == 0);

    /*
     * With something other than a digit in front, the same key is a full stop.
     * The bracket is what disarms the look-behind — after the `5` above it is
     * still armed, which is the rule working rather than an artefact.
     */
    log_reset(&log);
    PT_CHECK(press(ctx, '['));
    PT_CHECK(press(ctx, '.'));
    PT_CHECK(strcmp(log.commits, "\xE3\x80\x8C\xE3\x80\x82") == 0); /* 「。 */

    /* Half-width punctuation passes everything through unchanged. */
    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_PUNCTUATION_WIDTH,
                                                   PATHIME_WIDTH_HALF),
                    PATHIME_OK);
    PT_CHECK(press(ctx, '['));
    PT_CHECK(strcmp(log.commits, "[") == 0);

    pathime_context_destroy(ctx);
}

/*
 * The same key, a different table, a different bracket — with no client
 * involvement. wubi-jidian86 declares LANGUAGE_FILTER = cm2 where cangjie5
 * declares cm1, so tier 3 resolves PATHIME_OPT_CHINESE_VARIANT differently and
 * the punctuation layer picks the other table.
 */
static void test_punctuation_follows_table_variant(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "wubi-jidian86");

    if (ctx == NULL) {
        return;
    }

    log_reset(&log);
    PT_CHECK(press(ctx, '['));
    PT_CHECK(strcmp(log.commits, "\xE3\x80\x90") == 0); /* 【, the simplified pair */

    pathime_context_destroy(ctx);
}

/*
 * Return ends the composition without applying a conversion the user did not
 * choose, which for a table engine is "commit the literal input": the
 * keys as typed, not the radicals the preedit displayed them as.
 *
 * The preedit and the commit are two renderings of one key run: the table's
 * char prompts are the *keycap legend* — cangjie5 prints 日 on `a` and 月 on `b`
 * — so the preedit is the Cangjie keyboard rendered back at the user, and
 * committing without choosing a candidate is the method's escape hatch to Latin
 * text. The header's preedit clause covers this explicitly.
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

/* Backspace walks the run back one character at a time. */
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
 * A character that extends the run past every match is kept rather than
 * discarded or handed back to the client, and backspace takes it off again.
 *
 * That is the repair path for a typo in the middle of a code: the user has
 * typed four characters, the fourth was wrong, and what they expect from
 * backspace is the three good ones back — not an empty composition and not a
 * stray latin letter in their document. The engine holds the bad tail
 * separately from the matching run for exactly this reason.
 */
static void test_backspace_removes_an_invalid_tail(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");

    if (ctx == NULL) {
        return;
    }

    /* `vnd` is 好, the one row cangjie5 reaches with those three keys. */
    PT_CHECK(press(ctx, 'v'));
    PT_CHECK(press(ctx, 'n'));
    PT_CHECK(press(ctx, 'd'));
    PT_CHECK_SIZE(log.last_candidates, 1);

    /*
     * `q` extends it to something no row carries. It is absorbed — the client
     * must not receive it as a keystroke — and nothing is committed.
     *
     * The matching run is left standing behind it: the candidate for `vnd` is
     * still there, because the bad character was held apart rather than folded
     * into the run. That is what makes the backspace below a repair instead of
     * a re-type, and it is the visible difference from AUTO_SELECT, which
     * settles the match instead of holding it.
     */
    PT_CHECK(press(ctx, 'q'));
    PT_CHECK(log.commit_count == 0);
    PT_CHECK_SIZE(log.last_candidates, 1);

    /* Backspace removes the bad character and the run matches once more. */
    PT_CHECK(press(ctx, PATHIME_KEY_BACKSPACE));
    PT_CHECK(log.commit_count == 0);
    PT_CHECK_SIZE(log.last_candidates, 1);

    /* And the composition is exactly where it was, so it can be finished. */
    PT_CHECK_STATUS(pathime_context_select_candidate(ctx, 0), PATHIME_OK);
    PT_CHECK(log.commit_count == 1);

    pathime_context_destroy(ctx);
}

/*
 * PATHIME_OPT_TABLE_AUTO_SELECT: when the run stops matching, settle what it
 * matched a moment ago and start a new run from the character that broke it.
 *
 * The alternative — holding the character as an invalid tail, which is what the
 * test above shows with the option off — is right for a typo and wrong for a
 * user typing continuously, who meant the new character as the start of the
 * next code. The option is which of those two readings the table wants.
 */
static void test_auto_select_restarts_the_run(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");

    if (ctx == NULL) {
        return;
    }

    PT_CHECK_STATUS(
        pathime_context_set_option_bool(ctx, PATHIME_OPT_TABLE_AUTO_SELECT, true),
        PATHIME_OK);

    PT_CHECK(press(ctx, 'v'));
    PT_CHECK(press(ctx, 'n'));
    PT_CHECK(press(ctx, 'd'));
    PT_CHECK_SIZE(log.last_candidates, 1);

    /*
     * `a` matches nothing after `vnd`, so 好 settles and `a` becomes the whole
     * of a fresh run — which does match, since `a` is 日's code.
     */
    PT_CHECK(press(ctx, 'a'));
    PT_CHECK(log.commit_count == 1);
    PT_CHECK(strcmp(log.commits, "\xE5\xA5\xBD") == 0);   /* 好 */

    /* The new run is live, not an invalid tail: it has candidates. */
    PT_CHECK(log.last_candidates > 0);

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

    /*
     * Setting an unrelated option on a context with no table.
     *
     * This is the one thing an inert context was never asked to survive, and
     * it crashed: an option change republishes the composition, and publishing
     * reads the table's properties for the char prompts. Every other entry
     * point had learned to check for the missing table; this path reached it
     * through options_changed() -> publish() -> displayed_run() and did not.
     *
     * It is reachable by any client, not only by a build that shipped no
     * tables: naming a table that does not exist leaves a context in exactly
     * this state, having been told so by PATHIME_ERROR_BACKEND above, and a
     * client is entitled to carry on configuring it.
     */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_MAX_CANDIDATES, 5),
                    PATHIME_OK);
    PT_CHECK(pathime_context_composition(ctx)->preedit.len == 0);
    PT_CHECK(pathime_context_composition(ctx)->candidate_count == 0);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_CHINESE_VARIANT,
                                                   PATHIME_CHINESE_TRADITIONAL_ONLY),
                    PATHIME_OK);
    PT_CHECK(!press(ctx, 'a'));
    PT_CHECK(log.commit_count == 0);

    /* Commit and reset are equally inert, and equally must not crash. */
    PT_CHECK_STATUS(pathime_context_commit(ctx), PATHIME_OK);
    PT_CHECK(log.commit_count == 0);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    pathime_context_destroy(ctx);
}

/*
 * The `z` wildcard, and the position rule that lets cangjie5 have it without
 * losing its `z`-prefixed punctuation codes.
 *
 * cangjie5 declares no wildcard, and tools/table-compile derives one because the
 * table never uses `z` after the first key. See derive_single_wildcard() in
 * table_source.h for why this is checked per table rather than defaulted once —
 * wubi-jidian86, which is also shipped, fails the check.
 */
static void test_z_wildcard(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");
    pathime_str_t value;

    if (ctx == NULL) {
        return;
    }

    /* Derived at compile time and visible through tier 3, not set by anyone. */
    PT_CHECK_STATUS(pathime_context_get_option_string(ctx, PATHIME_OPT_TABLE_SINGLE_WILDCARD,
                                                      &value),
                    PATHIME_OK);
    PT_CHECK(value.len == 1 && value.bytes[0] == 'z');
    PT_CHECK(!pathime_context_option_is_set(ctx, PATHIME_OPT_TABLE_SINGLE_WILDCARD));

    /*
     * 我 is `hqi`. Typing `hz` stands in for the middle key, so 我 must be
     * reachable without knowing the whole decomposition — which is the entire
     * point of the feature.
     */
    log_reset(&log);
    PT_CHECK(press(ctx, 'h'));
    PT_CHECK(press(ctx, 'z'));
    PT_CHECK(press(ctx, 'i'));
    PT_CHECK(index_of(ctx, log.last_candidates, "\xE6\x88\x91") != (size_t)-1); /* 我 */
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    /*
     * And a *leading* `z` stays literal, so the punctuation codes still resolve.
     * `za` is the opening single quote in cangjie5. If the wildcard applied at
     * position 0 this would be a search for everything instead.
     */
    log_reset(&log);
    PT_CHECK(press(ctx, 'z'));
    PT_CHECK(press(ctx, 'a'));
    PT_CHECK(index_of(ctx, log.last_candidates, "\xE2\x80\x98") != (size_t)-1); /* ‘ */

    pathime_context_destroy(ctx);
}

/*
 * Tier 3 answers as soon as the table is named, because naming it is what loads
 * it: a client can read a table's declared options without sending a key first,
 * and the answer does not change once typing begins.
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
 * PATHIME_OPT_TABLE_AUTO_COMMIT, both of the things the header says it does:
 * a run matching exactly one entry commits that entry outright, mid-run, and a
 * run that has reached MAX_KEY_LENGTH stages what stands rather than absorbing
 * the next key.
 *
 * It takes two tables, because the default is the table's to declare and the
 * two shipped answers are opposite: stroke5 declares AUTO_COMMIT = TRUE and
 * cangjie5 declares FALSE. So the same three keystrokes deliver text through
 * one table and not through the other, with no client involved — and a client
 * setting the option overrides either, tier 1 over tier 3.
 */
static void test_auto_commit(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx;
    char candidate[64];
    char standing[64];
    bool value = false;

    /* 好 U+597D — the one row cangjie5's `vnd` reaches. */
    const char *const hao = "\xE5\xA5\xBD";
    /* 介 U+4ECB — likewise the only row under stroke5's `,.,/`. */
    const char *const jie = "\xE4\xBB\x8B";

    /* cangjie5 declares AUTO_COMMIT = FALSE, so nothing commits by itself. */
    ctx = open_context(engine, &log, "cangjie5");
    if (ctx == NULL) {
        return;
    }

    PT_CHECK(press(ctx, 'v'));
    PT_CHECK(press(ctx, 'n'));
    PT_CHECK(press(ctx, 'd'));

    PT_CHECK_STATUS(pathime_context_get_option_bool(ctx, PATHIME_OPT_TABLE_AUTO_COMMIT,
                                                    &value),
                    PATHIME_OK);
    PT_CHECK(!value);

    /* One candidate left, and it is waiting to be selected rather than sent. */
    PT_CHECK_SIZE(log.last_candidates, 1);
    PT_CHECK(log.commit_count == 0);
    candidate_at(ctx, 0, candidate, sizeof(candidate));
    PT_CHECK(strcmp(candidate, hao) == 0);

    pathime_context_destroy(ctx);

    /* The same keys, with the client asking for auto-commit. */
    ctx = open_context(engine, &log, "cangjie5");
    if (ctx == NULL) {
        return;
    }

    PT_CHECK_STATUS(pathime_context_set_option_bool(ctx, PATHIME_OPT_TABLE_AUTO_COMMIT,
                                                    true),
                    PATHIME_OK);
    PT_CHECK(press(ctx, 'v'));
    PT_CHECK(press(ctx, 'n'));
    PT_CHECK(press(ctx, 'd'));

    /*
     * The third key delivered text on its own: `vnd` is an exact match and the
     * only match, so there was nothing left to choose between. Cangjie's
     * MAX_KEY_LENGTH is 5 — this is the mid-run half of the option, not the
     * boundary half.
     */
    PT_CHECK(strcmp(log.commits, hao) == 0);
    PT_CHECK(strcmp(log.last_preedit, "") == 0);

    pathime_context_destroy(ctx);

    /* stroke5 declares it, so the same behaviour arrives without a client. */
    ctx = open_context(engine, &log, "stroke5");
    if (ctx == NULL) {
        return;
    }

    PT_CHECK(press(ctx, ','));
    PT_CHECK_STATUS(pathime_context_get_option_bool(ctx, PATHIME_OPT_TABLE_AUTO_COMMIT,
                                                    &value),
                    PATHIME_OK);
    PT_CHECK(value);
    PT_CHECK(!pathime_context_option_is_set(ctx, PATHIME_OPT_TABLE_AUTO_COMMIT));

    PT_CHECK(press(ctx, '.'));
    PT_CHECK(press(ctx, ','));
    PT_CHECK(press(ctx, '/'));
    PT_CHECK(strcmp(log.commits, jie) == 0);
    PT_CHECK(strcmp(log.last_preedit, "") == 0);

    pathime_context_destroy(ctx);

    /*
     * The boundary half. `/nm/m` is five keys, stroke5's MAX_KEY_LENGTH, and it
     * matches far too many rows to have committed on the way. A sixth key can
     * neither extend the run nor be thrown away, so with auto-commit on it
     * stages the candidate standing at that moment and starts a fresh run
     * behind it.
     */
    ctx = open_context(engine, &log, "stroke5");
    if (ctx == NULL) {
        return;
    }

    PT_CHECK(press(ctx, '/'));
    PT_CHECK(press(ctx, 'n'));
    PT_CHECK(press(ctx, 'm'));
    PT_CHECK(press(ctx, '/'));
    PT_CHECK(press(ctx, 'm'));
    PT_CHECK(log.last_candidates > 1);
    PT_CHECK(log.commit_count == 0);

    /*
     * Read the standing candidate rather than naming it: what stages is the
     * one under the cursor, and stroke5 learns, so an earlier test's choices
     * are entitled to have moved it.
     */
    candidate_at(ctx, 0, standing, sizeof(standing));
    PT_CHECK(standing[0] != '\0');

    PT_CHECK(press(ctx, 'n'));

    /*
     * Staged, not committed: the phrase is now the settled head of the preedit,
     * with the new run's prompt after it. Every stroke5 phrase is one
     * character, so that head is one scalar value long.
     */
    PT_CHECK(log.commit_count == 0);
    PT_CHECK_SIZE(log.last_settled, 1);
    PT_CHECK(strncmp(log.last_preedit, standing, strlen(standing)) == 0);
    PT_CHECK(strlen(log.last_preedit) > strlen(standing));

    /* Discard rather than commit: this test has no business teaching stroke5. */
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);
    pathime_context_destroy(ctx);

    /* With the client turning it off, the same key is absorbed instead. */
    ctx = open_context(engine, &log, "stroke5");
    if (ctx == NULL) {
        return;
    }

    PT_CHECK_STATUS(pathime_context_set_option_bool(ctx, PATHIME_OPT_TABLE_AUTO_COMMIT,
                                                    false),
                    PATHIME_OK);
    PT_CHECK(press(ctx, '/'));
    PT_CHECK(press(ctx, 'n'));
    PT_CHECK(press(ctx, 'm'));
    PT_CHECK(press(ctx, '/'));
    PT_CHECK(press(ctx, 'm'));

    {
        const size_t before = log.last_candidates;
        char preedit_before[256];

        snprintf(preedit_before, sizeof(preedit_before), "%s", log.last_preedit);

        /*
         * Handled, because letting it through would drop a stroke key into the
         * client's document in the middle of a composition — but nothing about
         * the composition moves.
         */
        PT_CHECK(press(ctx, 'n'));
        PT_CHECK(strcmp(log.last_preedit, preedit_before) == 0);
        PT_CHECK_SIZE(log.last_candidates, before);
        PT_CHECK_SIZE(log.last_settled, 0);
        PT_CHECK(log.commit_count == 0);
    }

    pathime_context_destroy(ctx);
}

/* Unicode scalar values in a NUL-terminated UTF-8 string. */
static size_t scalar_count(const char *text)
{
    size_t count = 0;
    size_t i;

    for (i = 0; text[i] != '\0'; i++) {
        if (((unsigned char)text[i] & 0xC0) != 0x80) {
            count++;
        }
    }
    return count;
}

/* The whole candidate list, as NUL-terminated copies. Returns how many. */
static size_t collect_candidates(pathime_context_t *ctx, size_t count,
                                 char out[][32], size_t out_max)
{
    size_t i;

    for (i = 0; i < count && i < out_max; i++) {
        candidate_at(ctx, i, out[i], 32);
    }
    return i;
}

/*
 * PATHIME_OPT_TABLE_SINGLE_CHAR_ONLY: the multi-character phrases leave the
 * candidate list and the single characters stay in the order they were already
 * in. It is a client's narrowing of what it wants to see rather than one of the
 * table's ordering rules, which is why the surviving order has to be the same
 * order — a filter that re-ranked would be a different feature.
 *
 * wubi-jidian86 is the shipped table this is visible in: cangjie5, quick5 and
 * stroke5 hold nothing but single characters, so the option would pass through
 * them unnoticed. `ggg` reaches a list with both kinds in it.
 *
 * Nothing here names a candidate. The glyph-coverage map the build chose
 * decides which rows survived compilation (BUILD.md, "Glyph coverage"), so the
 * list's contents are a property of the configuration; that the filter keeps
 * exactly the single characters, in order, is not.
 */
static void test_single_char_only(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "wubi-jidian86");
    char unfiltered[100][32];
    char filtered[100][32];
    char restored[100][32];
    size_t n_unfiltered, n_filtered, n_restored;
    size_t singles = 0;
    size_t phrases = 0;
    size_t i, at;
    bool value = true;

    if (ctx == NULL) {
        return;
    }

    /* No table declares this one, so it starts at the library default. */
    PT_CHECK_STATUS(pathime_context_get_option_bool(ctx, PATHIME_OPT_TABLE_SINGLE_CHAR_ONLY,
                                                    &value),
                    PATHIME_OK);
    PT_CHECK(!value);
    PT_CHECK(!pathime_context_option_is_set(ctx, PATHIME_OPT_TABLE_SINGLE_CHAR_ONLY));

    PT_CHECK(press(ctx, 'g'));
    PT_CHECK(press(ctx, 'g'));
    PT_CHECK(press(ctx, 'g'));

    n_unfiltered = collect_candidates(ctx, log.last_candidates, unfiltered,
                                      sizeof(unfiltered) / sizeof(unfiltered[0]));
    for (i = 0; i < n_unfiltered; i++) {
        if (scalar_count(unfiltered[i]) == 1) {
            singles++;
        } else {
            phrases++;
        }
    }

    /* The premise: this code reaches both kinds. */
    PT_CHECK(singles > 0);
    PT_CHECK(phrases > 0);

    /*
     * Set it mid-composition, which is also the assertion that the option
     * reaches a list already on screen rather than only the next lookup.
     */
    PT_CHECK_STATUS(pathime_context_set_option_bool(ctx, PATHIME_OPT_TABLE_SINGLE_CHAR_ONLY,
                                                    true),
                    PATHIME_OK);

    n_filtered = collect_candidates(ctx, log.last_candidates, filtered,
                                    sizeof(filtered) / sizeof(filtered[0]));
    PT_CHECK_SIZE(n_filtered, singles);
    PT_CHECK(n_filtered < n_unfiltered);

    /* Every survivor is one character, and they are the survivors in order. */
    at = 0;
    for (i = 0; i < n_unfiltered; i++) {
        if (scalar_count(unfiltered[i]) != 1) {
            continue;
        }
        if (at < n_filtered) {
            PT_CHECK(strcmp(filtered[at], unfiltered[i]) == 0);
        }
        at++;
    }
    for (i = 0; i < n_filtered; i++) {
        PT_CHECK_SIZE(scalar_count(filtered[i]), 1);
    }

    /* The preedit is the key run either way: this narrows the list, not the run. */
    PT_CHECK(strcmp(log.last_preedit, "ggg") == 0);
    PT_CHECK(log.commit_count == 0);

    /* And it is a filter, not a rebuild: turning it off restores the list. */
    PT_CHECK_STATUS(pathime_context_set_option_bool(ctx, PATHIME_OPT_TABLE_SINGLE_CHAR_ONLY,
                                                    false),
                    PATHIME_OK);
    n_restored = collect_candidates(ctx, log.last_candidates, restored,
                                    sizeof(restored) / sizeof(restored[0]));
    PT_CHECK_SIZE(n_restored, n_unfiltered);
    for (i = 0; i < n_restored && i < n_unfiltered; i++) {
        PT_CHECK(strcmp(restored[i], unfiltered[i]) == 0);
    }

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
 * Learning: selecting a candidate bumps its user_freq in the user
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

/*
 * Commit ends the run, keeping the literal keys — the one engine where what is
 * committed is deliberately not what the preedit showed.
 *
 * Cangjie supplies char prompts, so `ab` renders as 日月 and commits as `ab`.
 * That is the documented inexactness in pathime_composition_t::preedit's
 * "would be committed right now" guarantee, and a forced commit inherits it
 * rather than inventing a second answer: it is exactly what Return does.
 */
static void test_commit(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_context_t *ctx = open_context(engine, &log, "cangjie5");

    if (ctx == NULL) {
        return;
    }

    PT_CHECK(press(ctx, 'a'));
    PT_CHECK(press(ctx, 'b'));
    PT_CHECK(strcmp(log.last_preedit, SUN MOON) == 0);

    PT_CHECK_STATUS(pathime_context_commit(ctx), PATHIME_OK);
    PT_CHECK(log.commit_count == 1);
    PT_CHECK(strcmp(log.commits, "ab") == 0);
    PT_CHECK(strcmp(log.last_preedit, "") == 0);

    /* Nothing composing: no-op, no callbacks. */
    log.commit_count = 0;
    log.changed_count = 0;
    PT_CHECK_STATUS(pathime_context_commit(ctx), PATHIME_OK);
    PT_CHECK(log.commit_count == 0);
    PT_CHECK(log.changed_count == 0);

    pathime_context_destroy(ctx);
}

/*
 * Two contexts of the same engine, keyed alternately — and the shared database
 * underneath them.
 *
 * This engine shares more between contexts than the other three. A
 * TableDatabase is opened once per table name and handed to every context that
 * asks for that name, user database included, so two contexts on one table are
 * two key runs over one set of rows and one set of learned frequencies. Two
 * contexts on *different* tables are the opposite case: nothing shared but the
 * engine handle, and a key run that means one thing in Cangjie and another in
 * Stroke.
 *
 * Both halves are checked here because the two failure modes are opposite. A
 * context leaking its key run into its neighbour would show in the first half;
 * a per-context copy of what is meant to be shared learning would show in the
 * second.
 */
static void test_interleaved_contexts(pathime_engine_t *engine)
{
    client_log_t log_a, log_b, log_c;
    pathime_context_t *a = open_context(engine, &log_a, "cangjie5");
    pathime_context_t *b = open_context(engine, &log_b, "stroke5");
    pathime_context_t *c = NULL;
    char candidate[64];
    char target[64];
    size_t before;
    size_t after;
    int round;

    if (a == NULL || b == NULL) {
        pathime_context_destroy(b);
        pathime_context_destroy(a);
        return;
    }

    /* --- different tables, one key at a time each ------------------------ */

    PT_CHECK(press(a, 'a'));
    PT_CHECK(strcmp(log_a.last_preedit, SUN) == 0);
    PT_CHECK_SIZE(log_b.changed_count, 0);

    /* 'm' is a stroke key in stroke5 and not an input character in cangjie5,
     * so this key is only meaningful in the context it was sent to. */
    PT_CHECK(press(b, 'm'));
    PT_CHECK(strcmp(preedit_of(a), SUN) == 0);

    PT_CHECK(press(a, 'b'));
    PT_CHECK(strcmp(preedit_of(a), SUN MOON) == 0);
    candidate_at(a, 0, candidate, sizeof(candidate));
    PT_CHECK(strcmp(candidate, MING) == 0);

    PT_CHECK(press(b, '/'));
    PT_CHECK(press(b, 'm'));
    PT_CHECK(pathime_context_composition(b)->candidate_count > 0);

    /* a's lookup did not move while b was being typed into, and neither list
     * is the other's. */
    PT_CHECK(strcmp(preedit_of(a), SUN MOON) == 0);
    candidate_at(a, 0, candidate, sizeof(candidate));
    PT_CHECK(strcmp(candidate, MING) == 0);
    candidate_at(b, 0, candidate, sizeof(candidate));
    PT_CHECK(strcmp(candidate, MING) != 0);

    /*
     * Two tables, two preedits — the stroke table renders its keys with its
     * own prompts, so these cannot coincide. And each context's own view
     * agrees with what its client was handed, which is the same composition
     * reached the other way round.
     */
    PT_CHECK(strcmp(preedit_of(a), preedit_of(b)) != 0);
    PT_CHECK(strcmp(preedit_of(a), log_a.last_preedit) == 0);
    PT_CHECK(strcmp(preedit_of(b), log_b.last_preedit) == 0);

    /* Two keys into a and three into b, and that many callbacks into each
     * client: a leak would show as a count as readily as as a string. */
    PT_CHECK_SIZE(log_a.changed_count, 2);
    PT_CHECK_SIZE(log_b.changed_count, 3);
    PT_CHECK_SIZE(log_a.commit_count, 0);
    PT_CHECK_SIZE(log_b.commit_count, 0);

    /* --- the same table, and the learning both contexts share ------------- */

    c = open_context(engine, &log_c, "stroke5");
    if (c == NULL) {
        pathime_context_destroy(b);
        pathime_context_destroy(a);
        return;
    }

    PT_CHECK(press(c, 'm'));
    PT_CHECK(press(c, '/'));
    PT_CHECK(press(c, 'm'));

    /* Two contexts, one table, one answer — and c's answer is c's own, not the
     * cangjie context's that is still standing beside it. */
    PT_CHECK(strcmp(preedit_of(c), log_c.last_preedit) == 0);
    PT_CHECK(strcmp(preedit_of(c), preedit_of(b)) == 0);
    PT_CHECK(strcmp(preedit_of(c), preedit_of(a)) != 0);
    {
        size_t i;
        const size_t count = pathime_context_composition(c)->candidate_count;
        PT_CHECK_SIZE(pathime_context_composition(b)->candidate_count, count);
        for (i = 0; i < count; i++) {
            char from_b[64], from_c[64];
            candidate_at(b, i, from_b, sizeof(from_b));
            candidate_at(c, i, from_c, sizeof(from_c));
            PT_CHECK(strcmp(from_c, from_b) == 0);
        }
    }

    /*
     * Teach one context, and read the other. The phrase is taken from the
     * list rather than named, because api.engine_table's earlier tests have
     * already taught this code and what matters here is that the position
     * *moves*, not what it was.
     */
    candidate_at(c, 2, target, sizeof(target));
    PT_CHECK(target[0] != '\0');
    before = index_of(c, pathime_context_composition(c)->candidate_count, target);
    PT_CHECK_SIZE(before, 2);

    for (round = 0; round < 4; round++) {
        const size_t at = index_of(b, pathime_context_composition(b)->candidate_count,
                                   target);
        PT_CHECK(at != (size_t)-1);
        PT_CHECK_STATUS(pathime_context_select_candidate(b, at), PATHIME_OK);
        PT_CHECK(press(b, 'm'));
        PT_CHECK(press(b, '/'));
        PT_CHECK(press(b, 'm'));
    }

    /* c looks the code up again, against the database b just wrote to. */
    PT_CHECK_STATUS(pathime_context_reset(c), PATHIME_OK);
    PT_CHECK(press(c, 'm'));
    PT_CHECK(press(c, '/'));
    PT_CHECK(press(c, 'm'));
    after = index_of(c, pathime_context_composition(c)->candidate_count, target);
    PT_CHECK(after != (size_t)-1);
    PT_CHECK(after < before);

    /* What b learned is not something c was told about: learning moves through
     * the shared table, not through a callback. */
    PT_CHECK_SIZE(log_c.commit_count, 0);

    pathime_context_destroy(c);
    pathime_context_destroy(b);
    pathime_context_destroy(a);
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

    /*
     * The engine being available is not the same question as this suite having
     * something to type into, and pathime_has_engine() answers the first one
     * deliberately: the table engine reports itself available whenever names
     * can be resolved at all, because a client naming an absolute path needs
     * no shipped tables. A build configured with LIBPATHIME_TABLES="" is
     * exactly that case, and every expectation below is written against the
     * real compiled `cangjie5.db`.
     *
     * So the gate is what the library says it installed. Asking it, rather
     * than assuming the default table set, is also what keeps this honest for
     * a build that ships a different selection.
     */
    if (engine != NULL) {
        pathime_option_info_t tables;
        memset(&tables, 0, sizeof(tables));
        tables.struct_size = sizeof(tables);
        PT_CHECK_STATUS(
            pathime_engine_option_info(engine, PATHIME_OPT_TABLE_FILE, &tables),
            PATHIME_OK);
        if (tables.valid_value_count == 0) {
            pathime_engine_destroy(engine);
            pathime_shutdown();
            return pt_skip("api.engine_table",
                           "this build installed no tables (LIBPATHIME_TABLES is empty)");
        }
    }

    /* Nothing about a table engine needs surrounding text: it never revises
     * text it already committed. */
    PT_CHECK(pathime_engine_requirements(engine) == 0);

    if (engine != NULL) {
        test_compose_and_select(engine);
        test_space_commits_hovered(engine);
        test_punctuation_width(engine);
        test_punctuation_follows_table_variant(engine);
        test_return_commits_literal_keys(engine);
        test_backspace(engine);
        test_backspace_removes_an_invalid_tail(engine);
        test_auto_select_restarts_the_run(engine);
        test_table_declares_options(engine);
        test_z_wildcard(engine);
        test_declarations_available_before_typing(engine);
        test_pinyin_fallback_unsupported(engine);
        test_no_table_is_inert(engine);
        test_second_table(engine);
        test_enumeration(engine);
        test_max_candidates(engine);
        test_engine_cap(engine);
        test_auto_commit(engine);
        test_single_char_only(engine);
        test_commit(engine);
        test_reset_discards(engine);
        test_frequency_augmented_order(engine);
        test_learning(engine);
        test_learning_can_be_declined(engine);
        test_table_without_dynamic_adjust(engine);
        /* Last: it learns, into the same user database test_learning wrote to.
         * Its own assertions are relative for that reason. */
        test_interleaved_contexts(engine);
    }

    pathime_engine_destroy(engine);
    pathime_shutdown();
    return pt_report("api.engine_table");
}

#endif /* PATHIME_WITH_TABLE */
