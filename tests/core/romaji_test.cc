/*
 * src/engines/anthy/romaji.* — the Japanese composing front end, driven
 * directly rather than through anthy.
 *
 * This is the one adapter half that runs *before* its vendored library sees
 * anything, and romaji.h was written to be reachable on its own: the settings
 * are a plain struct rather than an OptionReader precisely so that the state
 * machine "stays testable from a scratch program". This is that program.
 *
 * What it covers that tests/api/engine_anthy.c cannot reasonably reach:
 *
 *   - The period and symbol styles. Those are a re-projection applied on the
 *     way *out* of the buffer rather than when a key is struck, which is what
 *     makes a mid-composition option change take effect on text already typed.
 *     Through the public API each combination would mean typing a word, setting
 *     an option and reading the preedit back; here it is one call per style.
 *   - The resolver's dead ends — the sequences that can never become kana. They
 *     are where this library knowingly differs from ibus-anthy, and the
 *     divergence is argued for in a comment with nothing pinning it down.
 *
 * Nothing here needs anthy: no dictionary, no conversion, no context. The suite
 * compiles two source files.
 */

#include "core_test_util.h"

#include <cstdint>
#include <string>

#include <pathime/pathime.h>

#include "engines/anthy/romaji.h"

using namespace pathime;

namespace {

/*
 * Feed a string of ASCII through the resolver as key events, which is the only
 * public way in — step() is private, and deliberately so: the composer's
 * contract is about keys, not about characters it has already accepted.
 */
void type(RomajiComposer &composer, const char *text, const RomajiSettings &settings)
{
    for (const char *p = text; *p != '\0'; ++p) {
        KeyEvent key;
        key.keysym = static_cast<uint32_t>(static_cast<unsigned char>(*p));
        composer.insert(key, settings);
    }
}

std::string typed(const char *text, const RomajiSettings &settings)
{
    RomajiComposer composer;
    type(composer, text, settings);
    return composer.display(settings);
}

/* ---------------------------------------------------------------------------
 * The resolver
 * ------------------------------------------------------------------------- */

void test_basic_resolution()
{
    const RomajiSettings settings;

    PT_CHECK_STR(typed("ka", settings), "\xE3\x81\x8B");                  /* か */
    PT_CHECK_STR(typed("kya", settings), "\xE3\x81\x8D\xE3\x82\x83");     /* きゃ */

    /* The three pending-consonant rules the header names. */
    PT_CHECK_STR(typed("kka", settings),
                 "\xE3\x81\xA3\xE3\x81\x8B");                            /* っか */
    PT_CHECK_STR(typed("nn", settings), "\xE3\x82\x93");                 /* ん */
    PT_CHECK_STR(typed("nka", settings),
                 "\xE3\x82\x93\xE3\x81\x8B");                            /* んか */

    /* The double-consonant rule is computed over the alphabet rather than
     * listed, so it holds for a consonant with no kana of its own. */
    PT_CHECK_STR(typed("vv", settings), "\xE3\x81\xA3" "v");             /* っv */

    /* A lone consonant stays pending and shows as itself. */
    {
        RomajiComposer composer;
        type(composer, "k", settings);
        PT_CHECK(composer.has_pending());
        PT_CHECK(!composer.empty());
        PT_CHECK_STR(composer.display(settings), "k");
    }
}

/*
 * A sequence whose leading character can never begin anything. The rule is that
 * what was typed first appears first — the honest reading — and it is
 * deliberately *not* what ibus-anthy does: its segment list can hold an
 * unresolved segment ahead of a resolved one, so it emits the suffix it matched
 * and shows the unresolvable prefix afterwards. A single kana run cannot do
 * that, and reproducing the reordering would be worse than the plain order.
 *
 * That reasoning sits in a comment in romaji.cc. Without a test it is only an
 * intention.
 */
void test_dead_ends_emit_in_typed_order()
{
    const RomajiSettings settings;

    /* 'q' begins no romaji at all; it is emitted, then "ka" resolves. */
    PT_CHECK_STR(typed("qka", settings), "q\xE3\x81\x8B");               /* qか */

    /* Two dead ends in a row, both kept in order. */
    PT_CHECK_STR(typed("qqka", settings), "qq\xE3\x81\x8B");             /* qqか */

    /*
     * Not every unfamiliar-looking pair is a dead end, which is why the cases
     * above are chosen rather than assumed: "xka" is the standard spelling of
     * the small ヵ, so a leading dead end followed by it resolves.
     */
    PT_CHECK_STR(typed("qxka", settings), "q\xE3\x83\xB5");              /* qヵ */

    /* A digit is not romaji and not a dead end to be retried: it stands. */
    PT_CHECK_STR(typed("1", settings), "1");

    /* A dead end after a resolved kana leaves the kana alone. */
    PT_CHECK_STR(typed("kaq", settings), "\xE3\x81\x8B" "q");            /* かq */
}

/* ---------------------------------------------------------------------------
 * The styles, which are a projection rather than a substitution at input
 * ------------------------------------------------------------------------- */

/*
 * Period and comma. Kept out of the stored kana so that changing the option
 * re-projects text already typed, which is what the header's "a change takes
 * effect immediately" costs here — two comparisons per scalar on the way out.
 *
 * The projection is asserted by typing once and reading the same composer back
 * under both settings, which is the property that matters: the same buffer,
 * two answers.
 */
void test_period_style_projects_existing_text()
{
    RomajiSettings settings;
    RomajiComposer composer;
    type(composer, "ka.ki,", settings);

    /* PATHIME_ANTHY_PERIOD_KUTEN, the default: 。 and 、 */
    settings.period = PATHIME_ANTHY_PERIOD_KUTEN;
    PT_CHECK_STR(composer.display(settings),
                 "\xE3\x81\x8B\xE3\x80\x82\xE3\x81\x8D\xE3\x80\x81");    /* か。き、 */

    /* PATHIME_ANTHY_PERIOD_FULLWIDTH: ．and ，, from the same buffer. */
    settings.period = PATHIME_ANTHY_PERIOD_FULLWIDTH;
    PT_CHECK_STR(composer.display(settings),
                 "\xE3\x81\x8B\xEF\xBC\x8E\xE3\x81\x8D\xEF\xBC\x8C");    /* か．き， */
}

/*
 * Brackets and the slash, the other half of the same projection. The four
 * PATHIME_ANTHY_SYMBOL_* values are two independent choices — corner brackets
 * or square ones, middle dot or solidus — so all four combinations are checked
 * rather than a diagonal through them.
 */
void test_symbol_style_projects_existing_text()
{
    RomajiSettings settings;
    RomajiComposer composer;
    type(composer, "[a]/", settings);

    /* 「あ」・ — corner brackets, middle dot. */
    settings.symbol = PATHIME_ANTHY_SYMBOL_CORNER_MIDDOT;
    PT_CHECK_STR(composer.display(settings),
                 "\xE3\x80\x8C\xE3\x81\x82\xE3\x80\x8D\xE3\x83\xBB");

    /* 「あ」／ — corner brackets, solidus. The default. */
    settings.symbol = PATHIME_ANTHY_SYMBOL_CORNER_SLASH;
    PT_CHECK_STR(composer.display(settings),
                 "\xE3\x80\x8C\xE3\x81\x82\xE3\x80\x8D\xEF\xBC\x8F");

    /* ［あ］・ — square brackets, middle dot. */
    settings.symbol = PATHIME_ANTHY_SYMBOL_BRACKET_MIDDOT;
    PT_CHECK_STR(composer.display(settings),
                 "\xEF\xBC\xBB\xE3\x81\x82\xEF\xBC\xBD\xE3\x83\xBB");

    /* ［あ］／ — square brackets, solidus. */
    settings.symbol = PATHIME_ANTHY_SYMBOL_BRACKET_SLASH;
    PT_CHECK_STR(composer.display(settings),
                 "\xEF\xBC\xBB\xE3\x81\x82\xEF\xBC\xBD\xEF\xBC\x8F");
}

/*
 * The two styles are independent, and the projection walks the whole buffer
 * rather than stopping at the first substitution it makes.
 */
void test_styles_compose()
{
    RomajiSettings settings;
    settings.period = PATHIME_ANTHY_PERIOD_FULLWIDTH;
    settings.symbol = PATHIME_ANTHY_SYMBOL_BRACKET_MIDDOT;

    RomajiComposer composer;
    type(composer, "[a]./", settings);

    /* ［あ］．・ — a bracket, a period and a slash, all three rewritten. */
    PT_CHECK_STR(composer.display(settings),
                 "\xEF\xBC\xBB\xE3\x81\x82\xEF\xBC\xBD\xEF\xBC\x8E\xE3\x83\xBB");
}

/*
 * The reading anthy is given is not the text the user sees. Styles are a
 * display concern; the reading must stay plain hiragana whatever they say, or
 * the conversion would be asked about characters no dictionary carries.
 */
void test_reading_is_unaffected_by_styles()
{
    RomajiSettings plain;
    RomajiSettings styled;
    styled.period = PATHIME_ANTHY_PERIOD_FULLWIDTH;
    styled.symbol = PATHIME_ANTHY_SYMBOL_BRACKET_MIDDOT;

    RomajiComposer composer;
    type(composer, "kaki", plain);

    PT_CHECK_STR(composer.reading(plain), composer.reading(styled));
    PT_CHECK_STR(composer.reading(styled), "\xE3\x81\x8B\xE3\x81\x8D");  /* かき */
}

/* ---------------------------------------------------------------------------
 * Editing
 * ------------------------------------------------------------------------- */

void test_backspace()
{
    const RomajiSettings settings;
    RomajiComposer composer;

    /* Nothing to remove is false, so the client's own backspace applies. */
    PT_CHECK(!composer.backspace());

    /* Pending Latin goes one character at a time. */
    type(composer, "ky", settings);
    PT_CHECK(composer.backspace());
    PT_CHECK_STR(composer.display(settings), "k");

    /* Then the resolved kana, one kana at a time rather than one byte. */
    composer.clear();
    type(composer, "kaki", settings);
    PT_CHECK(composer.backspace());
    PT_CHECK_STR(composer.display(settings), "\xE3\x81\x8B");            /* か */
    PT_CHECK(composer.backspace());
    PT_CHECK(composer.empty());
    PT_CHECK(!composer.backspace());
}

/*
 * A trailing "n" is still pending as far as the resolver is concerned — a
 * following vowel would make it な rather than ん — but committing must not
 * drop it. commit_text() is where that resolution happens.
 */
void test_commit_resolves_a_trailing_n()
{
    const RomajiSettings settings;
    RomajiComposer composer;
    type(composer, "kan", settings);

    PT_CHECK(composer.has_pending());
    PT_CHECK_STR(composer.commit_text(settings),
                 "\xE3\x81\x8B\xE3\x82\x93");                            /* かん */
}

/*
 * assign_kana() and prepend_kana(), the two re-seeding directions the eager
 * selection model needs. The documented cost of the first is that a pending
 * fragment stops pending, which is worth pinning: it is the price of selecting
 * mid-word with unresolved Latin in the buffer.
 */
void test_reseeding()
{
    const RomajiSettings settings;
    RomajiComposer composer;

    type(composer, "kak", settings);
    PT_CHECK(composer.has_pending());

    composer.assign_kana("\xE3\x81\x82");                                /* あ */
    PT_CHECK(!composer.has_pending());
    PT_CHECK_STR(composer.display(settings), "\xE3\x81\x82");

    composer.prepend_kana("\xE3\x81\x84");                               /* い */
    PT_CHECK_STR(composer.display(settings),
                 "\xE3\x81\x84\xE3\x81\x82");                            /* いあ */
}

}  // namespace

int main(void)
{
    test_basic_resolution();
    test_dead_ends_emit_in_typed_order();
    test_period_style_projects_existing_text();
    test_symbol_style_projects_existing_text();
    test_styles_compose();
    test_reading_is_unaffected_by_styles();
    test_backspace();
    test_commit_resolves_a_trailing_n();
    test_reseeding();
    return pt_report("core.romaji");
}
