/*
 * libhangul input-context / automata tests.
 *
 * Why this exists when engines/libhangul/test/test.c already covers the same ground:
 * that file is written against the Check framework and against a 32-bit
 * wchar_t (see hangul_test_util.h). Check is not present on a stock Windows
 * host and the wchar_t assumption is wrong there even when it is, so the whole
 * of upstream's automata coverage would simply be absent on the platform we
 * most need to verify. This is the same set of expectations, expressed in
 * plain C11 against ucschar, so that Linux and Windows are held to one
 * standard.
 *
 * The expected values are transcribed from engines/libhangul/test/test.c as code
 * points. The source is kept pure ASCII on purpose: MSVC interprets narrow and
 * wide literals in a non-BOM file using the host ANSI code page unless it is
 * told otherwise, so a file containing Hangul would mean different things to
 * different compilers. Comments name the characters instead.
 */
#include <stdlib.h>

#include "hangul_test_util.h"

/* One shared context, as upstream does, because the ic options under test are
 * per-context state and several cases deliberately observe them persisting. */
static HangulInputContext* g_ic = NULL;

static HangulInputContext* get_ic(const char* keyboard)
{
    if (g_ic == NULL)
        g_ic = hangul_ic_new("2");
    hangul_ic_select_keyboard(g_ic, keyboard);
    hangul_ic_reset(g_ic);
    return g_ic;
}

static void set_option(int option, bool value)
{
    hangul_ic_set_option(get_ic("2"), option, value);
}

static bool get_option(int option)
{
    return hangul_ic_get_option(get_ic("2"), option);
}

/* Feed an ASCII key sequence to a freshly reset context on <keyboard>.
 * '\b' is a real backspace to hangul_ic_process(), which is why the inputs
 * below can carry them inline exactly as upstream's do. */
static void feed(const char* keyboard, const char* input)
{
    const char* p;
    HangulInputContext* ic = get_ic(keyboard);
    for (p = input; *p != '\0'; p++)
        hangul_ic_process(ic, *p);
}

static const ucschar* preedit(const char* keyboard, const char* input)
{
    feed(keyboard, input);
    return hangul_ic_get_preedit_string(g_ic);
}

static const ucschar* commit(const char* keyboard, const char* input)
{
    feed(keyboard, input);
    return hangul_ic_get_commit_string(g_ic);
}

#define CHECK_PREEDIT(kb, in, ...) \
    HT_CHECK_UCS("preedit " kb " \"" in "\"", preedit(kb, in), UCS(__VA_ARGS__))
#define CHECK_COMMIT(kb, in, ...) \
    HT_CHECK_UCS("commit " kb " \"" in "\"", commit(kb, in), UCS(__VA_ARGS__))

/* --- 2-beolsik (the standard keyboard) ---------------------------------- */
static void test_keyboard_2(void)
{
    /* Defaults as set by hangul_ic_new(): no auto-reorder, no combi on double
     * stroke, non-choseong combi on. Set them explicitly so this block does
     * not depend on the order the other blocks run in. */
    set_option(HANGUL_IC_OPTION_AUTO_REORDER, false);
    set_option(HANGUL_IC_OPTION_COMBI_ON_DOUBLE_STROKE, false);
    set_option(HANGUL_IC_OPTION_NON_CHOSEONG_COMBI, true);

    CHECK_COMMIT("2", "rkW", 0xAC00);   /* GA */
    CHECK_PREEDIT("2", "rkW", 0x3149);  /* compat JJ */
    CHECK_COMMIT("2", "qjTm", 0xBC84);  /* BEO */
    CHECK_PREEDIT("2", "qjTm", 0xC4F0); /* SSEU */

    CHECK_PREEDIT("2", "akfr", 0xB9D1);  /* MALG */
    CHECK_COMMIT("2", "akfrh", 0xB9D0);  /* MAL */
    CHECK_PREEDIT("2", "akfrh", 0xACE0); /* GO */

    CHECK_PREEDIT("2", "rt", 0x3133);   /* compat GS */
    CHECK_COMMIT("2", "rtk", 0x3131);   /* compat G */
    CHECK_PREEDIT("2", "rtk", 0xC0AC);  /* SA */

    /* backspace unwinds one jamo, not one syllable */
    CHECK_PREEDIT("2", "rkT\b", 0xAC00);  /* GA */
    CHECK_PREEDIT("2", "rt\bk", 0xAC00);  /* GA */
    CHECK_PREEDIT("2", "akfr\b", 0xB9D0); /* MAL */
    CHECK_PREEDIT("2", "dnp\b", 0xC6B0);  /* U */
    HT_CHECK_UCS("preedit 2 backspaced empty",
                 preedit("2", "qqnpfr\b\b\b\b\b\b"), UCS_EMPTY);
    HT_CHECK_UCS("preedit 2 backspaced empty (shifted)",
                 preedit("2", "Qnpfr\b\b\b\b\b"), UCS_EMPTY);
}

/* --- 2-beolsik yetgeul: exercises the old-Hangul jamo path -------------- */
static void test_keyboard_2y(void)
{
    bool saved;

    CHECK_PREEDIT("2y", "g", 0x314E); /* compat H */
    CHECK_PREEDIT("2y", "h", 0x3157); /* compat O */
    CHECK_PREEDIT("2y", "x", 0x314C); /* compat T */
    CHECK_PREEDIT("2y", "qd", 0x3178);
    CHECK_PREEDIT("2y", "Z", 0x113C, 0x1160);
    CHECK_PREEDIT("2y", "V", 0x1150, 0x1160);
    CHECK_PREEDIT("2y", "sg", 0x115D, 0x1160);

    CHECK_PREEDIT("2y", "rkd", 0xAC15); /* GANG */
    CHECK_PREEDIT("2y", "fo", 0xB798);  /* RAE */
    CHECK_PREEDIT("2y", "gKs", 0x1112, 0x119E, 0x11AB);
    CHECK_PREEDIT("2y", "QdhaT", 0x112C, 0x1169, 0x11DE);

    CHECK_COMMIT("2y", "Qdhatty", 0x112C, 0x1169, 0x11DD);
    CHECK_PREEDIT("2y", "Qdhatty", 0xC1FC); /* SYO */
    CHECK_COMMIT("2y", "QdhaTy", 0x112C, 0x1169, 0x11B7);
    CHECK_PREEDIT("2y", "QdhaTy", 0xC448); /* SSYO */

    /* yesieung */
    CHECK_PREEDIT("2y", "rkDD", 0x1100, 0x1161, 0x11EE);
    CHECK_COMMIT("2y", "rkDDk", 0x1100, 0x1161, 0x11F0);
    CHECK_PREEDIT("2y", "rkDDk", 0x114C, 0x1161);

    saved = get_option(HANGUL_IC_OPTION_COMBI_ON_DOUBLE_STROKE);
    set_option(HANGUL_IC_OPTION_COMBI_ON_DOUBLE_STROKE, true);

    CHECK_PREEDIT("2y", "qqdhatt", 0x112C, 0x1169, 0x11DE);
    CHECK_COMMIT("2y", "qqdhatty", 0x112C, 0x1169, 0x11DD);
    CHECK_PREEDIT("2y", "qqdhatty", 0xC1FC); /* SYO */
    CHECK_COMMIT("2y", "qqdhaTy", 0x112C, 0x1169, 0x11B7);
    CHECK_PREEDIT("2y", "qqdhaTy", 0xC448); /* SSYO */

    CHECK_COMMIT("2y", "ddkdd", 0x1147, 0x1161, 0x11BC);
    CHECK_PREEDIT("2y", "ddkdd", 0x3147); /* compat NG */

    CHECK_PREEDIT("2y", "kkkk", 0x115F, 0x11A2);

    set_option(HANGUL_IC_OPTION_COMBI_ON_DOUBLE_STROKE, saved);
}

/* --- 3-beolsik variants: the jaso (not jamo) code path ------------------ */
static void test_keyboard_3(void)
{
    CHECK_PREEDIT("3f", "m", 0x314E); /* compat H */
    CHECK_PREEDIT("3f", "v", 0x3157); /* compat O */
    CHECK_PREEDIT("3f", "W", 0x314C); /* compat T */

    CHECK_PREEDIT("3f", "kfa", 0xAC15); /* GANG */
    CHECK_PREEDIT("3f", "yr", 0xB798);  /* RAE */
    CHECK_PREEDIT("3f", "hz", 0x1102, 0x1160, 0x11B7);
    CHECK_PREEDIT("3f", "tq", 0x115F, 0x1165, 0x11BA);

    CHECK_PREEDIT("3s", "mrqq", 0xD588); /* HAESS */
}

/* --- romaja: the transliteration path, driven by the raw ascii ---------- */
static void test_keyboard_romaja(void)
{
    HangulInputContext* ic = hangul_ic_new("ro");

    HT_CHECK(ic != NULL);
    if (ic == NULL)
        return;
    HT_CHECK(hangul_ic_is_transliteration(ic));

    /* "han" -> HAN */
    hangul_ic_process(ic, 'h');
    hangul_ic_process(ic, 'a');
    hangul_ic_process(ic, 'n');
    HT_CHECK_UCS("romaja han preedit",
                 hangul_ic_get_preedit_string(ic), UCS(0xD55C));
    HT_CHECK_UCS("romaja han commit",
                 hangul_ic_get_commit_string(ic), UCS_EMPTY);

    hangul_ic_reset(ic);

    /* a bare vowel gets an ieung inserted, and backspace removes both */
    hangul_ic_process(ic, 'a');
    HT_CHECK_UCS("romaja a preedit",
                 hangul_ic_get_preedit_string(ic), UCS(0xC544)); /* A */
    hangul_ic_backspace(ic);
    HT_CHECK_UCS("romaja a backspaced",
                 hangul_ic_get_preedit_string(ic), UCS_EMPTY);

    /* a syllable that does not end in a vowel gets EU appended */
    hangul_ic_process(ic, 't');
    hangul_ic_process(ic, 't');
    HT_CHECK_UCS("romaja tt preedit",
                 hangul_ic_get_preedit_string(ic), UCS(0x314C)); /* compat T */
    HT_CHECK_UCS("romaja tt commit",
                 hangul_ic_get_commit_string(ic), UCS(0xD2B8)); /* TEU */

    /* "gang" -> GANG, then "i" splits it into GANG + I */
    hangul_ic_reset(ic);
    hangul_ic_process(ic, 'g');
    hangul_ic_process(ic, 'a');
    hangul_ic_process(ic, 'n');
    hangul_ic_process(ic, 'g');
    HT_CHECK_UCS("romaja gang preedit",
                 hangul_ic_get_preedit_string(ic), UCS(0xAC15));
    hangul_ic_process(ic, 'i');
    HT_CHECK_UCS("romaja gangi preedit",
                 hangul_ic_get_preedit_string(ic), UCS(0xC774)); /* I */
    HT_CHECK_UCS("romaja gangi commit",
                 hangul_ic_get_commit_string(ic), UCS(0xAC15)); /* GANG */

    /* an uppercase letter always starts a new syllable */
    hangul_ic_process(ic, 'n');
    hangul_ic_process(ic, 'a');
    hangul_ic_process(ic, 'n');
    hangul_ic_process(ic, 'G');
    HT_CHECK_UCS("romaja nanG preedit",
                 hangul_ic_get_preedit_string(ic), UCS(0x3131)); /* compat G */
    HT_CHECK_UCS("romaja nanG commit",
                 hangul_ic_get_commit_string(ic), UCS(0xB09C)); /* NAN */

    /* 'x' is special-cased: J as a leading consonant, GS as a trailing one */
    hangul_ic_reset(ic);
    hangul_ic_process(ic, 'x');
    hangul_ic_process(ic, 'x');
    HT_CHECK_UCS("romaja xx preedit",
                 hangul_ic_get_preedit_string(ic), UCS(0x3148)); /* compat J */
    HT_CHECK_UCS("romaja xx commit",
                 hangul_ic_get_commit_string(ic), UCS(0xC988)); /* JEU */

    hangul_ic_reset(ic);
    hangul_ic_process(ic, 'x');
    hangul_ic_process(ic, 'y');
    HT_CHECK_UCS("romaja xy preedit",
                 hangul_ic_get_preedit_string(ic), UCS(0xC9C0)); /* JI */
    HT_CHECK_UCS("romaja xy commit",
                 hangul_ic_get_commit_string(ic), UCS_EMPTY);

    hangul_ic_reset(ic);
    hangul_ic_process(ic, 's');
    hangul_ic_process(ic, 'e');
    hangul_ic_process(ic, 'x');
    hangul_ic_process(ic, 'y');
    HT_CHECK_UCS("romaja sexy preedit",
                 hangul_ic_get_preedit_string(ic), UCS(0xC2DC)); /* SI */
    HT_CHECK_UCS("romaja sexy commit",
                 hangul_ic_get_commit_string(ic), UCS(0xC139)); /* SEK */

    hangul_ic_delete(ic);
}

/* --- per-context options ------------------------------------------------ */
static void test_option_auto_reorder(void)
{
    bool saved = get_option(HANGUL_IC_OPTION_AUTO_REORDER);

    set_option(HANGUL_IC_OPTION_AUTO_REORDER, true);
    CHECK_PREEDIT("2", "rk", 0xAC00); /* GA */
    CHECK_PREEDIT("2", "kr", 0xAC00); /* GA, jamo typed out of order */

    set_option(HANGUL_IC_OPTION_AUTO_REORDER, false);
    CHECK_PREEDIT("2", "rk", 0xAC00);
    CHECK_COMMIT("2", "kr", 0x314F);  /* compat A */
    CHECK_PREEDIT("2", "kr", 0x3131); /* compat G */

    set_option(HANGUL_IC_OPTION_AUTO_REORDER, true);
    CHECK_PREEDIT("3f", "kf", 0xAC00);
    CHECK_PREEDIT("3f", "fk", 0xAC00);

    set_option(HANGUL_IC_OPTION_AUTO_REORDER, false);
    CHECK_PREEDIT("3f", "kf", 0xAC00);
    CHECK_COMMIT("3f", "fk", 0x314F);
    CHECK_PREEDIT("3f", "fk", 0x3131);

    set_option(HANGUL_IC_OPTION_AUTO_REORDER, saved);
}

static void test_option_combi_on_double_stroke(void)
{
    bool saved = get_option(HANGUL_IC_OPTION_COMBI_ON_DOUBLE_STROKE);

    set_option(HANGUL_IC_OPTION_COMBI_ON_DOUBLE_STROKE, true);
    CHECK_PREEDIT("2", "rrkrr", 0xAE4E);  /* GGAGG */
    CHECK_PREEDIT("2", "rrkrrk", 0xAC00); /* GA */

    CHECK_PREEDIT("2", "qjtt", 0xBC98);  /* BEOSS */
    CHECK_COMMIT("2", "qjttm", 0xBC97);  /* BEOS */
    CHECK_PREEDIT("2", "qjttm", 0xC2A4); /* SEU */

    CHECK_PREEDIT("2", "rktt", 0xAC14);    /* GASS */
    CHECK_PREEDIT("2", "rktt\b", 0xAC13);  /* GAS */

    set_option(HANGUL_IC_OPTION_COMBI_ON_DOUBLE_STROKE, false);
    CHECK_COMMIT("2", "rr", 0x3131);
    CHECK_PREEDIT("2", "rr", 0x3131);
    CHECK_PREEDIT("2", "rrk", 0xAC00);
    CHECK_PREEDIT("2", "rrkr", 0xAC01);  /* GAG */
    CHECK_COMMIT("2", "rrkrr", 0xAC01);
    CHECK_PREEDIT("2", "rrkrr", 0x3131);
    CHECK_PREEDIT("2", "rrkrrk", 0xAC00);

    CHECK_COMMIT("2", "qjtt", 0xBC97);  /* BEOS */
    CHECK_PREEDIT("2", "qjtt", 0x3145); /* compat S */

    set_option(HANGUL_IC_OPTION_COMBI_ON_DOUBLE_STROKE, saved);
}

static void test_option_non_choseong_combi(void)
{
    bool saved = get_option(HANGUL_IC_OPTION_NON_CHOSEONG_COMBI);

    set_option(HANGUL_IC_OPTION_NON_CHOSEONG_COMBI, true);
    CHECK_PREEDIT("2", "rt", 0x3133);  /* compat GS */
    CHECK_COMMIT("2", "rtk", 0x3131);
    CHECK_PREEDIT("2", "rtk", 0xC0AC); /* SA */

    set_option(HANGUL_IC_OPTION_NON_CHOSEONG_COMBI, false);
    CHECK_COMMIT("2", "rt", 0x3131);
    CHECK_PREEDIT("2", "rt", 0x3145);

    set_option(HANGUL_IC_OPTION_NON_CHOSEONG_COMBI, saved);
}

/* --- lifecycle: not covered upstream, and the part most likely to differ
 * if the Windows build ever links two CRTs or two copies of the library. --- */
static void test_lifecycle(void)
{
    HangulInputContext* ic = hangul_ic_new("2");

    HT_CHECK(ic != NULL);
    if (ic == NULL)
        return;

    HT_CHECK(hangul_ic_is_empty(ic));
    HT_CHECK(!hangul_ic_has_choseong(ic));
    HT_CHECK(!hangul_ic_is_transliteration(ic));
    /* backspace on an empty context is a no-op that reports "not handled" */
    HT_CHECK(!hangul_ic_backspace(ic));

    HT_CHECK(hangul_ic_process(ic, 'r'));
    HT_CHECK(!hangul_ic_is_empty(ic));
    HT_CHECK(hangul_ic_has_choseong(ic));
    HT_CHECK(!hangul_ic_has_jungseong(ic));

    HT_CHECK(hangul_ic_process(ic, 'k'));
    HT_CHECK(hangul_ic_has_jungseong(ic));
    HT_CHECK(!hangul_ic_has_jongseong(ic));

    HT_CHECK(hangul_ic_process(ic, 's'));
    HT_CHECK(hangul_ic_has_jongseong(ic));
    HT_CHECK_UCS("preedit before flush",
                 hangul_ic_get_preedit_string(ic), UCS(0xAC04)); /* GAN */

    /* flush hands back the pending syllable and empties the context */
    HT_CHECK_UCS("flushed string", hangul_ic_flush(ic), UCS(0xAC04));
    HT_CHECK(hangul_ic_is_empty(ic));
    HT_CHECK_UCS("preedit after flush",
                 hangul_ic_get_preedit_string(ic), UCS_EMPTY);
    /* flushing an empty context yields the empty string, not NULL */
    HT_CHECK_UCS("flush of empty context", hangul_ic_flush(ic), UCS_EMPTY);

    /* reset discards rather than commits */
    hangul_ic_process(ic, 'r');
    hangul_ic_process(ic, 'k');
    hangul_ic_reset(ic);
    HT_CHECK(hangul_ic_is_empty(ic));
    HT_CHECK_UCS("preedit after reset",
                 hangul_ic_get_preedit_string(ic), UCS_EMPTY);

    hangul_ic_delete(ic);

    /* the whole API is documented NULL-tolerant; a crash here on one platform
     * only would point at a mismatched calling convention or import stub */
    HT_CHECK(!hangul_ic_process(NULL, 'r'));
    HT_CHECK(!hangul_ic_backspace(NULL));
    HT_CHECK(hangul_ic_get_preedit_string(NULL) == NULL);
    HT_CHECK(hangul_ic_get_commit_string(NULL) == NULL);
    HT_CHECK(hangul_ic_flush(NULL) == NULL);
    hangul_ic_reset(NULL);
    hangul_ic_delete(NULL);
}

int main(void)
{
    test_keyboard_2();
    test_keyboard_2y();
    test_keyboard_3();
    test_keyboard_romaja();
    test_option_auto_reorder();
    test_option_combi_on_double_stroke();
    test_option_non_choseong_combi();
    test_lifecycle();

    hangul_ic_delete(g_ic);
    return ht_report("hangul.ic");
}
