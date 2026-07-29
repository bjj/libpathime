/*
 * The Chinese engines, driven the way a client drives them: through
 * <pathime/pathime.h> and nothing else.
 *
 * Third of the end-to-end engine tests, after engine_hangul_test.c (no
 * candidates at all) and engine_anthy_test.c (a composing front end plus
 * multi-segment conversion). pyzy is the one that stresses the *candidate*
 * machinery, for two reasons the other two do not have:
 *
 *   - Its candidate enumeration is lazy and mutating. PhoneticContext's
 *     hasCandidate(i) prepares entry i as a side effect, so the library
 *     materializes the whole PATHIME_OPT_MAX_CANDIDATES window before
 *     dispatching composition_changed. That eager pump is what makes
 *     pathime_context_candidate() callback-safe, and this file is the only
 *     place that promise can actually be tested.
 *   - It commits by itself. When a selection exhausts the input pyzy fires
 *     commitText from inside selectCandidate(), which is the case the whole
 *     Output plumbing in the adapter exists to carry.
 *
 * One backend supplies two engine ids: PATHIME_ENGINE_PINYIN and
 * PATHIME_ENGINE_BOPOMOFO. They are distinct ids rather than an option because
 * pyzy fixes the phonetic scheme when its context is created, and the test
 * below shows the two reaching an identical candidate list from different keys.
 *
 * ---------------------------------------------------------------------------
 * Where the expected values came from
 * ---------------------------------------------------------------------------
 *
 * Real Chinese, observed by running this stack against the android.db this
 * build produces — not predicted. Written as UTF-8 escapes with the character
 * named in a comment, as engine_hangul_test.c does, so the file holds no
 * non-ASCII bytes and needs no /utf-8 from MSVC to mean what it says.
 *
 * The expectations are deliberately several candidates deep. With no database
 * open pyzy does not fail — Database::init() builds its singleton whether or
 * not open() succeeded, so the warning goes to stderr and the engine reports
 * success — it simply produces nothing. A test that only checked "some
 * candidates came back" would pass vacuously against a broken build tree;
 * naming five of them cannot.
 */

#include <string.h>

#include "api_test_util.h"

#if !PATHIME_WITH_PYZY

int main(void)
{
    return pt_skip("api.engine_pyzy", "this build does not contain pyzy");
}

#elif !defined(PYZY_TEST_DATA_DIR)

/*
 * The library is here but its database is not: the port only builds android.db
 * when it can find a Python 3 interpreter at configure time, and without it
 * every assertion below would fail for a reason that has nothing to do with
 * libpathime. tests/api/CMakeLists.txt withholds PYZY_TEST_DATA_DIR to say so.
 */
int main(void)
{
    return pt_skip("api.engine_pyzy", "pyzy's android.db was not built");
}

#else

/* ---- The candidates "nihao" produces, in the order pyzy ranks them ------- */

#define NI_HAO "\xE4\xBD\xA0\xE5\xA5\xBD"  /* 你好 U+4F60 U+597D — "hello" */
#define LI_HAO "\xE5\x88\xA9\xE5\xA5\xBD"  /* 利好 U+5229 U+597D */
#define NI     "\xE4\xBD\xA0"              /* 你 U+4F60 — "you" */
#define LI_1   "\xE9\x87\x8C"              /* 里 U+91CC */
#define LI_2   "\xE6\x9D\x8E"              /* 李 U+674E */
#define LI_3   "\xE7\xA6\xBB"              /* 离 U+79BB — simplified */
#define LI_3_T "\xE9\x9B\xA2"              /* 離 U+96E2 — its traditional form */

/* ---- What remains once 你 is settled ------------------------------------ */

#define HAO   "\xE5\xA5\xBD"  /* 好 U+597D */
#define HAO_2 "\xE5\x8F\xB7"  /* 号 U+53F7 */
#define HAO_3 "\xE6\xB5\xA9"  /* 浩 U+6D69 */

/* The preedit for a full-pinyin composition: the syllables pyzy decoded, with
 * the separators it writes between them kept and its input-cursor '|'
 * stripped. This is what the user typed, in the script they are composing in —
 * the rule at pathime_composition_t::preedit — and 你好 is candidate 0 rather
 * than preedit text. */
#define PINYIN_NI_HAO "ni hao"

/* What remains in the preedit once 你 has been settled out of "nihao": the
 * chosen character, then the input it did not cover. pyzy's auxiliary text
 * shrinks to match, which is what makes this projection possible at all. */
#define PINYIN_NI_HAO_SETTLED NI "hao"

/* ---- What the width and punctuation layer emits ------------------------- */

#define COMMA_FW    "\xEF\xBC\x8C"  /* ，U+FF0C — the Chinese comma */
#define PERIOD_FW   "\xE3\x80\x82"  /* 。U+3002 — the ideographic full stop */
#define BRACKET_S_L "\xE3\x80\x90"  /* 【U+3010 — simplified */
#define BRACKET_T_L "\xE3\x80\x8C"  /* 「U+300C — traditional */
#define QUOTE_OPEN  "\xE2\x80\x9C"  /* “ U+201C */
#define QUOTE_CLOSE "\xE2\x80\x9D"  /* ” U+201D */
#define AT_FW       "\xEF\xBC\xA0"  /* ＠U+FF20 — no Chinese form, so width only */
#define ONE_FW      "\xEF\xBC\x91"  /* １U+FF11 */
#define SPACE_FW    "\xE3\x80\x80"  /* 　U+3000 — the ideographic space */

/*
 * What the client saw. Same shape as the other two engine tests', plus one
 * extra field: a context handle, so composition_changed can exercise the
 * callback-safe query set from inside the callback.
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

    /*
     * Set once the context exists. When non-NULL, composition_changed walks
     * the whole candidate list from inside the callback — see
     * test_callback_safety() for what that is proving.
     */
    pathime_context_t *ctx;
    int probed_lists;      /* how many lists were walked that way */
    int probe_failures;    /* fetches that failed inside a callback */
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

    /*
     * The callback-safety probe. pathime_context_candidate() is documented as
     * callback-safe *because* the library materializes every candidate the cap
     * allows before dispatching this callback — so here it is a plain array
     * read and cannot re-enter pyzy's lazy, mutating hasCandidate(). If the
     * pump were lazy instead, this loop is where it would show: it runs while
     * the context is mid-mutation.
     */
    if (log->ctx != NULL && composition->candidate_count > 0) {
        size_t i;
        log->probed_lists++;
        for (i = 0; i < composition->candidate_count; i++) {
            pathime_str_t cand;
            if (pathime_context_candidate(log->ctx, i, &cand) != PATHIME_OK ||
                cand.len == 0 || cand.bytes[cand.len] != '\0') {
                log->probe_failures++;
            }
        }
    }
}

static void log_reset(client_log_t *log)
{
    pathime_context_t *ctx = log->ctx;
    memset(log, 0, sizeof(*log));
    log->ctx = ctx;
}

/* Press one printable US-QWERTY key. pyzy has no notion of key position, so
 * layout_key is never consulted; a client supplies it anyway. */
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

/* Type a spelling one key at a time; every key must be accepted. */
static void type(pathime_context_t *ctx, const char *keys)
{
    for (; *keys != '\0'; keys++) {
        PT_CHECK(press(ctx, (uint32_t)(unsigned char)*keys));
    }
}

static const char *preedit_of(pathime_context_t *ctx)
{
    return pathime_context_composition(ctx)->preedit.bytes;
}

static void check_str(const char *what, const char *got, const char *want)
{
    pt_checks++;
    if (strcmp(got, want) != 0) {
        PT_FAILF("%s: got \"%s\", expected \"%s\"", what, got, want);
    }
}

static const char *candidate_of(pathime_context_t *ctx, size_t index)
{
    pathime_str_t cand;
    if (pathime_context_candidate(ctx, index, &cand) != PATHIME_OK) return "";
    return cand.bytes;
}

/* A context with the standard callback table. */
static pathime_context_t *open_context(pathime_engine_t *engine,
                                       pathime_client_t *client,
                                       client_log_t *log)
{
    pathime_context_t *ctx = NULL;

    memset(log, 0, sizeof(*log));
    memset(client, 0, sizeof(*client));
    client->struct_size = sizeof(*client);
    client->commit_text = on_commit;
    client->delete_surrounding_text = on_delete;
    client->composition_changed = on_changed;

    PT_CHECK_STATUS(pathime_context_create(engine, client, log, &ctx), PATHIME_OK);
    PT_CHECK(ctx != NULL);
    return ctx;
}

/* Copy the first @a count candidates out, for comparison after a change. */
static void snapshot(pathime_context_t *ctx, size_t count, char list[][32])
{
    size_t i;
    for (i = 0; i < count; i++) {
        const char *text = candidate_of(ctx, i);
        PT_CHECK(strlen(text) < 32);
        strncpy(list[i], text, 31);
        list[i][31] = '\0';
    }
}

/* ---------------------------------------------------------------------- */

/*
 * Full pinyin: "nihao" gives a preedit and a real candidate list. Five entries
 * are named rather than one, because one would still pass against a build tree
 * with no database in it.
 */
static void test_pinyin_composition(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = open_context(engine, &client, &log);
    const pathime_composition_t *c = NULL;
    size_t i;

    type(ctx, "nihao");

    c = pathime_context_composition(ctx);
    /*
     * The pinyin, not 你好. The engine's guess is candidate 0, checked just
     * below; the preedit is what was typed. This is the assertion that pins
     * the preedit rule for Chinese — before it, this read NI_HAO and Return
     * committed "nihao", which is the divergence the rule removed.
     */
    check_str("nihao preedit", c->preedit.bytes, PINYIN_NI_HAO);
    PT_CHECK_SIZE(c->preedit.len, 6);
    /* Nothing settled: the whole input is one active span until a selection
     * settles part of it. */
    PT_CHECK_SIZE(c->preedit_settled, 0);

    PT_CHECK(c->candidate_count > 0);
    check_str("candidate 0", candidate_of(ctx, 0), NI_HAO);
    check_str("candidate 1", candidate_of(ctx, 1), LI_HAO);
    check_str("candidate 2", candidate_of(ctx, 2), NI);
    check_str("candidate 3", candidate_of(ctx, 3), LI_1);
    check_str("candidate 4", candidate_of(ctx, 4), LI_2);

    /* Every advertised index is fetchable and non-empty, over the whole
     * range — not merely the entries this test names. */
    for (i = 0; i < c->candidate_count; i++) {
        pathime_str_t cand;
        PT_CHECK_STATUS(pathime_context_candidate(ctx, i, &cand), PATHIME_OK);
        PT_CHECK(cand.len > 0);
        PT_CHECK(cand.bytes[cand.len] == '\0');
    }
    {
        pathime_str_t cand;
        PT_CHECK_STATUS(pathime_context_candidate(ctx, c->candidate_count, &cand),
                        PATHIME_ERROR_INVALID_ARGUMENT);
        PT_CHECK_STATUS(pathime_context_select_candidate(ctx, c->candidate_count),
                        PATHIME_ERROR_INVALID_ARGUMENT);
    }

    /* Escape discards without committing; with nothing composing the key goes
     * back to the client, whose dialog it must still close. */
    log_reset(&log);
    PT_CHECK(press(ctx, PATHIME_KEY_ESCAPE));
    check_str("preedit after escape", preedit_of(ctx), "");
    PT_CHECK(log.commit_count == 0);
    PT_CHECK(!press(ctx, PATHIME_KEY_ESCAPE));

    /*
     * Return means "give me what I typed" and Space means "convert it" — the
     * pinyin convention, and the difference is visible in the committed text.
     */
    type(ctx, "nihao");
    log_reset(&log);
    PT_CHECK(press(ctx, PATHIME_KEY_RETURN));
    check_str("return commits the raw input", log.commits, "nihao");

    type(ctx, "nihao");
    log_reset(&log);
    PT_CHECK(press(ctx, ' '));
    check_str("space commits the conversion", log.commits, NI_HAO);
    check_str("callback order across a commit", log.order, "cx");

    pathime_context_destroy(ctx);
}

/*
 * The settled boundary, and the automatic commit.
 *
 * Selecting 你 — candidate 2, the one covering only the first syllable —
 * settles it and leaves "hao" active with a fresh list for the remaining
 * input. That is greedy resolution advancing across a Chinese composition
 * exactly as it does across a Japanese one, and it is where
 * preedit_settled has to be a scalar count: 你 is one character and three
 * bytes, so a byte count would read 3 here and be indistinguishable from a
 * three-character prefix to any client that trusted it.
 */
static void test_settled_boundary_and_auto_commit(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = open_context(engine, &client, &log);
    const pathime_composition_t *c = NULL;
    char before[8][32];
    size_t i;

    type(ctx, "nihao");
    snapshot(ctx, 5, before);

    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_select_candidate(ctx, 2), PATHIME_OK);

    c = pathime_context_composition(ctx);
    /* One scalar settled. Not 3, which is what 你's byte length would give. */
    PT_CHECK_SIZE(c->preedit_settled, 1);
    /* 你 is three bytes, "hao" is three more. */
    PT_CHECK_SIZE(c->preedit.len, 6);
    /*
     * The chosen character, then the input it did not cover — settling is not
     * committing, and what has not been settled is still shown as typed.
     * This is the same shape anthy produces mid-sentence, which is the whole
     * point of the preedit rule.
     */
    check_str("preedit after settling 你", c->preedit.bytes, PINYIN_NI_HAO_SETTLED);

    /* Nothing was committed, and the client was told once. */
    PT_CHECK(log.commit_count == 0);
    PT_CHECK(log.changed_count == 1);
    PT_CHECK_SIZE(log.last_settled, 1);

    /* A fresh list arrived, describing the new active span and nothing else.
     * Positions in the previous list are obsolete — which is why the header
     * says so in composition_changed's own documentation. */
    PT_CHECK(c->candidate_count > 0);
    check_str("new candidate 0", candidate_of(ctx, 0), HAO);
    check_str("new candidate 1", candidate_of(ctx, 1), HAO_2);
    check_str("new candidate 2", candidate_of(ctx, 2), HAO_3);
    for (i = 0; i < 3; i++) {
        PT_CHECK(strcmp(candidate_of(ctx, i), before[i]) != 0);
    }

    /*
     * Return here would commit "你hao" — the settled character plus the input
     * it did not cover, which is exactly the preedit above. Checked on its own
     * context so the automatic-commit case below still starts from here.
     */
    {
        client_log_t end_log;
        pathime_client_t end_client;
        pathime_context_t *end_ctx = open_context(engine, &end_client, &end_log);
        type(end_ctx, "nihao");
        PT_CHECK_STATUS(pathime_context_select_candidate(end_ctx, 2), PATHIME_OK);
        check_str("preedit before Return", preedit_of(end_ctx), PINYIN_NI_HAO_SETTLED);
        PT_CHECK(press(end_ctx, PATHIME_KEY_RETURN));
        PT_CHECK(end_log.commit_count == 1);
        check_str("Return commits the half-settled preedit",
                  end_log.commits, PINYIN_NI_HAO_SETTLED);
        pathime_context_destroy(end_ctx);
    }

    /*
     * Selecting again exhausts the input, and pyzy commits by itself from
     * inside selectCandidate(). Nothing in the API asked for a commit here —
     * this is the engine deciding the composition is finished, and the case
     * the adapter's Output plumbing exists to carry back out.
     */
    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_select_candidate(ctx, 0), PATHIME_OK);
    PT_CHECK(log.commit_count == 1);
    check_str("automatic commit", log.commits, NI_HAO);
    check_str("callback order across the automatic commit", log.order, "cx");

    c = pathime_context_composition(ctx);
    check_str("preedit after the automatic commit", c->preedit.bytes, "");
    PT_CHECK_SIZE(c->preedit_settled, 0);
    PT_CHECK_SIZE(c->candidate_count, 0);

    /* With nothing composing there is no span to select from. */
    PT_CHECK_STATUS(pathime_context_select_candidate(ctx, 0),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    pathime_context_destroy(ctx);
}

/*
 * Eager materialization, which is the obligation the whole candidates.cc design
 * rests on. pyzy enumerates lazily, so raising PATHIME_OPT_MAX_CANDIDATES has
 * to *append* — a client that already handed index 4 to its user must still see
 * the same candidate there after the user scrolls. A lazy implementation, or
 * one that rebuilt the list from scratch, would show up here as renumbering.
 */
static void test_eager_materialization(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = open_context(engine, &client, &log);
    const pathime_composition_t *c = NULL;
    char at_5[8][32];
    char at_12[16][32];
    size_t i;

    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_MAX_CANDIDATES, 5),
                    PATHIME_OK);
    type(ctx, "nihao");

    c = pathime_context_composition(ctx);
    PT_CHECK_SIZE(c->candidate_count, 5);
    snapshot(ctx, 5, at_5);
    check_str("capped list still starts at the best", at_5[0], NI_HAO);

    /* Raise it: more candidates appear, and the first five are untouched. */
    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_MAX_CANDIDATES, 12),
                    PATHIME_OK);
    c = pathime_context_composition(ctx);
    PT_CHECK_SIZE(c->candidate_count, 12);
    PT_CHECK(log.changed_count == 1);
    PT_CHECK_SIZE(log.last_candidates, 12);
    for (i = 0; i < 5; i++) {
        check_str("candidate kept its position at 12", candidate_of(ctx, i), at_5[i]);
    }
    snapshot(ctx, 12, at_12);
    check_str("candidate 5 appeared", at_12[5], LI_3);

    /* Again, further. Still appending. */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_MAX_CANDIDATES, 40),
                    PATHIME_OK);
    c = pathime_context_composition(ctx);
    PT_CHECK(c->candidate_count > 12);
    for (i = 0; i < 12; i++) {
        check_str("candidate kept its position at 40", candidate_of(ctx, i), at_12[i]);
    }

    /* Lowering removes from the tail and leaves the head alone. */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_MAX_CANDIDATES, 5),
                    PATHIME_OK);
    c = pathime_context_composition(ctx);
    PT_CHECK_SIZE(c->candidate_count, 5);
    for (i = 0; i < 5; i++) {
        check_str("candidate survived the lowering", candidate_of(ctx, i), at_5[i]);
    }

    /* The preedit is untouched throughout: changing the cap is not a
     * conversion event. */
    check_str("preedit survives the cap changes", preedit_of(ctx), PINYIN_NI_HAO);

    /* Zero is rejected rather than treated as "hide the list": an engine that
     * converts by selection cannot make progress without a candidate. */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_MAX_CANDIDATES, 0),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    pathime_context_destroy(ctx);
}

/*
 * The candidate cursor on pyzy — and specifically that the preedit does *not*
 * follow it.
 *
 * pyzy tracks a focused candidate and rewrites its conversionText() to match
 * (PhoneticContext::focusCandidate()), but conversionText() is not the
 * preedit: the preedit is what the user typed, and 你好 is candidate 0.
 *
 * That difference from anthy is the preedit rule doing its job rather than an
 * inconsistency. On anthy the cursor moves *within a conversion the user asked
 * for* by pressing Space, so choosing among conversions rewrites the preedit.
 * Here the candidates arrived unbidden from the first keystroke, so moving the
 * cursor is browsing and settles nothing — and the invariant both engines keep
 * is the one a client relies on: ending the composition commits what is shown.
 * The Return check at the end is what pins that.
 */
static void test_candidate_cursor(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = open_context(engine, &client, &log);
    const pathime_composition_t *c = NULL;
    char first[32];
    char second[32];

    /* No composition, so no list and nothing to hover. */
    PT_CHECK_SIZE(pathime_context_composition(ctx)->candidate_cursor, 0);
    PT_CHECK_STATUS(pathime_context_set_candidate_cursor(ctx, 0),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    type(ctx, "nihao");
    c = pathime_context_composition(ctx);
    PT_CHECK(c->candidate_count > 1);

    snprintf(first, sizeof(first), "%s", candidate_of(ctx, 0));
    snprintf(second, sizeof(second), "%s", candidate_of(ctx, 1));
    PT_CHECK(strcmp(first, second) != 0);

    /* The cursor starts at the head, and the preedit is the pinyin. */
    PT_CHECK_SIZE(pathime_context_composition(ctx)->candidate_cursor, 0);
    check_str("preedit at cursor 0", preedit_of(ctx), PINYIN_NI_HAO);

    /* Move it: the cursor moves and the client is told once. */
    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_set_candidate_cursor(ctx, 1), PATHIME_OK);
    PT_CHECK_SIZE(pathime_context_composition(ctx)->candidate_cursor, 1);
    /* The preedit does not follow. Nothing has been chosen. */
    check_str("preedit does not follow cursor", preedit_of(ctx), PINYIN_NI_HAO);
    PT_CHECK(log.changed_count == 1);
    /* Hovering settles and commits nothing. */
    PT_CHECK(log.commit_count == 0);
    PT_CHECK_SIZE(pathime_context_composition(ctx)->preedit_settled, 0);

    /* The list itself is unchanged — hovering within a list does not replace
     * it, so positions the client already handed out still mean what they
     * did. */
    c = pathime_context_composition(ctx);
    check_str("candidate 0 unmoved", candidate_of(ctx, 0), first);
    check_str("candidate 1 unmoved", candidate_of(ctx, 1), second);

    /* Reversible, unlike selection. */
    PT_CHECK_STATUS(pathime_context_set_candidate_cursor(ctx, 0), PATHIME_OK);
    PT_CHECK_SIZE(pathime_context_composition(ctx)->candidate_cursor, 0);

    /* Out of range leaves everything alone. */
    PT_CHECK_STATUS(pathime_context_set_candidate_cursor(ctx, c->candidate_count),
                    PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_SIZE(pathime_context_composition(ctx)->candidate_cursor, 0);
    check_str("failed move changes nothing", preedit_of(ctx), PINYIN_NI_HAO);

    /*
     * The cursor is always a position in the current list, so lowering the cap
     * out from under a cursor near the end must still leave one a client can
     * draw with.
     *
     * Worth knowing what this does and does not pin down: on pyzy the answer
     * comes from the observer resetting the hover when its candidate list is
     * regenerated, not from the clamp in materialize_candidates() — both were
     * removed and this still passed. It is a check that the invariant holds
     * here, not that the clamp is what holds it. api.engine_anthy carries the
     * version that tests the clamp, because anthy takes the default no-op
     * options_changed() and nothing else volunteers.
     */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_MAX_CANDIDATES, 8),
                    PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_candidate_cursor(ctx, 6), PATHIME_OK);
    PT_CHECK_SIZE(pathime_context_composition(ctx)->candidate_cursor, 6);

    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_MAX_CANDIDATES, 3),
                    PATHIME_OK);
    c = pathime_context_composition(ctx);
    PT_CHECK_SIZE(c->candidate_count, 3);
    PT_CHECK(c->candidate_cursor < c->candidate_count);
    PT_CHECK_STATUS(pathime_context_reset_option(ctx, PATHIME_OPT_MAX_CANDIDATES),
                    PATHIME_OK);

    /* Selecting settles the span, so the list and the cursor both start over
     * for whatever is left. */
    PT_CHECK_STATUS(pathime_context_set_candidate_cursor(ctx, 1), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_select_candidate(ctx, 1), PATHIME_OK);
    PT_CHECK_SIZE(pathime_context_composition(ctx)->candidate_cursor, 0);

    /*
     * The invariant the whole preedit rule exists for, checked where it is
     * most likely to break: hover a candidate, then end the composition, and
     * what is committed is the preedit that was on screen — not the candidate
     * under the cursor, which was never chosen.
     *
     * Mutation-tested: restoring conversionText() as the active span makes the
     * preedit check above fail, and makes this commit disagree with the last
     * preedit the client saw, which is exactly the divergence the header used
     * to have to warn about.
     */
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);
    type(ctx, "nihao");
    PT_CHECK_STATUS(pathime_context_set_candidate_cursor(ctx, 1), PATHIME_OK);
    check_str("preedit before Return", preedit_of(ctx), PINYIN_NI_HAO);
    log_reset(&log);
    PT_CHECK(press(ctx, PATHIME_KEY_RETURN));
    PT_CHECK(log.commit_count == 1);
    check_str("Return commits the preedit that was shown", log.commits, "nihao");

    pathime_context_destroy(ctx);
}

/*
 * Callback safety in practice.
 *
 * The header lists pathime_context_candidate() as one of the functions a client
 * may call from inside a callback, and gives the reason: every candidate the
 * cap allows is materialized before composition_changed is dispatched, so the
 * call is a plain array read. That is a promise about pyzy specifically —
 * hasCandidate(i) is lazy *and* mutating — and this is the only place in the
 * suite where it can be checked, because it can only be checked from inside a
 * callback.
 */
static void test_callback_safety(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = open_context(engine, &client, &log);

    /* Arming the probe: on_changed now walks the whole list on every dispatch. */
    log.ctx = ctx;

    type(ctx, "nihao");
    PT_CHECK(log.probed_lists >= 5);      /* one per key, at least */
    PT_CHECK_SIZE(log.probe_failures, 0);

    /* Again across a selection, where the list is replaced wholesale rather
     * than extended — the dispatch most likely to hand out a stale array. */
    PT_CHECK_STATUS(pathime_context_select_candidate(ctx, 2), PATHIME_OK);
    PT_CHECK_SIZE(log.probe_failures, 0);

    /* And across a cap change, which is the one mutation that lengthens the
     * list without any key having been pressed. */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_MAX_CANDIDATES, 30),
                    PATHIME_OK);
    PT_CHECK_SIZE(log.probe_failures, 0);
    PT_CHECK(log.probed_lists >= 7);

    /* The other callback-safe queries answer sensibly from in there too — they
     * are what a language binding reaches for when handed a bare handle. */
    PT_CHECK(pathime_context_engine(ctx) == engine);
    PT_CHECK(pathime_context_user_data(ctx) == &log);

    log.ctx = NULL;
    pathime_context_destroy(ctx);
}

/*
 * Bopomofo: a different engine id, different keys, the same backend and the
 * same answer. "su3cl3" is ㄋㄧˇ ㄏㄠˇ — ni hao with both third tones — on the
 * standard zhuyin keyboard, and it has to reach exactly the candidate list
 * full pinyin's "nihao" reaches, because pyzy parses zhuyin into pinyin before
 * querying the database.
 */
static void test_bopomofo(pathime_engine_t *pinyin, pathime_engine_t *bopomofo)
{
    client_log_t pinyin_log, bopo_log;
    pathime_client_t pinyin_client, bopo_client;
    pathime_context_t *pinyin_ctx = open_context(pinyin, &pinyin_client, &pinyin_log);
    pathime_context_t *bopo_ctx = open_context(bopomofo, &bopo_client, &bopo_log);
    const pathime_composition_t *pc = NULL;
    const pathime_composition_t *bc = NULL;
    char expected[16][32];
    size_t i;

    type(pinyin_ctx, "nihao");
    pc = pathime_context_composition(pinyin_ctx);
    PT_CHECK(pc->candidate_count >= 12);
    snapshot(pinyin_ctx, 12, expected);

    type(bopo_ctx, "su3cl3");
    bc = pathime_context_composition(bopo_ctx);
    /*
     * The zhuyin, not the latin keys and not 你好 — the case that makes the
     * preedit rule obviously right rather than merely consistent. A zhuyin
     * typist composes in zhuyin; before the rule this string was reachable
     * only through the auxiliary text while the preedit showed 你好, which is
     * the wrong way round. pyzy's own ',' between syllables is kept, the way
     * full pinyin's ' ' is; its trailing '|' input cursor is stripped.
     */
    check_str("bopomofo preedit",
              bc->preedit.bytes,
              /* ㄋㄧˇ,ㄏㄠˇ — U+310B U+3127 U+02C7 , U+310F U+3120 U+02C7 */
              "\xE3\x84\x8B\xE3\x84\xA7\xCB\x87,\xE3\x84\x8F\xE3\x84\xA0\xCB\x87");

    /* And the preedit is what Return would commit, minus the separator. */
    {
        client_log_t end_log;
        pathime_client_t end_client;
        pathime_context_t *end_ctx = open_context(bopomofo, &end_client, &end_log);
        type(end_ctx, "su3cl3");
        PT_CHECK(press(end_ctx, PATHIME_KEY_RETURN));
        check_str("bopomofo Return commits the zhuyin",
                  end_log.commits,
                  /* ㄋㄧˇㄏㄠˇ */
                  "\xE3\x84\x8B\xE3\x84\xA7\xCB\x87\xE3\x84\x8F\xE3\x84\xA0\xCB\x87");
        pathime_context_destroy(end_ctx);
    }

    PT_CHECK_SIZE(bc->candidate_count, pc->candidate_count);
    for (i = 0; i < 12; i++) {
        check_str("bopomofo reaches the same candidate",
                  candidate_of(bopo_ctx, i), expected[i]);
    }

    /* Selecting behaves identically, down to the automatic commit. */
    log_reset(&bopo_log);
    PT_CHECK_STATUS(pathime_context_select_candidate(bopo_ctx, 0), PATHIME_OK);
    PT_CHECK(bopo_log.commit_count == 1);
    check_str("bopomofo commit", bopo_log.commits, NI_HAO);

    pathime_context_destroy(bopo_ctx);
    pathime_context_destroy(pinyin_ctx);
}

/*
 * Options. Each of the three below is implemented at a different level: the
 * Chinese variant is a pyzy property set on every mutating call, the pinyin
 * scheme is a context rebuild, and the fuzzy flags are a table translation
 * whose *engine set* was the thing recently corrected.
 */
static void test_options(pathime_engine_t *pinyin, pathime_engine_t *bopomofo)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = NULL;
    pathime_option_info_t info;
    int64_t value = -1;

    /* --- PATHIME_OPT_CHINESE_VARIANT ------------------------------------ */

    /*
     * pyzy models this as one simplified-or-traditional flag with no mixed
     * mode, so these two engines accept only the two exclusive values while the
     * table engine accepts all five. That narrowing is reported through
     * valid_values rather than hidden, so a client can present exactly the
     * choices that will work — and this is the assertion that the reported set
     * is the set the setters actually enforce.
     */
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    PT_CHECK_STATUS(pathime_engine_option_info(pinyin, PATHIME_OPT_CHINESE_VARIANT, &info),
                    PATHIME_OK);
    PT_CHECK(info.supported);
    PT_CHECK(info.type == PATHIME_OPTION_ENUM);
    PT_CHECK(info.valid_values ==
             ((UINT64_C(1) << PATHIME_CHINESE_SIMPLIFIED_ONLY) |
              (UINT64_C(1) << PATHIME_CHINESE_TRADITIONAL_ONLY)));
    PT_CHECK(info.default_value == PATHIME_CHINESE_SIMPLIFIED_ONLY);
    /* Changing the repertoire does not invalidate what has been typed. */
    PT_CHECK(!info.resets_composition);

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    PT_CHECK_STATUS(pathime_engine_option_info(bopomofo, PATHIME_OPT_CHINESE_VARIANT, &info),
                    PATHIME_OK);
    PT_CHECK(info.valid_values ==
             ((UINT64_C(1) << PATHIME_CHINESE_SIMPLIFIED_ONLY) |
              (UINT64_C(1) << PATHIME_CHINESE_TRADITIONAL_ONLY)));

    ctx = open_context(pinyin, &client, &log);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_CHINESE_VARIANT,
                                                   PATHIME_CHINESE_SIMPLIFIED_ONLY),
                    PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_CHINESE_VARIANT,
                                                   PATHIME_CHINESE_TRADITIONAL_ONLY),
                    PATHIME_OK);
    /* The three non-exclusive values are rejected, not silently coerced. */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_CHINESE_VARIANT,
                                                   PATHIME_CHINESE_SIMPLIFIED_FIRST),
                    PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_CHINESE_VARIANT,
                                                   PATHIME_CHINESE_TRADITIONAL_FIRST),
                    PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_CHINESE_VARIANT,
                                                   PATHIME_CHINESE_ANY),
                    PATHIME_ERROR_INVALID_ARGUMENT);
    /* A rejected set changes nothing: traditional is still in force. */
    PT_CHECK_STATUS(pathime_context_get_option_int(ctx, PATHIME_OPT_CHINESE_VARIANT,
                                                   &value),
                    PATHIME_OK);
    PT_CHECK(value == PATHIME_CHINESE_TRADITIONAL_ONLY);

    /*
     * And it really changes the candidates. The head of the list is the same
     * in both repertoires — 你好 has no distinct traditional form — so the
     * check has to reach the first entry that does, which is 离 / 離.
     */
    type(ctx, "nihao");
    check_str("traditional preedit", preedit_of(ctx), PINYIN_NI_HAO);
    check_str("traditional candidate 0", candidate_of(ctx, 0), NI_HAO);
    check_str("traditional candidate 5", candidate_of(ctx, 5), LI_3_T);

    /*
     * A change takes effect immediately. The header states that as a rule, and
     * this option — not one of the four marked resets_composition — has to obey
     * it without disturbing the composition in progress.
     *
     * Worth asserting rather than assuming, because the way it fails is quiet:
     * the store updates, the getters report the new value, composition_changed
     * is dispatched, and the list on screen stays the old repertoire's until
     * the user types one more key. Two things keep that from happening. The
     * core tells the backend when a resolved value has moved — options are
     * pulled, but pulling next time is too late when there may be no next time
     * — which is ContextBackend::options_changed(). And pyzy's setProperty()
     * stores without regenerating, firing no candidatesChanged, so the adapter
     * drops its materialized list and lets the core's pump refill it.
     *
     * Index 5 must therefore flip from the traditional 離 to the simplified 离
     * with no key pressed in between.
     */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_CHINESE_VARIANT,
                                                   PATHIME_CHINESE_SIMPLIFIED_ONLY),
                    PATHIME_OK);
    check_str("variant change felt with no further key", candidate_of(ctx, 5), LI_3);

    /* And it stays in force across the next keystroke, which is the check that
     * the list was genuinely regenerated and not merely reordered once. */
    PT_CHECK(press(ctx, PATHIME_KEY_BACKSPACE));
    PT_CHECK(press(ctx, 'o'));
    check_str("simplified preedit", preedit_of(ctx), PINYIN_NI_HAO);
    check_str("simplified candidate 5", candidate_of(ctx, 5), LI_3);
    pathime_context_destroy(ctx);

    /* Set before anything is typed, the option behaves the way a client would
     * expect it to throughout — which is the ordinary case and the one worth
     * pinning independently of the gap above. */
    ctx = open_context(pinyin, &client, &log);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_CHINESE_VARIANT,
                                                   PATHIME_CHINESE_TRADITIONAL_ONLY),
                    PATHIME_OK);
    type(ctx, "nihao");
    check_str("traditional from the start, candidate 5", candidate_of(ctx, 5), LI_3_T);
    pathime_context_destroy(ctx);

    /* --- PATHIME_OPT_PINYIN_SCHEME -------------------------------------- */

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    PT_CHECK_STATUS(pathime_engine_option_info(pinyin, PATHIME_OPT_PINYIN_SCHEME, &info),
                    PATHIME_OK);
    PT_CHECK(info.supported);
    /* Declared as data, not left for a client to discover: pyzy fixes the
     * scheme when its context is created, so changing it rebuilds that context
     * and whatever was typed goes with it. */
    PT_CHECK(info.resets_composition);

    ctx = open_context(pinyin, &client, &log);
    type(ctx, "nihao");
    check_str("composition before the scheme change", preedit_of(ctx), PINYIN_NI_HAO);

    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_PINYIN_SCHEME,
                                                   PATHIME_PINYIN_SCHEME_DOUBLE_MSPY),
                    PATHIME_OK);
    /* Really discarded — not preserved, and not committed either. */
    check_str("composition after the scheme change", preedit_of(ctx), "");
    PT_CHECK_SIZE(pathime_context_composition(ctx)->candidate_count, 0);
    PT_CHECK(log.commit_count == 0);
    PT_CHECK(log.changed_count == 1);

    /* The rebuilt context is a working one, not a wedged one: the scheme change
     * replaced pyzy's InputContext, and the next key has to reach the new one. */
    PT_CHECK(press(ctx, 'n'));
    PT_CHECK(pathime_context_composition(ctx)->candidate_count > 0);
    pathime_context_destroy(ctx);

    /* The scheme is not a Bopomofo option: that engine's phonetic type comes
     * from its id, so offering the pinyin schemes there would be a control that
     * does nothing, and the setter says so rather than accepting it. */
    ctx = open_context(bopomofo, &client, &log);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_PINYIN_SCHEME,
                                                   PATHIME_PINYIN_SCHEME_DOUBLE_MSPY),
                    PATHIME_ERROR_UNSUPPORTED);
    /* Its own layout option is the one that resets, for the mirror-image
     * reason: pyzy stores a new zhuyin arrangement without re-reading the keys
     * already typed. */
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    PT_CHECK_STATUS(pathime_engine_option_info(bopomofo, PATHIME_OPT_BOPOMOFO_LAYOUT, &info),
                    PATHIME_OK);
    PT_CHECK(info.supported);
    PT_CHECK(info.resets_composition);
    pathime_context_destroy(ctx);

    /* --- PATHIME_OPT_PINYIN_FUZZY --------------------------------------- */

    /*
     * Recently widened from Pinyin alone to both pyzy engines. The claim was
     * measured rather than reasoned: 61 rows of pyzy's bopomofo_table carry a
     * PINYIN_FUZZY_* bit, and check_flags() makes parseBopomofo stop at a
     * syllable whose bit is clear — so the rules really are reachable from
     * zhuyin, which is parsed into pinyin before it is looked up. This is the
     * assertion that the descriptor says so.
     */
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    PT_CHECK_STATUS(pathime_engine_option_info(pinyin, PATHIME_OPT_PINYIN_FUZZY, &info),
                    PATHIME_OK);
    PT_CHECK(info.supported);
    PT_CHECK(info.type == PATHIME_OPTION_FLAGS);

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    PT_CHECK_STATUS(pathime_engine_option_info(bopomofo, PATHIME_OPT_PINYIN_FUZZY, &info),
                    PATHIME_OK);
    PT_CHECK(info.supported);
    PT_CHECK(info.type == PATHIME_OPTION_FLAGS);
    /* Every bit, on both, and the same default. */
    PT_CHECK(info.valid_values == ((UINT64_C(1) << 20) - 1));
    PT_CHECK((uint64_t)info.default_value == info.valid_values);

    /* Settable on a bopomofo context, which is the behavioural half. */
    ctx = open_context(bopomofo, &client, &log);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_PINYIN_FUZZY, 0),
                    PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_PINYIN_FUZZY,
                                                   PATHIME_PINYIN_FUZZY_F_H),
                    PATHIME_OK);
    /* An undefined bit is rejected rather than dropped: silently ignoring one
     * would let a client believe a rule is in force that is not. */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_PINYIN_FUZZY,
                                                   UINT64_C(1) << 20),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    /*
     * PATHIME_OPT_PINYIN_CORRECTION did *not* widen with it, and the contrast
     * is the point: zero rows of bopomofo_table reach an entry carrying a
     * PINYIN_CORRECT_* bit, because corrections are latin typing slips and
     * there is no zhuyin spelling to slip in.
     */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_PINYIN_CORRECTION, 0),
                    PATHIME_ERROR_UNSUPPORTED);
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    PT_CHECK_STATUS(pathime_engine_option_info(bopomofo, PATHIME_OPT_PINYIN_CORRECTION,
                                               &info),
                    PATHIME_OK);
    PT_CHECK(!info.supported);
    pathime_context_destroy(ctx);
}

/*
 * PATHIME_OPT_LATIN_WIDTH and PATHIME_OPT_PUNCTUATION_WIDTH: the keys pyzy
 * will not take, which these engines emit themselves.
 *
 * The subject is a whole *class* of key rather than a feature — every
 * printable ASCII character that is not a pinyin letter goes through the same
 * path — so the checks are grouped by the distinction each one is drawing,
 * and each names the distinction rather than just the expected string.
 */
static void test_width_and_punctuation(pathime_engine_t *pinyin,
                                       pathime_engine_t *bopomofo)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx;

    /* --- The defaults: half-width Latin, full-width punctuation --------- */

    ctx = open_context(pinyin, &client, &log);

    /*
     * Handled, not declined. That is the substantive claim: a client that has
     * switched to this engine never sees the comma, which is the only way the
     * width options can be honoured at all — a key the client inserts itself
     * is one the library never saw. It is also what ibus-pinyin's
     * FallbackEditor does with the same key.
     */
    PT_CHECK(press(ctx, ','));
    check_str("comma at the default punctuation width", log.commits, COMMA_FW);
    /* Committed outright, not composed: there is nothing to convert. */
    check_str("no composition from a comma", preedit_of(ctx), "");

    log_reset(&log);
    PT_CHECK(press(ctx, '.'));
    check_str("period at the default punctuation width", log.commits, PERIOD_FW);

    /*
     * A digit is Latin, not punctuation, and the two options are independent —
     * this is the combination the header calls "the useful one" and makes the
     * default, so it is worth pinning that they really are separate knobs.
     */
    log_reset(&log);
    PT_CHECK(press(ctx, '1'));
    check_str("digit at the default latin width", log.commits, "1");

    /*
     * Punctuation with no Chinese form still gets the full-width treatment.
     * This is the one place the layer departs from ibus-pinyin, which falls
     * back to the *Latin* width flag here and so leaves its '@' ASCII while
     * its '!' is not. One option governing one class of character is what the
     * header promises and what the anthy front end already does.
     */
    log_reset(&log);
    PT_CHECK(press(ctx, '@'));
    check_str("punctuation with no Chinese form", log.commits, AT_FW);

    pathime_context_destroy(ctx);

    /* --- Half-width punctuation ----------------------------------------- */

    ctx = open_context(pinyin, &client, &log);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_PUNCTUATION_WIDTH,
                                                   PATHIME_WIDTH_HALF),
                    PATHIME_OK);
    PT_CHECK(press(ctx, ','));
    check_str("comma at half punctuation width", log.commits, ",");
    /* Still handled — the engine emits it, it just emits the ASCII form. */
    log_reset(&log);
    PT_CHECK(press(ctx, '"'));
    check_str("quote at half punctuation width", log.commits, "\"");
    pathime_context_destroy(ctx);

    /* --- Full-width Latin ------------------------------------------------ */

    ctx = open_context(pinyin, &client, &log);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_LATIN_WIDTH,
                                                   PATHIME_WIDTH_FULL),
                    PATHIME_OK);
    PT_CHECK(press(ctx, '1'));
    check_str("digit at full latin width", log.commits, ONE_FW);

    /*
     * Space is Latin too, and the header says so. It is also the one key whose
     * meaning depends on whether anything is composing — see below.
     */
    log_reset(&log);
    PT_CHECK(press(ctx, ' '));
    check_str("space at full latin width", log.commits, SPACE_FW);
    pathime_context_destroy(ctx);

    /* --- The variant chooses the table ---------------------------------- */

    ctx = open_context(pinyin, &client, &log);
    PT_CHECK(press(ctx, '['));
    check_str("bracket, simplified", log.commits, BRACKET_S_L);
    pathime_context_destroy(ctx);

    ctx = open_context(pinyin, &client, &log);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_CHINESE_VARIANT,
                                                   PATHIME_CHINESE_TRADITIONAL_ONLY),
                    PATHIME_OK);
    PT_CHECK(press(ctx, '['));
    /* Not a width difference: 【 and 「 are both full width. The variant is a
     * third input to the substitution, which is why there are two tables. */
    check_str("bracket, traditional", log.commits, BRACKET_T_L);
    pathime_context_destroy(ctx);

    /* --- The two rules that need memory --------------------------------- */

    ctx = open_context(pinyin, &client, &log);
    PT_CHECK(press(ctx, '"'));
    PT_CHECK(press(ctx, '"'));
    check_str("quotes alternate", log.commits, QUOTE_OPEN QUOTE_CLOSE);

    /* And a reset puts the next one back to opening: an unclosed quotation
     * belongs to the run of text that opened it. */
    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);
    PT_CHECK(press(ctx, '"'));
    check_str("quote after a reset", log.commits, QUOTE_OPEN);
    pathime_context_destroy(ctx);

    ctx = open_context(pinyin, &client, &log);
    type(ctx, "1");
    PT_CHECK(press(ctx, '.'));
    type(ctx, "5");
    check_str("a period between digits is a decimal point", log.commits, "1.5");

    /*
     * And the look-behind is over the document, not over this layer's own
     * output: the 你 committed in between is what the second period follows,
     * so it is a full stop again. Only tracking what *we* emitted would get
     * this wrong, which is why the bookkeeping is ordered around pyzy's
     * commits rather than folded into the emit call.
     */
    log_reset(&log);
    type(ctx, "ni");
    PT_CHECK(press(ctx, '.'));
    check_str("a period after a committed character", log.commits, NI PERIOD_FW);
    pathime_context_destroy(ctx);

    /* --- Mid-composition: ended first, never lost ------------------------ */

    ctx = open_context(pinyin, &client, &log);
    type(ctx, "nihao");
    check_str("composition before the comma", preedit_of(ctx), PINYIN_NI_HAO);

    log_reset(&log);
    PT_CHECK(press(ctx, ','));
    /*
     * The composition is taken as the user was shown it and the comma follows
     * it, in that order and in one commit stream. ibus-pinyin's default
     * instead swallows the key and loses the comma outright; that is the one
     * behaviour here deliberately not copied.
     */
    check_str("composition then comma", log.commits, NI_HAO COMMA_FW);
    check_str("nothing left composing", preedit_of(ctx), "");
    pathime_context_destroy(ctx);

    /* --- Space: the convert key only while composing --------------------- */

    ctx = open_context(pinyin, &client, &log);
    PT_CHECK(press(ctx, ' '));
    check_str("space with nothing composing", log.commits, " ");

    log_reset(&log);
    type(ctx, "ni");
    PT_CHECK(press(ctx, ' '));
    /* Converted rather than emitted: the switch claims the key before the
     * emit path can see it, which is the whole reason the emit path runs last. */
    check_str("space while composing", log.commits, NI);
    pathime_context_destroy(ctx);

    /* --- The apostrophe is two different keys ---------------------------- */

    ctx = open_context(pinyin, &client, &log);
    PT_CHECK(press(ctx, '\''));
    /*
     * With nothing typed it is an opening quotation mark. Without the guard in
     * insertable(), pyzy takes it as its syllable separator, accepts it, and
     * renders nothing at all — an invisible composition and no text.
     */
    PT_CHECK(log.commit_count == 1);
    check_str("leading apostrophe is a quotation mark", preedit_of(ctx), "");

    log_reset(&log);
    type(ctx, "xian");
    PT_CHECK(press(ctx, '\''));
    /* Between syllables it is pyzy's, and disambiguates xi'an from xian. */
    PT_CHECK(log.commit_count == 0);
    PT_CHECK(strlen(preedit_of(ctx)) > 0);
    pathime_context_destroy(ctx);

    /* --- Bopomofo takes more keys, and emits the rest -------------------- */

    ctx = open_context(bopomofo, &client, &log);
    /*
     * Bopomofo's layouts bind digits and much of the punctuation as tone and
     * symbol keys, so pyzy takes far more here than it does for pinyin — but
     * the fallback is the same one, reached when pyzy's own insert() declines.
     */
    /*
     * ',' is ㄝ on the standard zhuyin keyboard, and one vowel alone matches no
     * phrase — so pyzy has no candidate and suppresses its auxiliary text
     * entirely (PinyinContext.cc:163-167), leaving the symbol in restText().
     * That is the one path where the auxiliary text is not the whole preedit,
     * and without the fallback in harvest() the user would type a zhuyin symbol
     * and watch nothing appear. Asserted exactly rather than as "something
     * happened", which is a check this failure would slip through.
     */
    PT_CHECK(press(ctx, ','));
    PT_CHECK(log.commit_count == 0);
    check_str("an incomplete zhuyin syllable is still shown",
              preedit_of(ctx),
              "\xE3\x84\x9D");  /* ㄝ U+311D */
    pathime_context_destroy(ctx);

    /* --- Non-printable keys are still the client's ----------------------- */

    ctx = open_context(pinyin, &client, &log);
    /* The emit path is bounded at printable ASCII precisely so that these keep
     * working: Backspace must delete the client's text and Escape must close
     * its dialog when there is no composition to spend them on. */
    PT_CHECK(!press(ctx, PATHIME_KEY_BACKSPACE));
    PT_CHECK(!press(ctx, PATHIME_KEY_ESCAPE));
    PT_CHECK(!press(ctx, PATHIME_KEY_RETURN));
    PT_CHECK(!press(ctx, PATHIME_KEY_TAB));
    PT_CHECK(log.commit_count == 0);
    pathime_context_destroy(ctx);
}

/*
 * Double pinyin, where the preedit and the keys that produced it genuinely
 * differ — the one mode in which the preedit rule says something a full-pinyin
 * test cannot.
 *
 * "nihk" is four keys; "ni hao" is the two syllables they decode to. The
 * preedit shows the syllables, because those are what the user is composing;
 * the keys are an encoding of them and the client already knows which ones it
 * sent. Nothing in the API surfaces the raw keys.
 */
static void test_double_pinyin_preedit(pathime_engine_t *pinyin)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = open_context(pinyin, &client, &log);

    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_PINYIN_SCHEME,
                                                   PATHIME_PINYIN_SCHEME_DOUBLE_MSPY),
                    PATHIME_OK);
    type(ctx, "nihk");
    check_str("double pinyin preedit is the decoded syllables",
              preedit_of(ctx), PINYIN_NI_HAO);

    /* Same candidates as the four-key spelling reached, so the scheme really
     * decoded rather than merely echoing. */
    check_str("double pinyin candidate 0", candidate_of(ctx, 0), NI_HAO);

    /* And Return still commits what is shown, separators dropped — the same
     * guarantee full pinyin gives, on a spelling where the raw keys would have
     * given something else entirely. */
    log_reset(&log);
    PT_CHECK(press(ctx, PATHIME_KEY_RETURN));
    PT_CHECK(log.commit_count == 1);
    check_str("double pinyin Return commits the preedit", log.commits, "nihao");

    pathime_context_destroy(ctx);
}

/*
 * Commit, checked on double pinyin because that is where getting it wrong is
 * visible.
 *
 * A forced commit must land where Return lands: the preedit the client was
 * shown, separators dropped. Taking pyzy's own commit(TYPE_CONVERTED) instead
 * would emit the raw keystrokes "nihk" under this scheme — shorthand for an
 * IME that is no longer listening, and nothing any user wants in a document.
 * Full pinyin cannot tell the two apart, so it is not the case to test on.
 */
static void test_commit(pathime_engine_t *pinyin)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = open_context(pinyin, &client, &log);

    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_PINYIN_SCHEME,
                                                   PATHIME_PINYIN_SCHEME_DOUBLE_MSPY),
                    PATHIME_OK);
    type(ctx, "nihk");
    check_str("preedit before commit", preedit_of(ctx), PINYIN_NI_HAO);

    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_commit(ctx), PATHIME_OK);
    PT_CHECK(log.commit_count == 1);
    check_str("commit takes the preedit, not the keystrokes", log.commits, "nihao");
    check_str("preedit after commit", preedit_of(ctx), "");

    /* Nothing composing: no-op, no callbacks. */
    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_commit(ctx), PATHIME_OK);
    PT_CHECK(log.commit_count == 0);
    PT_CHECK(log.changed_count == 0);

    pathime_context_destroy(ctx);
}

/*
 * The other half of the fuzzy claim: the engines that do *not* implement it.
 *
 * Hangul is checked here because this test can create one. Anthy cannot be
 * created without its dictionary, which this test has no wiring for, so the
 * matching assertion lives in engine_anthy_test.c instead — see the comment
 * there. Splitting it keeps both halves real rather than making one of them
 * skip silently.
 */
static void test_fuzzy_is_not_a_hangul_option(void)
{
    pathime_engine_t *hangul = NULL;
    pathime_option_info_t info;

    if (!pathime_has_engine(PATHIME_ENGINE_HANGUL)) return;

    PT_CHECK_STATUS(pathime_engine_create(PATHIME_ENGINE_HANGUL, &hangul), PATHIME_OK);
    if (hangul == NULL) return;

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    /* An option the engine does not implement is reported through `supported`,
     * not as an error — the query itself still succeeds. */
    PT_CHECK_STATUS(pathime_engine_option_info(hangul, PATHIME_OPT_PINYIN_FUZZY, &info),
                    PATHIME_OK);
    PT_CHECK(!info.supported);
    PT_CHECK_STATUS(pathime_engine_set_option_int(hangul, PATHIME_OPT_PINYIN_FUZZY, 0),
                    PATHIME_ERROR_UNSUPPORTED);
    PT_CHECK(!pathime_engine_option_is_set(hangul, PATHIME_OPT_PINYIN_FUZZY));

    pathime_engine_destroy(hangul);
}

/*
 * Three contexts keyed alternately: two on the pinyin engine, one on bopomofo.
 *
 * pyzy is where the interleaving question has the most to say, because it is
 * the engine with the most state that is *not* in the context the client holds
 * — a lazily enumerated candidate list that mutates as it is walked, and the
 * eagerly materialized copy this library keeps in front of it. A single shared
 * copy would be invisible in every other test in this file and would show here
 * on the first key.
 *
 * The second half is the engine-level broadcast against a live backend. The
 * core suite proves the dispatch bookkeeping with hand-built contexts; what it
 * cannot show is that an engine-level change reaches a context that is
 * mid-composition and re-derives its list, while leaving a context that
 * overrode the option — and a context belonging to another engine — alone.
 */
static void test_interleaved_contexts(pathime_engine_t *pinyin, pathime_engine_t *bopomofo)
{
    /* ㄋㄧˇ,ㄏㄠˇ — what "su3cl3" composes; see test_bopomofo. */
    static const char kZhuyin[] =
        "\xE3\x84\x8B\xE3\x84\xA7\xCB\x87,\xE3\x84\x8F\xE3\x84\xA0\xCB\x87";
    static const char kPinyinKeys[] = "nihao";
    static const char kZhuyinKeys[] = "su3cl3";

    client_log_t log_a, log_b, log_z;
    pathime_client_t client_a, client_b, client_z;
    pathime_context_t *a = open_context(pinyin, &client_a, &log_a);
    pathime_context_t *b = open_context(pinyin, &client_b, &log_b);
    pathime_context_t *z = open_context(bopomofo, &client_z, &log_z);
    char a_list[8][32], b_list[8][32], z_list[8][32];
    size_t full_count;
    size_t i;

    /* One key to each context in turn, until each spelling is finished. */
    for (i = 0; i < sizeof(kZhuyinKeys) - 1; i++) {
        if (i < sizeof(kPinyinKeys) - 1) {
            PT_CHECK(press(a, (uint32_t)(unsigned char)kPinyinKeys[i]));
            PT_CHECK(press(b, (uint32_t)(unsigned char)kPinyinKeys[i]));
        }
        PT_CHECK(press(z, (uint32_t)(unsigned char)kZhuyinKeys[i]));
    }

    /* Each context composed its own spelling in its own script. */
    check_str("a's preedit", preedit_of(a), PINYIN_NI_HAO);
    check_str("b's preedit", preedit_of(b), PINYIN_NI_HAO);
    check_str("z's preedit", preedit_of(z), kZhuyin);

    /* One callback per key, to that key's client and no other. */
    PT_CHECK(log_a.changed_count == (int)(sizeof(kPinyinKeys) - 1));
    PT_CHECK(log_b.changed_count == (int)(sizeof(kPinyinKeys) - 1));
    PT_CHECK(log_z.changed_count == (int)(sizeof(kZhuyinKeys) - 1));
    PT_CHECK(log_a.commit_count == 0);
    PT_CHECK(log_b.commit_count == 0);
    PT_CHECK(log_z.commit_count == 0);

    /*
     * The same input interleaved with two other compositions reaches the same
     * list it reaches alone — a and b are the same query typed alternately,
     * and z is the same query reached through the other engine.
     */
    full_count = pathime_context_composition(a)->candidate_count;
    PT_CHECK(full_count >= 8);
    PT_CHECK_SIZE(pathime_context_composition(b)->candidate_count, full_count);
    PT_CHECK_SIZE(pathime_context_composition(z)->candidate_count, full_count);

    snapshot(a, 8, a_list);
    snapshot(b, 8, b_list);
    snapshot(z, 8, z_list);
    for (i = 0; i < 8; i++) {
        check_str("b's list matches a's", b_list[i], a_list[i]);
        check_str("z's list matches a's", z_list[i], a_list[i]);
    }

    /* --- the engine-level broadcast, with all three mid-composition ------- */

    /* b overrides the option for itself, which is what makes it immune below. */
    PT_CHECK_STATUS(pathime_context_set_option_int(b, PATHIME_OPT_MAX_CANDIDATES, 5),
                    PATHIME_OK);
    PT_CHECK_SIZE(pathime_context_composition(b)->candidate_count, 5);

    log_reset(&log_a);
    log_reset(&log_b);
    log_reset(&log_z);
    PT_CHECK_STATUS(pathime_engine_set_option_int(pinyin, PATHIME_OPT_MAX_CANDIDATES, 3),
                    PATHIME_OK);

    /* a inherits, so a is re-derived and told about it. */
    PT_CHECK_SIZE(pathime_context_composition(a)->candidate_count, 3);
    PT_CHECK(log_a.changed_count == 1);
    /* b set it itself: tier 1 already won, so nothing changed for b. */
    PT_CHECK_SIZE(pathime_context_composition(b)->candidate_count, 5);
    PT_CHECK(log_b.changed_count == 0);
    /* z belongs to another engine handle entirely. */
    PT_CHECK_SIZE(pathime_context_composition(z)->candidate_count, full_count);
    PT_CHECK(log_z.changed_count == 0);

    /* Raising the cap back appends rather than rebuilding: the entries a
     * already showed are still at the indices it showed them at. */
    PT_CHECK_STATUS(pathime_engine_reset_option(pinyin, PATHIME_OPT_MAX_CANDIDATES),
                    PATHIME_OK);
    PT_CHECK_SIZE(pathime_context_composition(a)->candidate_count, full_count);
    for (i = 0; i < 8; i++) {
        check_str("a's list after the cap is lifted", candidate_of(a, i), a_list[i]);
    }

    /* --- committing in one context leaves the others composing ----------- */

    log_reset(&log_a);
    log_reset(&log_b);
    log_reset(&log_z);

    /* Candidate 0 covers the whole input, so pyzy commits from inside
     * selectCandidate() — the automatic commit, fired here while two other
     * compositions are live. */
    PT_CHECK_STATUS(pathime_context_select_candidate(a, 0), PATHIME_OK);
    PT_CHECK(log_a.commit_count == 1);
    check_str("a commits its own text", log_a.commits, NI_HAO);
    check_str("a is finished", preedit_of(a), "");
    PT_CHECK(log_b.commit_count == 0);
    PT_CHECK(log_z.commit_count == 0);
    check_str("b still composing", preedit_of(b), PINYIN_NI_HAO);
    check_str("z still composing", preedit_of(z), kZhuyin);

    /* Return means "what I typed", and what b typed is b's alone. */
    PT_CHECK(press(b, PATHIME_KEY_RETURN));
    check_str("b commits its raw input", log_b.commits, "nihao");
    PT_CHECK(log_z.commit_count == 0);

    PT_CHECK_STATUS(pathime_context_select_candidate(z, 0), PATHIME_OK);
    PT_CHECK(log_z.commit_count == 1);
    check_str("z commits its own text", log_z.commits, NI_HAO);

    pathime_context_destroy(z);
    pathime_context_destroy(b);
    pathime_context_destroy(a);
}

int main(void)
{
    pathime_engine_t *pinyin = NULL;
    pathime_engine_t *bopomofo = NULL;
    pathime_init_params_t params;

    /*
     * The library's own persistent-storage surface, pointed at the build tree:
     * pyzy_global_init() roots its cache and config directories here, so the
     * learned-phrase database this run writes cannot be the developer's.
     *
     * It is *not* how the main database is found. That one lives under
     * resource_dir, left at its default here so this test opens the database
     * staged beside the library exactly as a client would.
     */
    memset(&params, 0, sizeof(params));
    params.struct_size = sizeof(params);
    params.data_dir = PYZY_TEST_DATA_DIR;
    PT_CHECK_STATUS(pathime_init(&params), PATHIME_OK);

    /*
     * One backend, two engine ids, and the id round-trips through the handle so
     * a client juggling both need not carry it alongside.
     */
    PT_CHECK(pathime_has_engine(PATHIME_ENGINE_PINYIN));
    PT_CHECK(pathime_has_engine(PATHIME_ENGINE_BOPOMOFO));
    PT_CHECK_STATUS(pathime_engine_create(PATHIME_ENGINE_PINYIN, &pinyin), PATHIME_OK);
    PT_CHECK_STATUS(pathime_engine_create(PATHIME_ENGINE_BOPOMOFO, &bopomofo), PATHIME_OK);
    PT_CHECK(pinyin != NULL && bopomofo != NULL);
    PT_CHECK(pathime_engine_id(pinyin) == PATHIME_ENGINE_PINYIN);
    PT_CHECK(pathime_engine_id(bopomofo) == PATHIME_ENGINE_BOPOMOFO);
    PT_CHECK(pinyin != bopomofo);

    /* Neither uses the surrounding-text surface: pyzy has no reconversion path
     * and the adapter asks the client for nothing. */
    PT_CHECK(pathime_engine_requirements(pinyin) == 0);
    PT_CHECK(pathime_engine_requirements(bopomofo) == 0);

    if (pinyin != NULL && bopomofo != NULL) {
        test_pinyin_composition(pinyin);
        test_settled_boundary_and_auto_commit(pinyin);
        test_eager_materialization(pinyin);
        test_candidate_cursor(pinyin);
        test_callback_safety(pinyin);
        test_bopomofo(pinyin, bopomofo);
        test_options(pinyin, bopomofo);
        test_width_and_punctuation(pinyin, bopomofo);
        test_double_pinyin_preedit(pinyin);
        test_commit(pinyin);
        test_fuzzy_is_not_a_hangul_option();
        /* Last: it commits, and pyzy learns on commit into a user database
         * shared by every context. Everything above that pins a candidate
         * position has run by now. */
        test_interleaved_contexts(pinyin, bopomofo);
    }

    pathime_engine_destroy(bopomofo);
    pathime_engine_destroy(pinyin);
    pathime_shutdown();
    return pt_report("api.engine_pyzy");
}

#endif /* PATHIME_WITH_PYZY */
