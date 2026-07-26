/* PyZy::PinyinParser and the generated pinyin table.
 *
 * This is the test that guards the riskiest thing the Windows port does. pyzy's
 * PinyinParserTable.h initialises 733 `struct Pinyin` entries with GNU labelled
 * fields:
 *
 *     { text : "a", bopomofo : L"...", sheng : "", yun : "a",
 *       pinyin_id : {...}, len : 1, flags : 0 }
 *
 * No MSVC-style compiler accepts that, so the port strips the labels and lets
 * the values fall through as ordinary aggregate initialisation. That is only
 * correct because the labels happen to appear in struct-declaration order. If a
 * future table (or a hand edit) ever breaks that ordering the rewrite still
 * compiles and the table is silently permuted — every syllable in the language
 * quietly resolves to the wrong phonetics. Nothing else in the build would
 * notice.
 *
 * So the checks below are chosen to be sensitive to permutation rather than to
 * any particular syllable:
 *
 *   1. Structural invariants over the whole table (sorted by `text`, `len`
 *      agrees with `text`, `sheng` is a prefix of `text`, ids in range). A
 *      field landing in the wrong slot violates several of these at once.
 *   2. A closed loop through data the table does not own:
 *      PinyinParser::isPinyin() turns a (sheng id, yun id) pair into a string
 *      using id_map[], which lives in PinyinParser.cc, then bsearches the table
 *      for it. Asking for every id pair and checking that what comes back
 *      carries the ids we asked for ties `pinyin_id` to `text` through an
 *      independent source.
 *   3. Round-tripping every entry through PinyinParser::parse(), which is the
 *      path the engine actually uses, against *the library's* copy of the table
 *      rather than the one this file instantiates.
 *   4. Hand-written expectations for a spread of syllables, so that a plausible
 *      whole-table transformation still has to survive values written out by a
 *      human.
 *
 * None of this needs android.db: the parser is pure table lookup.
 */
#include "pyzy_assert.h"
#include "pyzy_pinyin_table.h"   /* generated: the vendored table, in-namespace */
#include "pyzy_private.h"        /* generated: PinyinParser, PinyinArray, String */

#include <cstring>
#include <string>

using PyZy::Pinyin;
using PyZy::PinyinArray;
using PyZy::PinyinParser;
using PyZy::String;

namespace {

/* InputContext's documented default for PROPERTY_CONVERSION_OPTION. Using the
 * permissive set means incomplete syllables ("n", "zh") and the typo-correction
 * entries are all reachable, so the sweeps below cover the entire table. */
const unsigned int kAllOptions =
    PINYIN_INCOMPLETE_PINYIN | PINYIN_CORRECT_ALL | PINYIN_FUZZY_ALL;

const size_t kTableSize = sizeof (PyZy::pinyin_table) / sizeof (PyZy::pinyin_table[0]);

/* The single entry where upstream's generated data is internally inconsistent:
 * text "shon" is four characters but len says 3. It is a typo-correction entry
 * (on -> ong) and pyzy has shipped it this way for its whole life. Excluded by
 * name rather than by loosening the invariant, so that a *new* inconsistency
 * still fails. */
const char kKnownBadLenEntry[] = "shon";

/* Parse `text` as a single syllable and return the table entry it resolved to,
 * or NULL. Note this reaches into the library's copy of the table, not the copy
 * this translation unit instantiates. */
const Pinyin * parseOne (const char *text, unsigned int option)
{
    PinyinArray result;
    const String input (text);
    const size_t used = PinyinParser::parse (input, input.size (), option, result, 1);
    if (used == 0 || result.size () != 1)
        return NULL;
    return result[0].pinyin;
}

void checkTableStructure ()
{
    PYZY_EXPECT_INT ((long long) kTableSize, 733);

    for (size_t i = 0; i < kTableSize; ++i) {
        const Pinyin &e = PyZy::pinyin_table[i];

        if (e.text == NULL || e.sheng == NULL || e.yun == NULL ||
            e.bopomofo == NULL) {
            PYZY_EXPECT (!"table entry has a NULL string field");
            continue;
        }

        const std::string text (e.text);

        /* `len` is the field immediately after pinyin_id, i.e. the one most
         * likely to pick up a neighbour's value if the ordering assumption
         * breaks. It must be the length of `text`. */
        if (text != kKnownBadLenEntry)
            PYZY_EXPECT_INT ((long long) e.len, (long long) text.size ());
        PYZY_EXPECT (e.len >= 1 && e.len <= 6);

        /* Every syllable is its initial followed by its final, so `sheng` is
         * always a literal prefix of `text`. (`yun` is not always a suffix:
         * "nv" spells its final with U+00FC.) */
        PYZY_EXPECT (text.compare (0, std::strlen (e.sheng), e.sheng) == 0);

        /* Initials occupy ids 0..23 and finals 0..56 (Types.h). A pinyin_id
         * that picked up part of a pointer would blow straight past these. */
        PYZY_EXPECT (e.pinyin_id[0].sheng <= PINYIN_ID_ZH);
        PYZY_EXPECT (e.pinyin_id[0].yun <= PINYIN_ID_V);

        /* PinyinParser::parse() bsearches this table by `text`, so ascending
         * strcmp order is a correctness requirement, not a nicety — and it is
         * exactly what a shifted `text` field would destroy. */
        if (i > 0)
            PYZY_EXPECT (std::strcmp (PyZy::pinyin_table[i - 1].text, e.text) < 0);
    }
}

/* Ask the library to build a syllable from a (sheng id, yun id) pair and check
 * that the entry it finds carries those same ids. The string it searches for is
 * assembled from id_map[] inside PinyinParser.cc, so a table whose pinyin_id
 * column had drifted relative to its text column could not satisfy this.
 *
 * Two things constrain the sweep. Initials and finals share one id space —
 * 0 is the empty id, 1..23 are initials, 24..56 are finals — and id_map[] is
 * indexed by it directly, so feeding an initial's id in as a `yun` composes
 * strings like "s"+"h" that do resolve, just not to what was asked for. And the
 * option set has to exclude the typo-correction and fuzzy entries: those exist
 * precisely so that a misspelling resolves to *different* ids from the ones its
 * letters name ("jv" carries the ids of "ju"). */
void checkIdRoundTrip ()
{
    int found = 0;
    for (int sheng = PINYIN_ID_ZERO; sheng <= PINYIN_ID_ZH; ++sheng) {
        for (int yun = PINYIN_ID_A; yun <= PINYIN_ID_V; ++yun) {
            /* PINYIN_INCOMPLETE_PINYIN alone: check_flags() then rejects every
             * entry whose flags are non-zero, i.e. everything but the canonical
             * spellings. */
            const Pinyin *p =
                PinyinParser::isPinyin (sheng, yun, PINYIN_INCOMPLETE_PINYIN);
            if (p == NULL)
                continue;   /* not a syllable of Mandarin; nothing to check */
            ++found;
            PYZY_EXPECT_INT (p->pinyin_id[0].sheng, sheng);
            PYZY_EXPECT_INT (p->pinyin_id[0].yun, yun);
            PYZY_EXPECT_INT ((long long) std::strlen (p->text), (long long) p->len);
        }
    }

    /* The bare initials, which are what PINYIN_INCOMPLETE_PINYIN is for. */
    for (int sheng = PINYIN_ID_B; sheng <= PINYIN_ID_ZH; ++sheng) {
        const Pinyin *p =
            PinyinParser::isPinyin (sheng, PINYIN_ID_ZERO, PINYIN_INCOMPLETE_PINYIN);
        if (p == NULL)
            continue;
        ++found;
        PYZY_EXPECT_INT (p->pinyin_id[0].sheng, sheng);
        PYZY_EXPECT_INT (p->pinyin_id[0].yun, PINYIN_ID_ZERO);
    }

    /* Mandarin has a few hundred distinct syllables; the exact count is a
     * property of the table, so only assert that the sweep found a realistic
     * number rather than pinning it. */
    PYZY_EXPECT (found > 300);
}

/* Feed every entry's own text back through the parser. This exercises the
 * library's table (not ours) and the flag filtering in check_flags(). */
void checkEveryEntryParses ()
{
    for (size_t i = 0; i < kTableSize; ++i) {
        const Pinyin &e = PyZy::pinyin_table[i];
        const Pinyin *got = parseOne (e.text, kAllOptions);
        if (got == NULL) {
            ::pyzy_test::fail (__FILE__, __LINE__,
                               std::string ("parse() rejected table entry \"") +
                               e.text + "\"");
            continue;
        }
        /* parse() takes the longest match, which for an entry's own text is
         * that entry (texts are unique), so the phonetics must agree exactly. */
        PYZY_EXPECT_STR (std::string (got->text), std::string (e.text));
        PYZY_EXPECT_INT (got->pinyin_id[0].sheng, e.pinyin_id[0].sheng);
        PYZY_EXPECT_INT (got->pinyin_id[0].yun, e.pinyin_id[0].yun);
        PYZY_EXPECT_INT ((long long) got->len, (long long) e.len);
    }
}

/* Hand-written expectations. Chosen to cover: a bare final, a bare initial
 * (incomplete pinyin), both two-letter initials, the widest final, the
 * ü-spelled final, and the retroflex/zero-final oddities. */
void checkSpotSyllables ()
{
    struct Expectation {
        const char *text;
        const char *sheng;
        const char *yun;
        int         sheng_id;
        int         yun_id;
        size_t      len;
    };

    static const Expectation kSpot[] = {
        { "a",      "",   "a",           PINYIN_ID_ZERO, PINYIN_ID_A,    1 },
        { "o",      "",   "o",           PINYIN_ID_ZERO, PINYIN_ID_O,    1 },
        { "er",     "",   "er",          PINYIN_ID_ZERO, PINYIN_ID_ER,   2 },
        { "ou",     "",   "ou",          PINYIN_ID_ZERO, PINYIN_ID_OU,   2 },
        { "n",      "n",  "",            PINYIN_ID_N,    PINYIN_ID_ZERO, 1 },
        { "zh",     "zh", "",            PINYIN_ID_ZH,   PINYIN_ID_ZERO, 2 },
        { "ri",     "r",  "i",           PINYIN_ID_R,    PINYIN_ID_I,    2 },
        { "zhi",    "zh", "i",           PINYIN_ID_ZH,   PINYIN_ID_I,    3 },
        { "ju",     "j",  "u",           PINYIN_ID_J,    PINYIN_ID_U,    2 },
        { "diu",    "d",  "iu",          PINYIN_ID_D,    PINYIN_ID_IU,   3 },
        { "xue",    "x",  "ue",          PINYIN_ID_X,    PINYIN_ID_UE,   3 },
        { "qiong",  "q",  "iong",        PINYIN_ID_Q,    PINYIN_ID_IONG, 5 },
        { "shuai",  "sh", "uai",         PINYIN_ID_SH,   PINYIN_ID_UAI,  5 },
        { "zhuang", "zh", "uang",        PINYIN_ID_ZH,   PINYIN_ID_UANG, 6 },
        /* "nv"'s final is spelled U+00FC. Written as bytes so the expectation
         * does not depend on how this file's encoding is interpreted — which is
         * itself something the Windows build has to get right (/utf-8). */
        { "nv",     "n",  "\xc3\xbc",    PINYIN_ID_N,    PINYIN_ID_V,    2 },
    };

    for (size_t i = 0; i < sizeof (kSpot) / sizeof (kSpot[0]); ++i) {
        const Expectation &x = kSpot[i];
        const Pinyin *p = parseOne (x.text, kAllOptions);
        if (p == NULL) {
            ::pyzy_test::fail (__FILE__, __LINE__,
                               std::string ("parse() rejected \"") + x.text + "\"");
            continue;
        }
        PYZY_EXPECT_STR (std::string (p->text),  std::string (x.text));
        PYZY_EXPECT_STR (std::string (p->sheng), std::string (x.sheng));
        PYZY_EXPECT_STR (std::string (p->yun),   std::string (x.yun));
        PYZY_EXPECT_INT (p->pinyin_id[0].sheng,  x.sheng_id);
        PYZY_EXPECT_INT (p->pinyin_id[0].yun,    x.yun_id);
        PYZY_EXPECT_INT ((long long) p->len,     (long long) x.len);
    }
}

/* Segmentation of whole words, i.e. what the engine does with real input. */
void checkSegmentation ()
{
    struct Case {
        const char *input;
        const char *segments[4];   /* NULL-terminated */
    };

    static const Case kCases[] = {
        { "nihao",   { "ni", "hao", NULL } },
        { "woaini",  { "wo", "ai", "ni", NULL } },
        { "zhongguo",{ "zhong", "guo", NULL } },
        /* The apostrophe is a syllable separator: without it "xian" is one
         * syllable, with it two. Getting both right needs the table's `len`
         * column to be trustworthy. */
        { "xian",    { "xian", NULL } },
        { "xi'an",   { "xi", "an", NULL } },
    };

    for (size_t i = 0; i < sizeof (kCases) / sizeof (kCases[0]); ++i) {
        const Case &c = kCases[i];
        size_t expected_count = 0;
        while (c.segments[expected_count] != NULL)
            ++expected_count;

        PinyinArray result;
        const String input (c.input);
        const size_t used =
            PinyinParser::parse (input, input.size (), kAllOptions, result, 16);

        PYZY_EXPECT_INT ((long long) used, (long long) input.size ());
        PYZY_EXPECT_INT ((long long) result.size (), (long long) expected_count);
        for (size_t j = 0; j < result.size () && j < expected_count; ++j)
            PYZY_EXPECT_STR (std::string (result[j].pinyin->text),
                             std::string (c.segments[j]));
    }

    /* `max` really does cap the result, and the return value reports only the
     * input actually consumed. */
    {
        PinyinArray result;
        const String input ("nihao");
        const size_t used =
            PinyinParser::parse (input, input.size (), kAllOptions, result, 1);
        PYZY_EXPECT_INT ((long long) result.size (), 1);
        PYZY_EXPECT_INT ((long long) used, 2);   /* "ni" */
    }

    /* Nothing parseable at the start means nothing consumed. */
    {
        PinyinArray result;
        const String input ("1234");
        PYZY_EXPECT_INT (
            (long long) PinyinParser::parse (input, input.size (), kAllOptions,
                                             result, 16),
            0);
    }
}

}  // namespace

int main ()
{
    checkTableStructure ();
    checkIdRoundTrip ();
    checkEveryEntryParses ();
    checkSpotSyllables ();
    checkSegmentation ();
    return PYZY_TEST_RESULT;
}
