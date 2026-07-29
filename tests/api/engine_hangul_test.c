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
 * at all — hanja is out of scope for this API — so the composition it
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
 * The compatibility jamo the PATHIME_HANGUL_PREEDIT_NONE tests need: ㄱ
 * U+3131, ㅏ U+314F, ㄴ U+3134. These are what libhangul shows for a lone
 * consonant or vowel that has not yet joined a syllable — a stranded jamo, in
 * other words, which is exactly what the stale-snapshot recovery leaves
 * behind.
 */
#define GIYEOK  "\xE3\x84\xB1"
#define A_VOWEL "\xE3\x85\x8F"
#define NIEUN   "\xE3\x84\xB4"

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

    /*
     * A toy client document, maintained by applying the callbacks the way a
     * real client would: commit_text appends at the cursor, and
     * delete_surrounding_text removes a range relative to it. The cursor is
     * always at the end, which is the only case PATHIME_HANGUL_PREEDIT_NONE
     * produces.
     *
     * Only the PREEDIT_NONE tests read it. It exists because that mode's
     * correctness is not a property of any one callback — it is whether the
     * sequence of deletions and commits leaves the right text behind — and
     * checking the callbacks individually would miss a syllable duplicated or
     * a deletion applied to the wrong range.
     */
    char doc[512];
    int last_delete_offset;
    size_t last_delete_count;
    int delete_count;
} client_log_t;

/* Scalar values in a NUL-terminated UTF-8 string: lead bytes, not bytes. */
static size_t doc_scalars(const char *s)
{
    size_t n = 0;
    for (; *s != '\0'; s++) {
        if (((unsigned char)*s & 0xC0) != 0x80) {
            n++;
        }
    }
    return n;
}

/* Remove the last @a count scalar values, which is what an offset of -count
 * means when the cursor sits at the end of the document. */
static void doc_erase_tail(char *doc, size_t count)
{
    size_t i = strlen(doc);
    while (count > 0 && i > 0) {
        i--;
        if (((unsigned char)doc[i] & 0xC0) != 0x80) {
            count--;
        }
    }
    doc[i] = '\0';
}

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

    /* Apply to the toy document as a client would: insert at the cursor. */
    if (strlen(log->doc) + text.len < sizeof(log->doc)) {
        memcpy(log->doc + strlen(log->doc), text.bytes, text.len);
    }
}

static void on_delete(void *user_data, ptrdiff_t offset, size_t count)
{
    client_log_t *log = (client_log_t *)user_data;
    log_order(log, 'd');
    log->delete_count++;
    log->last_delete_offset = (int)offset;
    log->last_delete_count = count;

    /* "Never 0", per the callback's documentation. */
    PT_CHECK(count != 0);

    /*
     * Applied only for the shape this engine produces — a range ending at the
     * cursor. Anything else would be a bug in the library rather than
     * something this toy client should paper over, so it is asserted instead.
     */
    PT_CHECK(offset == -(ptrdiff_t)count);
    if (offset == -(ptrdiff_t)count) {
        doc_erase_tail(log->doc, count);
    }
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

/*
 * Clear the callback record but keep the document.
 *
 * The PATHIME_HANGUL_PREEDIT_NONE tests need to check one key's worth of
 * callbacks against a document several keys deep, and log_reset() would throw
 * that document away — which does not merely lose an assertion, it silently
 * changes what the *next* deletion applies to and can make a wrong result look
 * right.
 */
static void log_reset_callbacks(client_log_t *log)
{
    char saved[sizeof(log->doc)];
    memcpy(saved, log->doc, sizeof(saved));
    memset(log, 0, sizeof(*log));
    memcpy(log->doc, saved, sizeof(saved));
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

        /*
         * Nor is there a cursor to move. The empty list is what rejects this,
         * before the adapter is consulted at all — so the answer is
         * INVALID_ARGUMENT for an index that cannot exist, not the UNSUPPORTED
         * that ContextBackend::set_cursor() defaults to. This engine can never
         * reach that default, which is why it is here as a check rather than
         * as a distinction the client has to handle.
         */
        PT_CHECK_STATUS(pathime_context_set_candidate_cursor(ctx, 0),
                        PATHIME_ERROR_INVALID_ARGUMENT);
        /* And the field is still there, and still 0. */
        PT_CHECK_SIZE(c->candidate_cursor, 0);
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
    PT_CHECK(pathime_context_is_focused(ctx));
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, false), PATHIME_OK);
    PT_CHECK(!pathime_context_is_focused(ctx));
    PT_CHECK(log.changed_count == 0);
    PT_CHECK_SIZE(strlen(log.commits), 0);
    check_str("preedit survives focus loss", preedit_of(ctx), HA);

    /* Redundant transitions are no-ops. */
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, false), PATHIME_OK);
    PT_CHECK(!pathime_context_is_focused(ctx));
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, true), PATHIME_OK);
    PT_CHECK(pathime_context_is_focused(ctx));
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

/* ---------------------------------------------------------------------- */

/*
 * PATHIME_HANGUL_PREEDIT_NONE: the document is the display.
 *
 * The mode holds nothing. Each jamo goes into the client's text as it is
 * struck, and the syllable grows by deleting what was written a moment ago and
 * writing the fuller form in its place. It is the only producer of
 * delete_surrounding_text and the only reason this library has a
 * surrounding-text surface at all.
 *
 * The snapshot has to be refreshed after every key, because this mode commits
 * on every key and a commit invalidates the snapshot by definition. That is
 * not this test being fussy — it is the obligation the option's documentation
 * calls "keen", and test_preedit_none_stale_snapshot() below is what happens
 * to a client that does not meet it.
 */
static void refresh_snapshot(pathime_context_t *ctx, client_log_t *log)
{
    pathime_str_t text;
    text.bytes = log->doc;
    text.len = strlen(log->doc);
    PT_CHECK_STATUS(pathime_context_set_surrounding_text(ctx, text,
                                                         doc_scalars(log->doc)),
                    PATHIME_OK);
}

/*
 * Per-context requirements, which is the case pathime_engine_requirements()
 * cannot answer.
 *
 * PATHIME_OPT_HANGUL_PREEDIT is settable per context, and it is the one option
 * in the library that drives a PATHIME_REQUIRES_* bit. So an engine left at its
 * default reports nothing required while a context that overrode it requires
 * both callbacks — and a client displaying the engine's answer over its text
 * field is simply wrong about the context it is actually typing into.
 *
 * The capping half is the other reason the two calls differ. A context whose
 * client cannot delete resolves PREEDIT_NONE back down, so it needs nothing
 * and says so, while the engine that was configured into that mode still
 * reports the requirement it was configured with.
 */
static void test_context_requirements(pathime_engine_t *engine)
{
    pathime_client_t full;
    pathime_client_t no_delete;
    pathime_context_t *ctx = NULL;
    pathime_context_t *limited = NULL;
    client_log_t log;

    log_reset(&log);

    memset(&full, 0, sizeof(full));
    full.struct_size = sizeof(full);
    full.commit_text = on_commit;
    full.delete_surrounding_text = on_delete;
    full.composition_changed = on_changed;

    /* A client with no delete_surrounding_text — legal, and the reason the
     * capping rule exists. */
    memset(&no_delete, 0, sizeof(no_delete));
    no_delete.struct_size = sizeof(no_delete);
    no_delete.commit_text = on_commit;
    no_delete.composition_changed = on_changed;

    PT_CHECK_STATUS(pathime_context_create(engine, &full, &log, &ctx), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_create(engine, &no_delete, &log, &limited),
                    PATHIME_OK);

    /* Nothing overridden anywhere: both levels agree on nothing required. */
    PT_CHECK(pathime_engine_requirements(engine) == 0);
    PT_CHECK(pathime_context_requirements(ctx) == 0);
    PT_CHECK(pathime_context_requirements(limited) == 0);

    /* The per-context override. The context now requires both bits; the
     * engine, untouched, still requires neither. */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_HANGUL_PREEDIT,
                                                   PATHIME_HANGUL_PREEDIT_NONE),
                    PATHIME_OK);
    PT_CHECK(pathime_context_requirements(ctx) ==
             (PATHIME_REQUIRES_SURROUNDING_TEXT | PATHIME_REQUIRES_DELETE_SURROUNDING));
    PT_CHECK(pathime_engine_requirements(engine) == 0);
    /* The other context is unaffected: this is per-context state. */
    PT_CHECK(pathime_context_requirements(limited) == 0);

    /* Back to a mode that needs nothing. */
    PT_CHECK_STATUS(pathime_context_reset_option(ctx, PATHIME_OPT_HANGUL_PREEDIT),
                    PATHIME_OK);
    PT_CHECK(pathime_context_requirements(ctx) == 0);

    /*
     * Now from the engine level, which is where the two calls part company.
     * The engine reports the configured value uncapped — that is what context
     * creation must test a new client against. The context whose client cannot
     * delete reports the *effective* value, which is nothing, because its
     * resolution capped PREEDIT_NONE back to _SYLLABLE.
     */
    PT_CHECK_STATUS(pathime_engine_set_option_int(engine, PATHIME_OPT_HANGUL_PREEDIT,
                                                  PATHIME_HANGUL_PREEDIT_NONE),
                    PATHIME_OK);
    PT_CHECK(pathime_engine_requirements(engine) ==
             (PATHIME_REQUIRES_SURROUNDING_TEXT | PATHIME_REQUIRES_DELETE_SURROUNDING));
    PT_CHECK(pathime_context_requirements(ctx) ==
             (PATHIME_REQUIRES_SURROUNDING_TEXT | PATHIME_REQUIRES_DELETE_SURROUNDING));
    PT_CHECK(pathime_context_requirements(limited) == 0);

    /* And that uncapped engine answer is exactly what refuses a new client
     * that cannot serve it. */
    {
        pathime_context_t *rejected = NULL;
        PT_CHECK_STATUS(pathime_context_create(engine, &no_delete, &log, &rejected),
                        PATHIME_ERROR_MISSING_CALLBACK);
        PT_CHECK(rejected == NULL);
    }

    PT_CHECK_STATUS(pathime_engine_reset_option(engine, PATHIME_OPT_HANGUL_PREEDIT),
                    PATHIME_OK);
    pathime_context_destroy(limited);
    pathime_context_destroy(ctx);
}

static void test_preedit_none_builds_in_document(pathime_engine_t *engine)
{
    pathime_client_t client;
    pathime_context_t *ctx = NULL;
    client_log_t log;

    log_reset(&log);
    memset(&client, 0, sizeof(client));
    client.struct_size = sizeof(client);
    client.commit_text = on_commit;
    client.delete_surrounding_text = on_delete;
    client.composition_changed = on_changed;

    PT_CHECK_STATUS(pathime_context_create(engine, &client, &log, &ctx), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, true), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_HANGUL_PREEDIT,
                                                   PATHIME_HANGUL_PREEDIT_NONE),
                    PATHIME_OK);
    refresh_snapshot(ctx, &log);

    /* g -> ㅎ. Nothing to revise yet, so no deletion — the first key of a
     * syllable is a plain insertion. */
    log_reset_callbacks(&log);
    refresh_snapshot(ctx, &log);
    PT_CHECK(press(ctx, 'g'));
    check_str("none: after g", log.doc, HIEUH);
    PT_CHECK_SIZE(log.delete_count, 0);

    /*
     * k -> 하. Now there is: ㅎ comes back out and 하 goes in. The dispatch
     * order matters as much as the result, so it is checked — 'd' before 'c'.
     *
     * And composition_changed does *not* follow, which is right rather than an
     * omission: this mode's composition is empty before the key and empty
     * after it, so nothing about it changed. The header dispatches that
     * callback when the composition data is replaced, and here it never is —
     * all the movement is in the document. A client in this mode is one that
     * cannot draw a preedit anyway, which is why it is allowed not to supply
     * the callback at all.
     */
    refresh_snapshot(ctx, &log);
    log_reset_callbacks(&log);
    PT_CHECK(press(ctx, 'k'));
    check_str("none: after k", log.doc, HA);
    PT_CHECK_SIZE(log.delete_count, 1);
    PT_CHECK(log.last_delete_offset == -1);
    PT_CHECK_SIZE(log.last_delete_count, 1);
    check_str("none: k callback order", log.order, "dc");
    PT_CHECK_SIZE(log.changed_count, 0);

    /* s -> 한. */
    refresh_snapshot(ctx, &log);
    log_reset_callbacks(&log);
    PT_CHECK(press(ctx, 's'));
    check_str("none: after s", log.doc, HAN);

    /*
     * The preedit stays empty throughout, which is the whole point: a client
     * that cannot draw one is exactly who this mode is for.
     */
    check_str("none: preedit stays empty", preedit_of(ctx), "");
    PT_CHECK_SIZE(log.last_settled, 0);

    /*
     * r -> 한 is finished and ㄱ begins. One commit carries both, after the
     * provisional 한 is removed: a client must not see the finished syllable
     * arrive separately from the one that follows it.
     */
    refresh_snapshot(ctx, &log);
    log_reset_callbacks(&log);
    PT_CHECK(press(ctx, 'r'));
    check_str("none: after r", log.doc, HAN GIYEOK);
    PT_CHECK_SIZE(log.commit_count, 1);
    check_str("none: r commit payload", log.commits, HAN GIYEOK);

    pathime_context_destroy(ctx);
}

/*
 * Backspace in this mode deletes the committed syllable and recommits it one
 * component shorter, which is the option's documentation almost verbatim.
 */
static void test_preedit_none_backspace(pathime_engine_t *engine)
{
    pathime_client_t client;
    pathime_context_t *ctx = NULL;
    client_log_t log;

    log_reset(&log);
    memset(&client, 0, sizeof(client));
    client.struct_size = sizeof(client);
    client.commit_text = on_commit;
    client.delete_surrounding_text = on_delete;

    PT_CHECK_STATUS(pathime_context_create(engine, &client, &log, &ctx), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, true), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_HANGUL_PREEDIT,
                                                   PATHIME_HANGUL_PREEDIT_NONE),
                    PATHIME_OK);

    refresh_snapshot(ctx, &log);
    PT_CHECK(press(ctx, 'g'));
    refresh_snapshot(ctx, &log);
    PT_CHECK(press(ctx, 'k'));
    refresh_snapshot(ctx, &log);
    PT_CHECK(press(ctx, 's'));
    check_str("none/bs: built 한", log.doc, HAN);

    refresh_snapshot(ctx, &log);
    PT_CHECK(press(ctx, PATHIME_KEY_BACKSPACE));
    check_str("none/bs: back to 하", log.doc, HA);

    refresh_snapshot(ctx, &log);
    PT_CHECK(press(ctx, PATHIME_KEY_BACKSPACE));
    check_str("none/bs: back to ㅎ", log.doc, HIEUH);

    pathime_context_destroy(ctx);
}

/*
 * Ending the composition leaves the syllable where it is.
 *
 * The trap this guards is a double commit. In the other two modes the pending
 * syllable is flushed out of libhangul and committed when a non-composing key
 * arrives; here it is already in the document, and hangul_ic_flush() returns
 * the very same text — measured identical across all nine layouts. Committing
 * it again would write 한한.
 */
static void test_preedit_none_end_composition(pathime_engine_t *engine)
{
    pathime_client_t client;
    pathime_context_t *ctx = NULL;
    client_log_t log;

    log_reset(&log);
    memset(&client, 0, sizeof(client));
    client.struct_size = sizeof(client);
    client.commit_text = on_commit;
    client.delete_surrounding_text = on_delete;

    PT_CHECK_STATUS(pathime_context_create(engine, &client, &log, &ctx), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, true), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_HANGUL_PREEDIT,
                                                   PATHIME_HANGUL_PREEDIT_NONE),
                    PATHIME_OK);

    refresh_snapshot(ctx, &log);
    PT_CHECK(press(ctx, 'g'));
    refresh_snapshot(ctx, &log);
    PT_CHECK(press(ctx, 'k'));
    refresh_snapshot(ctx, &log);
    PT_CHECK(press(ctx, 's'));
    check_str("none/end: built 한", log.doc, HAN);

    /* Return ends it. The key is declined — it was never the engine's — and
     * nothing further is written. */
    refresh_snapshot(ctx, &log);
    log_reset_callbacks(&log);
    PT_CHECK(!press(ctx, PATHIME_KEY_RETURN));
    check_str("none/end: 한 stays, undoubled", log.doc, HAN);
    PT_CHECK_SIZE(log.commit_count, 0);
    PT_CHECK_SIZE(log.delete_count, 0);

    /* And the next jamo starts cleanly after it rather than reviving it. */
    refresh_snapshot(ctx, &log);
    PT_CHECK(press(ctx, 'r'));
    check_str("none/end: new syllable follows", log.doc, HAN GIYEOK);

    pathime_context_destroy(ctx);
}

/*
 * The recovery path: a client that does not refresh the snapshot.
 *
 * The header fixes what happens rather than leaving it undefined — the engine
 * "abandons the revision, discards the composition state that was to be
 * revised, and treats what is already in the document as final, continuing
 * from the next input as if starting fresh. No deletion is requested and no
 * key is refused; the user sees the partial text stay where it is."
 *
 * So typing the three keys of 한 without ever refreshing strands each jamo in
 * turn: ㅎㅏㄴ, not 한. That is not a good outcome for the user, and it is not
 * meant to be — it is the determinate, non-corrupting one, and the point of
 * testing it is that a client which falls behind gets *this* rather than a
 * deletion applied to text the engine can no longer see.
 */
static void test_preedit_none_stale_snapshot(pathime_engine_t *engine)
{
    pathime_client_t client;
    pathime_context_t *ctx = NULL;
    client_log_t log;

    log_reset(&log);
    memset(&client, 0, sizeof(client));
    client.struct_size = sizeof(client);
    client.commit_text = on_commit;
    client.delete_surrounding_text = on_delete;

    PT_CHECK_STATUS(pathime_context_create(engine, &client, &log, &ctx), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, true), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_HANGUL_PREEDIT,
                                                   PATHIME_HANGUL_PREEDIT_NONE),
                    PATHIME_OK);

    /* Supplied once and then never again — the commit on the first key
     * invalidates it and the client never notices. */
    refresh_snapshot(ctx, &log);

    PT_CHECK(press(ctx, 'g'));
    PT_CHECK(press(ctx, 'k'));
    PT_CHECK(press(ctx, 's'));

    check_str("none/stale: each jamo stranded", log.doc, HIEUH A_VOWEL NIEUN);

    /* Not one deletion was requested: the adapter asked first and was told no,
     * so it never issued a range the client could not honour. */
    PT_CHECK_SIZE(log.delete_count, 0);

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
        test_context_requirements(engine);
        test_layout_option(engine);
        test_preedit_none_builds_in_document(engine);
        test_preedit_none_backspace(engine);
        test_preedit_none_end_composition(engine);
        test_preedit_none_stale_snapshot(engine);
    }

    pathime_engine_destroy(engine);
    pathime_shutdown();
    return pt_report("api.engine_hangul");
}

#endif /* PATHIME_WITH_HANGUL */
