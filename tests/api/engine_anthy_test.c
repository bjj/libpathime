/*
 * The Japanese engine, driven the way a client drives it: through
 * <pathime/pathime.h> and nothing else.
 *
 * Peer of engine_hangul_test.c, and the counterpart it was written against.
 * Hangul proves the stack on the engine with no candidates at all — the
 * settled/active spine and nothing on top. Anthy is the other extreme and the
 * only engine that exercises the whole composition model at once: it has a
 * *composing front end* (romaji to kana, a state machine that is entirely this
 * library's, since anthy accepts only finished kana), and it has *multi-segment
 * conversion* behind it. So it is the one engine where settled, active and tail
 * are all non-empty at the same moment, and the one place greedy left-to-right
 * resolution can actually be watched advancing.
 *
 * ---------------------------------------------------------------------------
 * The assertion this file exists for
 * ---------------------------------------------------------------------------
 *
 * preedit_settled, in Unicode scalar values. Every character in the sentence
 * this test converts is three bytes in UTF-8, so a byte count would be exactly
 * 3x the right answer — plausible-looking, self-consistent, and wrong. It is
 * the single most likely bug in the stack and the reason the multi-segment case
 * is written out at the length it is. Hangul's word mode makes the same point
 * with one syllable; here the boundary moves three times.
 *
 * ---------------------------------------------------------------------------
 * Where the expected values came from
 * ---------------------------------------------------------------------------
 *
 * Real Japanese, observed by running this stack against the dictionary this
 * build produces — not predicted. They are written as UTF-8 escapes with the
 * character named in a comment, as engine_hangul_test.c does, so the file
 * carries no non-ASCII bytes and needs no /utf-8 from MSVC to mean what it says.
 *
 * anthy *learns* on commit and this test commits, so the ordering of a
 * candidate list is not stable across runs beyond the first few entries. Two
 * things keep that from making the test flaky: the CTest fixture
 * api.engine_anthy.clean wipes the learned record before every run, and nothing
 * here asserts a candidate position deep enough for learning to reach.
 */

#include <string.h>

#include "api_test_util.h"

/*
 * No backend header, and deliberately so. This test drives anthy through
 * <pathime/pathime.h> alone; the dictionary it needs is found by pointing
 * anthy's CONFFILE at a build-tree conf file from the environment, which
 * tests/api/CMakeLists.txt sets. Calling anthy_conf_override() from here
 * instead would need this program to link anthy — and on Windows that is not
 * merely inelegant but broken, because anthy is a *static* library absorbed
 * into pathime.dll, so a second copy linked into the test executable has its
 * own conf database and the library never sees the override. See
 * tests/api/CMakeLists.txt and cmake/ports/anthy-unicode/CMakeLists.txt.
 */

#if !PATHIME_WITH_ANTHY

int main(void)
{
    return pt_skip("api.engine_anthy", "this build does not contain anthy-unicode");
}

#else

/* ---- Hiragana the romaji front end produces ----------------------------- */

#define NI    "\xE3\x81\xAB"  /* に U+306B */
#define HO    "\xE3\x81\xBB"  /* ほ U+307B */
#define N     "\xE3\x82\x93"  /* ん U+3093 */
#define SMALL_TSU "\xE3\x81\xA3"  /* っ U+3063 */
#define SHI   "\xE3\x81\x97"  /* し U+3057 */
#define TSU   "\xE3\x81\xA4"  /* つ U+3064 */
#define KA    "\xE3\x81\x8B"  /* か U+304B */
#define JI    "\xE3\x81\x98"  /* じ U+3058 */

/* PATHIME_ANTHY_TYPING_KANA. The US-101 positions these sit on, per
 * scim-anthy's 101kana table: a=ち, k=の, t=か, f=は, e=い, Shift-2='@'=゛,
 * '['=゜. */
#define CHI         "\xE3\x81\xA1"  /* ち U+3061 */
#define NO_KANA     "\xE3\x81\xAE"  /* の U+306E */
#define GA          "\xE3\x81\x8C"  /* が U+304C */
#define PA          "\xE3\x81\xB1"  /* ぱ U+3071 */
#define I_KANA      "\xE3\x81\x84"  /* い U+3044 */
#define VOICED_MARK "\xE3\x82\x9B"  /* ゛U+309B */

/* にほん / にほn — "nihon" finished, and mid-typing with the n still pending. */
#define NIHON         NI HO N
#define NIHON_PENDING NI HO "n"

/* かんじ — the reading of 漢字. */
#define KANJI_KANA KA N JI

/* ---- Katakana, for PATHIME_OPT_ANTHY_KANA_SCRIPT ------------------------ */

#define KATA_NI "\xE3\x83\x8B"  /* ニ U+30CB */
#define KATA_HO "\xE3\x83\x9B"  /* ホ U+30DB */

/* ---- Kanji the conversion produces -------------------------------------- */

#define KANJI "\xE6\xBC\xA2\xE5\xAD\x97"  /* 漢字 U+6F22 U+5B57 */
#define KANJI_2ND "\xE7\x9B\xA3\xE4\xBA\x8B"  /* 監事 U+76E3 U+4E8B */

/*
 * "kyouhaiitenkidesune" — 今日はいい天気ですね, "it's nice weather today".
 * anthy segments the reading into exactly three: 今日は / いい / てんきですね.
 * The pieces are named individually because the point of the multi-segment
 * test is watching the boundary walk across them.
 */
#define KYOU_HA "\xE4\xBB\x8A\xE6\x97\xA5\xE3\x81\xAF"          /* 今日は */
#define II_KANA "\xE3\x81\x84\xE3\x81\x84"                      /* いい */
#define II_KANJI "\xE8\x89\xAF\xE3\x81\x84"                     /* 良い */
#define TENKI_KANA "\xE3\x81\xA6\xE3\x82\x93\xE3\x81\x8D"       /* てんき */
#define DESUNE "\xE3\x81\xA7\xE3\x81\x99\xE3\x81\xAD"           /* ですね */
#define TENKI_KANJI "\xE5\xA4\xA9\xE6\xB0\x97"                  /* 天気 */

/* きょうは — the reading of 今日は. */
#define KYOU_HA_KANA "\xE3\x81\x8D\xE3\x82\x87\xE3\x81\x86\xE3\x81\xAF"

/*
 * What the client saw. Same shape as engine_hangul_test.c's: callbacks append
 * in the order they arrive, so the dispatch order the header fixes — every
 * deletion before any commit, composition_changed always last — is checkable
 * rather than assumed.
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

/*
 * Press one printable US-QWERTY key: keysym and layout_key are the same for an
 * unshifted ASCII key.
 *
 * Which of the two anthy consults depends on the typing method, and that is
 * not a wrinkle but the point of having both. Romaji entry reads the
 * *character*, because the user is spelling; PATHIME_ANTHY_TYPING_KANA reads
 * the *position*, because the user is striking a kana whose legend the
 * client's keymap cannot report. A client supplies both and neither mode has
 * to ask what kind of keyboard is attached.
 */
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

/*
 * Press a shifted key, given the *unshifted* US-QWERTY character on it.
 *
 * This is the shape the public header fixes: layout_key is the key unmodified,
 * the shift state is in modifiers, and an engine that cares about position
 * recombines the two itself. So Shift-2 is layout_key '2' with PATHIME_MOD_SHIFT
 * — which the kana table reads as the '@' position — and keysym is the '@' the
 * client's layout actually produced.
 */
static bool press_shift(pathime_context_t *ctx, char unshifted)
{
    static const char kUnshifted[] = "`1234567890-=[]\\;',./";
    static const char kShifted[]   = "~!@#$%^&*()_+{}|:\"<>?";
    pathime_key_event_t event;
    bool handled = false;
    const char *p = strchr(kUnshifted, unshifted);
    uint32_t shifted;

    if (p != NULL && *p != '\0') {
        shifted = (uint32_t)(unsigned char)kShifted[p - kUnshifted];
    } else if (unshifted >= 'a' && unshifted <= 'z') {
        shifted = (uint32_t)(unshifted - 'a' + 'A');
    } else {
        shifted = (uint32_t)(unsigned char)unshifted;
    }

    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.keysym = shifted;
    event.layout_key = (uint32_t)(unsigned char)unshifted;
    event.modifiers = PATHIME_MOD_SHIFT;
    PT_CHECK_STATUS(pathime_context_process_key(ctx, &event, &handled), PATHIME_OK);
    return handled;
}

/* Type a romaji spelling one key at a time. Every key must be accepted: the
 * front end consumes printable ASCII and this is all printable ASCII. */
static void type(pathime_context_t *ctx, const char *romaji)
{
    for (; *romaji != '\0'; romaji++) {
        PT_CHECK(press(ctx, (uint32_t)(unsigned char)*romaji));
    }
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

/* Candidate at @a index, as a C string, or "" if it could not be fetched. */
static const char *candidate_of(pathime_context_t *ctx, size_t index)
{
    pathime_str_t cand;
    if (pathime_context_candidate(ctx, index, &cand) != PATHIME_OK) return "";
    return cand.bytes;
}

/* A focused context with the standard callback table. */
static pathime_context_t *open_context(pathime_engine_t *engine,
                                       pathime_client_t *client,
                                       client_log_t *log)
{
    pathime_context_t *ctx = NULL;

    log_reset(log);
    memset(client, 0, sizeof(*client));
    client->struct_size = sizeof(*client);
    client->commit_text = on_commit;
    client->delete_surrounding_text = on_delete;
    client->composition_changed = on_changed;

    PT_CHECK_STATUS(pathime_context_create(engine, client, log, &ctx), PATHIME_OK);
    PT_CHECK(ctx != NULL);
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, true), PATHIME_OK);
    return ctx;
}

/* ---------------------------------------------------------------------- */

/*
 * The romaji front end, which is a state machine and not a table lookup. Every
 * case below is one where a key cannot be resolved until the next one arrives,
 * which is exactly what makes it one.
 */
static void test_romaji_state_machine(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = open_context(engine, &client, &log);

    /*
     * "nihon". The trailing n stays an n: one more key still decides whether it
     * is ん or な, so the display must not commit to either. That distinction —
     * display() showing "n", commit_text() finishing it to ん — is the whole
     * reason the composer has three accessors instead of two.
     */
    type(ctx, "nihon");
    check_str("nihon", preedit_of(ctx), NIHON_PENDING);
    PT_CHECK_SIZE(pathime_context_composition(ctx)->preedit.len, 7);
    /* Nothing is settled while typing: the kana buffer is one span, all of it
     * still mutable, and anthy has not been asked about it yet. */
    PT_CHECK_SIZE(pathime_context_composition(ctx)->preedit_settled, 0);
    PT_CHECK_SIZE(pathime_context_composition(ctx)->candidate_count, 0);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    /* A doubled consonant is the sokuon plus a still-pending consonant: the
     * second k has not been used up, it has only been shown to be doubled. */
    type(ctx, "kk");
    check_str("kk", preedit_of(ctx), SMALL_TSU "k");
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    /* "nn" is the explicit way to finish ん... */
    type(ctx, "nn");
    check_str("nn", preedit_of(ctx), N);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    /* ...and a following consonant is the implicit way. The n resolves without
     * the k being consumed, so the k pends in its turn. */
    type(ctx, "nk");
    check_str("n before a consonant", preedit_of(ctx), N "k");
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    /* Three-key and two-key digraphs: "sh" and "ts" are prefixes of nothing
     * else, so they hold rather than resolving to さ-row or た-row kana. */
    type(ctx, "shi");
    check_str("shi", preedit_of(ctx), SHI);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    type(ctx, "tsu");
    check_str("tsu", preedit_of(ctx), TSU);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    /*
     * Backspace has two granularities and the order matters: pending Latin
     * first, because the user typed it one key at a time and it is not kana
     * yet, then whole kana.
     */
    type(ctx, "nihon");
    PT_CHECK(press(ctx, PATHIME_KEY_BACKSPACE));
    check_str("backspace eats the pending n", preedit_of(ctx), NI HO);
    PT_CHECK(press(ctx, PATHIME_KEY_BACKSPACE));
    check_str("backspace eats one kana", preedit_of(ctx), NI);
    PT_CHECK(press(ctx, PATHIME_KEY_BACKSPACE));
    check_str("backspace empties the buffer", preedit_of(ctx), "");

    /* With nothing left to consume the key is declined, so the client's own
     * backspace still reaches its document. */
    PT_CHECK(!press(ctx, PATHIME_KEY_BACKSPACE));
    PT_CHECK_SIZE(strlen(log.commits), 0);

    pathime_context_destroy(ctx);
}

/*
 * Conversion: Space hands the kana to anthy and a candidate list appears. This
 * is the first point in the stack where anthy itself has been called at all.
 */
static void test_conversion_candidates(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = open_context(engine, &client, &log);
    const pathime_composition_t *c = NULL;
    size_t i;

    /* Space with an empty buffer is declined rather than absorbed, so the
     * client inserts its own space instead of a full-width one nobody asked
     * for. */
    PT_CHECK(!press(ctx, ' '));

    type(ctx, "kanji");
    check_str("kanji reading", preedit_of(ctx), KANJI_KANA);
    PT_CHECK_SIZE(pathime_context_composition(ctx)->candidate_count, 0);

    log_reset(&log);
    PT_CHECK(press(ctx, ' '));
    check_str("space converts", preedit_of(ctx), KANJI);

    /* One composition_changed and no commit: conversion is still preedit. */
    PT_CHECK(log.changed_count == 1);
    PT_CHECK(log.commit_count == 0);
    check_str("callback order on convert", log.order, "x");

    c = pathime_context_composition(ctx);
    PT_CHECK(c->candidate_count > 0);
    /* Still nothing settled: the whole conversion is one active span until a
     * selection settles it. */
    PT_CHECK_SIZE(c->preedit_settled, 0);

    /* The first two entries are anthy's own ranking for this reading. Only the
     * head of the list is pinned — see the file comment on learning. */
    check_str("best candidate", candidate_of(ctx, 0), KANJI);
    check_str("second candidate", candidate_of(ctx, 1), KANJI_2ND);

    /* Every advertised index really is fetchable, and every one is non-empty.
     * This is the promise pathime_context_candidate() makes about the whole
     * range [0, candidate_count), not just the entries a test names. */
    for (i = 0; i < c->candidate_count; i++) {
        pathime_str_t cand;
        PT_CHECK_STATUS(pathime_context_candidate(ctx, i, &cand), PATHIME_OK);
        PT_CHECK(cand.len > 0);
        PT_CHECK(cand.bytes[cand.len] == '\0');
    }

    /* And one past the end is a rejection, not a crash or an empty string. */
    {
        pathime_str_t cand;
        PT_CHECK_STATUS(pathime_context_candidate(ctx, c->candidate_count, &cand),
                        PATHIME_ERROR_INVALID_ARGUMENT);
        PT_CHECK_STATUS(pathime_context_select_candidate(ctx, c->candidate_count),
                        PATHIME_ERROR_INVALID_ARGUMENT);
    }

    pathime_context_destroy(ctx);
}

/*
 * The multi-segment projection — the case this file is really for.
 *
 * "kyouhaiitenkidesune" reads きょうはいいてんきですね and anthy segments it
 * into three. The API exposes one span at a time, so what the client sees is
 * the whole sentence in the preedit with preedit_settled marking how much of it
 * the engine has stopped revisiting. Selecting walks that boundary rightward,
 * one segment per selection, and never back: greedy left-to-right resolution.
 *
 * Every character here is three bytes in UTF-8, which is the point. Each
 * PT_CHECK_SIZE on preedit_settled below would read exactly 3x higher if the
 * library were counting bytes, and every one of them is paired with the
 * preedit.len it would have been confused with.
 */
static void test_multi_segment_settled_boundary(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = open_context(engine, &client, &log);
    const pathime_composition_t *c = NULL;
    size_t candidates_before = 0;
    char first_candidate[64];

    type(ctx, "kyouhaiitenkidesune");
    check_str("sentence reading", preedit_of(ctx),
              KYOU_HA_KANA II_KANA TENKI_KANA DESUNE);

    PT_CHECK(press(ctx, ' '));

    /*
     * After conversion the preedit is the whole sentence — the first segment
     * converted, the rest still at their readings, because the user has not
     * reached them and committing something they were never shown would be
     * worse than showing the reading.
     */
    c = pathime_context_composition(ctx);
    check_str("converted sentence", c->preedit.bytes,
              KYOU_HA II_KANA TENKI_KANA DESUNE);
    /* Nothing is settled yet: 12 characters on screen, 33 bytes, 0 settled. */
    PT_CHECK_SIZE(c->preedit_settled, 0);
    PT_CHECK_SIZE(c->preedit.len, 33);
    PT_CHECK(c->candidate_count > 0);
    check_str("first segment's best", candidate_of(ctx, 0), KYOU_HA);

    candidates_before = c->candidate_count;
    strcpy(first_candidate, candidate_of(ctx, 0));

    /* --- selection 1: settle 今日は ------------------------------------- */
    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_select_candidate(ctx, 0), PATHIME_OK);

    c = pathime_context_composition(ctx);
    /*
     * 今日は is three characters and nine bytes. preedit_settled must be 3.
     * If this ever reads 9, the projection is counting bytes and every client
     * that highlights the settled prefix will underline three times too much.
     */
    PT_CHECK_SIZE(c->preedit_settled, 3);
    PT_CHECK_SIZE(c->preedit.len, 33);
    /* The preedit still spans the whole sentence — settling text does not
     * commit it. The active segment now shows its own best conversion rather
     * than its reading, which is what becoming active means. */
    check_str("preedit after first selection", c->preedit.bytes,
              KYOU_HA II_KANJI TENKI_KANA DESUNE);

    /* A fresh list arrived, describing the *new* active span and nothing else.
     * Positions in the old list are obsolete, which is why the header says so
     * in composition_changed's own documentation. */
    PT_CHECK(c->candidate_count > 0);
    check_str("second segment's best", candidate_of(ctx, 0), II_KANJI);
    PT_CHECK(strcmp(candidate_of(ctx, 0), first_candidate) != 0);
    PT_CHECK(c->candidate_count != candidates_before);

    /* The client was told exactly once, and nothing was committed. */
    PT_CHECK(log.changed_count == 1);
    PT_CHECK(log.commit_count == 0);
    PT_CHECK_SIZE(log.last_settled, 3);

    /* --- selection 2: settle 良い --------------------------------------- */
    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_select_candidate(ctx, 0), PATHIME_OK);

    c = pathime_context_composition(ctx);
    /* 今日は良い: five characters, fifteen bytes. */
    PT_CHECK_SIZE(c->preedit_settled, 5);
    PT_CHECK_SIZE(c->preedit.len, 30);
    check_str("preedit after second selection", c->preedit.bytes,
              KYOU_HA II_KANJI TENKI_KANJI DESUNE);
    PT_CHECK(c->candidate_count > 0);
    check_str("third segment's best", candidate_of(ctx, 0), TENKI_KANJI DESUNE);
    PT_CHECK(log.commit_count == 0);

    /* --- selection 3: nothing remains, so the engine commits ------------- */
    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_select_candidate(ctx, 0), PATHIME_OK);

    /*
     * Selecting the last segment leaves nothing unsettled, and the header's
     * rule is that the engine commits when nothing remains. What it commits is
     * exactly what was on screen.
     */
    PT_CHECK(log.commit_count == 1);
    check_str("sentence commit", log.commits,
              KYOU_HA II_KANJI TENKI_KANJI DESUNE);
    /* commit_text before composition_changed, always — the ordering the
     * header fixes so a client never renders a stale preedit over new text. */
    check_str("callback order across the commit", log.order, "cx");

    c = pathime_context_composition(ctx);
    check_str("preedit after commit", c->preedit.bytes, "");
    PT_CHECK_SIZE(c->preedit_settled, 0);
    PT_CHECK_SIZE(c->candidate_count, 0);

    /* With nothing composing there is no span to select from. */
    PT_CHECK_STATUS(pathime_context_select_candidate(ctx, 0),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    pathime_context_destroy(ctx);
}

/*
 * Return and Escape: the two ways a composition ends without the user walking
 * every segment. Return means "take what is on screen", Escape means "put it
 * back the way it was".
 */
static void test_commit_and_cancel(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = open_context(engine, &client, &log);

    /*
     * Return with no conversion asked for commits the kana — and finishes the
     * pending n while doing it. "nihon" then Return is にほん and not にほn:
     * committing is the key that decides what the n was.
     */
    type(ctx, "nihon");
    check_str("preedit still shows the pending n", preedit_of(ctx), NIHON_PENDING);
    log_reset(&log);
    PT_CHECK(press(ctx, PATHIME_KEY_RETURN));
    PT_CHECK(log.commit_count == 1);
    check_str("return commits finished kana", log.commits, NIHON);
    check_str("callback order", log.order, "cx");
    check_str("preedit after return", preedit_of(ctx), "");

    /* Return on an empty composition is declined: the client's own Return must
     * still insert a newline. */
    PT_CHECK(!press(ctx, PATHIME_KEY_RETURN));

    /* Return mid-conversion commits the chosen text, not the reading. */
    type(ctx, "kanji");
    PT_CHECK(press(ctx, ' '));
    log_reset(&log);
    PT_CHECK(press(ctx, PATHIME_KEY_RETURN));
    PT_CHECK(log.commit_count == 1);
    check_str("return commits the conversion", log.commits, KANJI);
    check_str("preedit after converted return", preedit_of(ctx), "");

    /*
     * Escape cancels the conversion back to the kana buffer rather than
     * discarding it. The buffer never went anywhere — the romaji composer still
     * holds it — so the user lands somewhere they recognize and can retype from.
     */
    type(ctx, "kanji");
    PT_CHECK(press(ctx, ' '));
    check_str("converted", preedit_of(ctx), KANJI);
    PT_CHECK(pathime_context_composition(ctx)->candidate_count > 0);

    log_reset(&log);
    PT_CHECK(press(ctx, PATHIME_KEY_ESCAPE));
    check_str("escape returns to the kana", preedit_of(ctx), KANJI_KANA);
    /* Nothing was committed, and the candidate list went with the conversion. */
    PT_CHECK(log.commit_count == 0);
    PT_CHECK_SIZE(pathime_context_composition(ctx)->candidate_count, 0);

    /* A second Escape now discards the kana, since there is no conversion left
     * to fall back to. */
    PT_CHECK(press(ctx, PATHIME_KEY_ESCAPE));
    check_str("second escape discards", preedit_of(ctx), "");
    PT_CHECK(log.commit_count == 0);

    /* And with nothing composing, Escape belongs to the client again. */
    PT_CHECK(!press(ctx, PATHIME_KEY_ESCAPE));

    pathime_context_destroy(ctx);
}

/*
 * Options. Three of them, chosen because each one is implemented somewhere
 * different: the kana script is a projection in this library's own front end,
 * the candidate cap is the core's materialization pump, and the typing method
 * is a documented gap.
 */
static void test_options(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = open_context(engine, &client, &log);
    const pathime_composition_t *c = NULL;
    char head[8][64];
    size_t capped = 0;
    size_t i;

    /* --- PATHIME_OPT_ANTHY_KANA_SCRIPT ---------------------------------- */
    {
        int64_t script = -1;
        PT_CHECK_STATUS(pathime_context_get_option_int(ctx, PATHIME_OPT_ANTHY_KANA_SCRIPT,
                                                       &script),
                        PATHIME_OK);
        PT_CHECK(script == PATHIME_ANTHY_SCRIPT_HIRAGANA);
    }

    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_ANTHY_KANA_SCRIPT,
                                                   PATHIME_ANTHY_SCRIPT_KATAKANA),
                    PATHIME_OK);
    type(ctx, "nihon");
    /*
     * Katakana, with the pending n still Latin. The script is a projection
     * applied on the way out — the composer's own buffer stays hiragana, since
     * that is what anthy has to be fed — so this checks the projection and not
     * a second internal representation.
     */
    check_str("katakana typing", preedit_of(ctx), KATA_NI KATA_HO "n");
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_reset_option(ctx, PATHIME_OPT_ANTHY_KANA_SCRIPT),
                    PATHIME_OK);
    type(ctx, "nihon");
    check_str("hiragana again after reset_option", preedit_of(ctx), NIHON_PENDING);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    /* --- PATHIME_OPT_MAX_CANDIDATES ------------------------------------- */

    /* Zero is rejected rather than treated as "hide the list": an engine that
     * converts by selection cannot make progress without a candidate. */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_MAX_CANDIDATES, 0),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_MAX_CANDIDATES, 3),
                    PATHIME_OK);
    type(ctx, "kanji");
    PT_CHECK(press(ctx, ' '));

    c = pathime_context_composition(ctx);
    /* The cap really caps: this reading has far more than three conversions,
     * and the unconverted-list test above saw the whole run. */
    PT_CHECK_SIZE(c->candidate_count, 3);
    capped = c->candidate_count;
    for (i = 0; i < capped; i++) {
        strcpy(head[i], candidate_of(ctx, i));
    }

    /*
     * Raising the cap mid-composition appends and does not renumber. That is
     * what makes the option composition-safe, and it is the promise a client
     * displaying a growing list depends on: index 2 must still be the same
     * candidate it was before the user scrolled.
     */
    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_MAX_CANDIDATES, 8),
                    PATHIME_OK);
    c = pathime_context_composition(ctx);
    PT_CHECK_SIZE(c->candidate_count, 8);
    /* The client is told, because the list it is rendering changed. */
    PT_CHECK(log.changed_count == 1);
    PT_CHECK_SIZE(log.last_candidates, 8);
    for (i = 0; i < capped; i++) {
        check_str("candidate kept its position", candidate_of(ctx, i), head[i]);
    }
    /* And the preedit is untouched: raising the cap is not a conversion event. */
    check_str("preedit survives the cap change", preedit_of(ctx), KANJI);

    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);
    PT_CHECK_STATUS(pathime_context_reset_option(ctx, PATHIME_OPT_MAX_CANDIDATES),
                    PATHIME_OK);

    /* --- PATHIME_OPT_ANTHY_TYPING_METHOD -------------------------------- */
    {
        pathime_option_info_t info;
        memset(&info, 0, sizeof(info));
        info.struct_size = sizeof(info);
        PT_CHECK_STATUS(pathime_engine_option_info(engine, PATHIME_OPT_ANTHY_TYPING_METHOD,
                                                   &info),
                        PATHIME_OK);
        PT_CHECK(info.supported);
        /* One of the four options in the inventory that reset the composition:
         * a pending romaji fragment has no meaning once the keys are read as
         * kana. */
        PT_CHECK(info.resets_composition);
    }

    /*
     * PATHIME_ANTHY_TYPING_KANA: one key, one kana, off the US-101 positions.
     *
     * The table is scim-anthy's 101kana — the JIS kana arrangement laid over a
     * US keyboard — so 'a' is ち and 'k' is の, which is what a JIS keycap in
     * that position reads. Nothing here is romaji: "ka" would be か if these
     * keys were being spelled, and it is ちの because they are being struck.
     */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_ANTHY_TYPING_METHOD,
                                                   PATHIME_ANTHY_TYPING_KANA),
                    PATHIME_OK);
    log_reset(&log);
    PT_CHECK(press(ctx, 'a'));
    PT_CHECK(press(ctx, 'k'));
    check_str("kana entry types kana directly", preedit_of(ctx), CHI NO_KANA);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    /*
     * The dakuten key folds into the kana before it. 't' is か and Shift-2
     * reaches the '@' position, which types ゛— so the pair is が, one
     * character rather than two.
     */
    PT_CHECK(press(ctx, 't'));
    check_str("kana: t is か", preedit_of(ctx), KA);
    PT_CHECK(press_shift(ctx, '2'));
    check_str("kana: か plus dakuten is が", preedit_of(ctx), GA);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    /* Handakuten likewise, on the '[' position: 'f' is は, so は゜ is ぱ. */
    PT_CHECK(press(ctx, 'f'));
    PT_CHECK(press(ctx, '['));
    check_str("kana: は plus handakuten is ぱ", preedit_of(ctx), PA);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    /*
     * A mark after a kana that takes none stands alone rather than being
     * dropped, which is ibus-anthy's behaviour: its append() falls through to
     * a new segment built from the same key. 'e' is い, which has no voiced
     * form, so the ゛stays visible as its own character.
     */
    PT_CHECK(press(ctx, 'e'));
    PT_CHECK(press_shift(ctx, '2'));
    check_str("kana: mark alone after い", preedit_of(ctx), I_KANA VOICED_MARK);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    /* And a mark with nothing before it is simply the mark. */
    PT_CHECK(press_shift(ctx, '2'));
    check_str("kana: leading mark", preedit_of(ctx), VOICED_MARK);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    /*
     * Backspace removes one kana. There is no second granularity here the way
     * there is in romaji mode, because kana entry never leaves an unresolved
     * prefix — every key resolves on the spot.
     */
    PT_CHECK(press(ctx, 't'));
    PT_CHECK(press_shift(ctx, '2'));
    check_str("kana/bs: が", preedit_of(ctx), GA);
    PT_CHECK(press(ctx, PATHIME_KEY_BACKSPACE));
    check_str("kana/bs: が removed whole", preedit_of(ctx), "");
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    /*
     * And the whole point: kana typed this way converts. The front end feeds
     * anthy the same hiragana whichever method produced it, so everything
     * downstream — segmentation, the candidate list, commit — is the machinery
     * the romaji cases above already exercised.
     *
     * かんじ off the kana positions is t-y-d-Shift2: か, ん, し, and the
     * dakuten that turns し into じ. The same reading "kanji" spells out in
     * romaji earlier in this file, and it converts to the same 漢字 — which is
     * the assertion, because it shows the typing method chooses only how the
     * reading is entered and nothing about what happens to it afterwards.
     */
    PT_CHECK(press(ctx, 't'));
    PT_CHECK(press(ctx, 'y'));
    PT_CHECK(press(ctx, 'd'));
    PT_CHECK(press_shift(ctx, '2'));
    check_str("kana: かんじ typed as kana", preedit_of(ctx), KANJI_KANA);
    PT_CHECK(press(ctx, PATHIME_KEY_SPACE));
    check_str("kana: converts to 漢字", preedit_of(ctx), KANJI);
    PT_CHECK(pathime_context_composition(ctx)->candidate_count > 1);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    /* Switching back restores the romaji machine, so the mode is confined to
     * that one value rather than latching. */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_ANTHY_TYPING_METHOD,
                                                   PATHIME_ANTHY_TYPING_ROMAJI),
                    PATHIME_OK);
    type(ctx, "nihon");
    check_str("romaji works again", preedit_of(ctx), NIHON_PENDING);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);

    /* --- options belonging to other engines ------------------------------ */

    /* PATHIME_OPT_PINYIN_FUZZY was widened from Pinyin to both pyzy engines
     * when the bopomofo tables were traced; it did not widen further. Anthy
     * has no pinyin spellings for those rules to be about, so it reports the
     * option unsupported — the half of that claim engine_pyzy_test.c cannot
     * check, since it has no anthy dictionary to create an engine with. */
    {
        pathime_option_info_t info;
        memset(&info, 0, sizeof(info));
        info.struct_size = sizeof(info);
        PT_CHECK_STATUS(pathime_engine_option_info(engine, PATHIME_OPT_PINYIN_FUZZY, &info),
                        PATHIME_OK);
        PT_CHECK(!info.supported);
        PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_PINYIN_FUZZY, 0),
                        PATHIME_ERROR_UNSUPPORTED);
    }

    /* And the Hangul preedit mode, for the same reason in the other direction. */
    PT_CHECK_STATUS(pathime_context_set_option_int(ctx, PATHIME_OPT_HANGUL_PREEDIT,
                                                   PATHIME_HANGUL_PREEDIT_WORD),
                    PATHIME_ERROR_UNSUPPORTED);

    pathime_context_destroy(ctx);
}

/*
 * Focus and reset, which behave the same for every engine but are worth
 * checking on the one whose state is deepest: mid-conversion there is a live
 * anthy context behind the composition, not just a string.
 */
static void test_reset_and_focus(pathime_engine_t *engine)
{
    client_log_t log;
    pathime_client_t client;
    pathime_context_t *ctx = open_context(engine, &client, &log);

    /* Focus gates input and only input. */
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, false), PATHIME_OK);
    {
        pathime_key_event_t event;
        bool handled = true;
        memset(&event, 0, sizeof(event));
        event.struct_size = sizeof(event);
        event.keysym = 'a';
        PT_CHECK_STATUS(pathime_context_process_key(ctx, &event, &handled),
                        PATHIME_ERROR_NOT_FOCUSED);
        PT_CHECK(handled == false);
    }
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, true), PATHIME_OK);

    /* Reset mid-conversion discards without committing — an engine that must
     * preserve text would have to commit it explicitly, and cancelling a
     * conversion is not that. */
    type(ctx, "kanji");
    PT_CHECK(press(ctx, ' '));
    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);
    check_str("preedit after reset", preedit_of(ctx), "");
    PT_CHECK_SIZE(strlen(log.commits), 0);
    PT_CHECK(log.changed_count == 1);
    PT_CHECK_SIZE(pathime_context_composition(ctx)->candidate_count, 0);

    /* Resetting an already-empty composition dispatches nothing. */
    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_reset(ctx), PATHIME_OK);
    PT_CHECK(log.changed_count == 0);

    /*
     * Focus loss neither commits nor discards, and this is the interesting
     * case: the state preserved is a live anthy conversion with a segment
     * index, not merely a preedit string.
     */
    type(ctx, "kanji");
    PT_CHECK(press(ctx, ' '));
    log_reset(&log);
    PT_CHECK_STATUS(pathime_context_set_focused(ctx, false), PATHIME_OK);
    PT_CHECK(log.changed_count == 0);
    PT_CHECK_SIZE(strlen(log.commits), 0);
    check_str("conversion survives focus loss", preedit_of(ctx), KANJI);
    PT_CHECK(pathime_context_composition(ctx)->candidate_count > 0);

    PT_CHECK_STATUS(pathime_context_set_focused(ctx, true), PATHIME_OK);
    PT_CHECK(log.changed_count == 0);

    /* And the conversion resumes: Return still commits what was on screen. */
    PT_CHECK(press(ctx, PATHIME_KEY_RETURN));
    check_str("conversion resumes after refocus", log.commits, KANJI);

    pathime_context_destroy(ctx);
}

int main(void)
{
    pathime_engine_t *engine = NULL;
    pathime_init_params_t params;

    /*
     * The library's own persistent-storage surface, pointed at the build tree.
     * anthy_global_init() turns this into anthy_conf_override("XDG_CONFIG_HOME",
     * ...), which is what moves the learned record file and the personal
     * dictionary — so this run cannot touch the developer's ~/.config/anthy
     * even if the environment says otherwise.
     */
    memset(&params, 0, sizeof(params));
    params.struct_size = sizeof(params);
    params.data_dir = ANTHY_TEST_HOME;
    PT_CHECK_STATUS(pathime_init(&params), PATHIME_OK);

    /*
     * Not a conditional skip. Unlike libhangul, anthy has a runtime
     * prerequisite that can fail — but the CONFFILE this test is run with is
     * exactly what makes it succeed, so a false here means the wiring in
     * tests/api/CMakeLists.txt is broken and the right response is a failure,
     * not a quietly green run.
     */
    PT_CHECK(pathime_has_engine(PATHIME_ENGINE_ANTHY));
    PT_CHECK_STATUS(pathime_engine_create(PATHIME_ENGINE_ANTHY, &engine), PATHIME_OK);
    PT_CHECK(engine != NULL);

    if (engine != NULL) {
        PT_CHECK(pathime_engine_id(engine) == PATHIME_ENGINE_ANTHY);

        /* Nothing in this adapter uses the surrounding-text surface: reconversion
        * is the only route to it and anthy_set_reconversion_mode() is left at its
        * default. */
        PT_CHECK(pathime_engine_requirements(engine) == 0);

        test_romaji_state_machine(engine);
        test_conversion_candidates(engine);
        test_multi_segment_settled_boundary(engine);
        test_commit_and_cancel(engine);
        test_options(engine);
        test_reset_and_focus(engine);

        pathime_engine_destroy(engine);
    }

    pathime_shutdown();
    return pt_report("api.engine_anthy");
}

#endif /* PATHIME_WITH_ANTHY */
