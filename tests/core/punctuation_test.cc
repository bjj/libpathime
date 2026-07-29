/*
 * src/punctuation.* — the shared punctuation and width layer, driven directly.
 *
 * Written from the *branch* report rather than the line report, which is why it
 * exists at all: punctuation.cc was already at 97% of lines and looked done.
 * What the line figure hid is that several of its conditions had only ever gone
 * one way. The quote alternation is the clearest case — every engine suite types
 * one quote and none types a second, so the code that produces a *closing* mark
 * ran nowhere, and a bug in it would have reached a user before a test.
 *
 * Three subjects, all of them things a user notices immediately:
 *
 *   - The alternation. A quote key produces ‘ or ’ depending on what the last
 *     one was, and the state has to survive being asked repeatedly.
 *   - The digit look-behind, so that "1.5" stays a number instead of becoming
 *     "1。5" — including the part that makes it a fact about the *document*
 *     rather than about this layer's own output.
 *   - observe_document(), which recovers both of those from a snapshot the
 *     client supplies, because the engine did not necessarily type what is
 *     already in the field.
 *
 * No backend, no options store: WidthSettings is a plain struct, so this is one
 * source file plus utf8.
 */

#include "core_test_util.h"

#include <string>

#include <pathime/pathime.h>

#include "punctuation.h"

using namespace pathime;

namespace {

/* Full-width punctuation is where the substitutions live; half-width is the
 * pass-through case and is asserted separately. */
WidthSettings full_width()
{
    WidthSettings settings;
    settings.punctuation = PATHIME_WIDTH_FULL;
    settings.latin = PATHIME_WIDTH_HALF;
    return settings;
}

std::string emit(char c, const WidthSettings &settings, PunctuationState *state)
{
    return emit_text(c, settings, state);
}

/* ---------------------------------------------------------------------------
 * The alternation
 * ------------------------------------------------------------------------- */

/*
 * A quote key is not a substitution but a toggle, and the toggle is the half
 * no engine suite reaches: they type one quote each, so the closing mark had
 * never been produced.
 */
void test_single_quotes_alternate()
{
    const WidthSettings settings = full_width();
    PunctuationState state;

    /* A fresh state opens. */
    PT_CHECK_STR(emit('\'', settings, &state), "\xE2\x80\x98");   /* ‘ */
    PT_CHECK_STR(emit('\'', settings, &state), "\xE2\x80\x99");   /* ’ */
    PT_CHECK_STR(emit('\'', settings, &state), "\xE2\x80\x98");   /* ‘ again */
    PT_CHECK_STR(emit('\'', settings, &state), "\xE2\x80\x99");

    /* clear() puts it back to opening, which is what a reset must do. */
    state.clear();
    PT_CHECK_STR(emit('\'', settings, &state), "\xE2\x80\x98");
}

void test_double_quotes_alternate_independently()
{
    const WidthSettings settings = full_width();
    PunctuationState state;

    PT_CHECK_STR(emit('"', settings, &state), "\xE2\x80\x9C");    /* “ */
    PT_CHECK_STR(emit('"', settings, &state), "\xE2\x80\x9D");    /* ” */

    /*
     * The two alternations are separate pieces of state. A single quote in the
     * middle of a double-quoted span must not flip the double one, or a
     * quotation containing an apostrophe would close itself.
     */
    PT_CHECK_STR(emit('"', settings, &state), "\xE2\x80\x9C");    /* “ */
    PT_CHECK_STR(emit('\'', settings, &state), "\xE2\x80\x98");   /* ‘ */
    PT_CHECK_STR(emit('\'', settings, &state), "\xE2\x80\x99");   /* ’ */
    PT_CHECK_STR(emit('"', settings, &state), "\xE2\x80\x9D");    /* ” closes */
}

/*
 * At half width a quote is the ASCII character, and the alternation state is
 * deliberately left alone — so a quote typed later at full width still opens
 * rather than arriving mid-alternation from marks that were never curly.
 */
void test_half_width_passes_through_without_alternating()
{
    WidthSettings settings = full_width();
    settings.punctuation = PATHIME_WIDTH_HALF;

    PunctuationState state;
    PT_CHECK_STR(emit('\'', settings, &state), "'");
    PT_CHECK_STR(emit('\'', settings, &state), "'");
    PT_CHECK_STR(emit('"', settings, &state), "\"");

    /* Nothing alternated, so full width still opens. */
    settings.punctuation = PATHIME_WIDTH_FULL;
    PT_CHECK_STR(emit('\'', settings, &state), "\xE2\x80\x98");   /* ‘ */
}

/* ---------------------------------------------------------------------------
 * The digit look-behind
 * ------------------------------------------------------------------------- */

/*
 * A full stop after a digit is a decimal point. Without this "1.5" becomes
 * "1。5", which is the kind of thing that makes an input method unusable for
 * anyone typing numbers.
 */
void test_period_after_digit_is_a_decimal_point()
{
    const WidthSettings settings = full_width();
    PunctuationState state;

    /* On its own, a full stop is 。 */
    PT_CHECK_STR(emit('.', settings, &state), "\xE3\x80\x82");

    /* Directly after a digit it is a period. */
    state.note_commit("1");
    PT_CHECK_STR(emit('.', settings, &state), ".");

    /*
     * And the look-behind is over the document rather than over this layer's
     * own output: a Chinese character committed between the digit and the stop
     * disarms it, or "1好." would be punctuated like "1.5".
     */
    state.note_commit("1");
    state.note_commit("\xE5\xA5\xBD");                            /* 好 */
    PT_CHECK_STR(emit('.', settings, &state), "\xE3\x80\x82");

    /* Multi-character commits are not digits however they begin. */
    state.note_commit("12");
    PT_CHECK_STR(emit('.', settings, &state), "\xE3\x80\x82");

    /* An empty commit says nothing and must not clear what is known. */
    state.note_commit("7");
    state.note_commit("");
    PT_CHECK_STR(emit('.', settings, &state), ".");
}

/* ---------------------------------------------------------------------------
 * Recovering state from the client's document
 * ------------------------------------------------------------------------- */

/*
 * The engine did not necessarily type what is already in the field, so both
 * pieces of state are re-derived from the snapshot when there is one. This is
 * the path that makes a quote typed into the middle of existing prose close the
 * mark that is actually there rather than the one the engine last emitted.
 */
void test_observe_document_recovers_the_alternation()
{
    const WidthSettings settings = full_width();

    /* An opening mark in the document means the next quote closes it. */
    {
        PunctuationState state;
        observe_document("\xE2\x80\x98""abc", &state);             /* ‘abc */
        PT_CHECK_STR(emit('\'', settings, &state), "\xE2\x80\x99");
    }

    /* A closing mark means the next one opens. */
    {
        PunctuationState state;
        state.quote_open = false;
        observe_document("\xE2\x80\x99", &state);                  /* ’ */
        PT_CHECK_STR(emit('\'', settings, &state), "\xE2\x80\x98");
    }

    /*
     * With both present the *last* one decides, which is the whole reason the
     * search runs from the end.
     */
    {
        PunctuationState state;
        observe_document("\xE2\x80\x98""a\xE2\x80\x99""b", &state); /* ‘a’b */
        PT_CHECK_STR(emit('\'', settings, &state), "\xE2\x80\x98");
    }
    {
        PunctuationState state;
        observe_document("\xE2\x80\x99""a\xE2\x80\x98""b", &state); /* ’a‘b */
        PT_CHECK_STR(emit('\'', settings, &state), "\xE2\x80\x99");
    }

    /* The double-quote alternation is recovered by the same rule, separately. */
    {
        PunctuationState state;
        observe_document("\xE2\x80\x9C""x", &state);               /* “x */
        PT_CHECK_STR(emit('"', settings, &state), "\xE2\x80\x9D"); /* ” */
        /* and the single-quote state was left alone by a document with none */
        PT_CHECK_STR(emit('\'', settings, &state), "\xE2\x80\x98");
    }
}

void test_observe_document_recovers_the_digit_look_behind()
{
    const WidthSettings settings = full_width();

    /* A document ending in a digit arms it. */
    {
        PunctuationState state;
        observe_document("value 3", &state);
        PT_CHECK_STR(emit('.', settings, &state), ".");
    }

    /* One ending in something else does not. */
    {
        PunctuationState state;
        state.note_commit("3");
        observe_document("value x", &state);
        PT_CHECK_STR(emit('.', settings, &state), "\xE3\x80\x82");
    }

    /*
     * A document ending in a multi-byte character. The walk back over
     * continuation bytes is what makes this a property of the code rather than
     * of the encoding table: 好's last byte is 0xBD, which is not in the ASCII
     * digit range, but nothing here should depend on that being true.
     */
    {
        PunctuationState state;
        state.note_commit("3");
        observe_document("\xE5\xA5\xBD", &state);                 /* 好 */
        PT_CHECK_STR(emit('.', settings, &state), "\xE3\x80\x82");
    }

    /* A single multi-byte character preceded by a digit is still not a digit. */
    {
        PunctuationState state;
        observe_document("1\xE5\xA5\xBD", &state);                /* 1好 */
        PT_CHECK_STR(emit('.', settings, &state), "\xE3\x80\x82");
    }
}

/*
 * "Nothing visible: every tracked value stands." The empty snapshot is the
 * no-snapshot case and the caret-at-the-start case at once, and both mean the
 * engine keeps what it was tracking rather than resetting to a guess.
 */
void test_empty_document_changes_nothing()
{
    const WidthSettings settings = full_width();
    PunctuationState state;

    state.note_commit("5");
    state.quote_open = false;

    observe_document("", &state);

    PT_CHECK_STR(emit('.', settings, &state), ".");       /* digit survived */
    PT_CHECK_STR(emit('\'', settings, &state), "\xE2\x80\x99");  /* ’ survived */
}

/* ---------------------------------------------------------------------------
 * What is punctuation at all
 * ------------------------------------------------------------------------- */

void test_emittable()
{
    /* Printable ASCII, and nothing else — the range this layer can speak for. */
    PT_CHECK(emittable(0x20));
    PT_CHECK(emittable('A'));
    PT_CHECK(emittable('z'));
    PT_CHECK(emittable('9'));
    PT_CHECK(emittable(0x7E));

    PT_CHECK(!emittable(0x1F));
    PT_CHECK(!emittable(0x7F));
    PT_CHECK(!emittable(PATHIME_KEY_BACKSPACE));
    PT_CHECK(!emittable(0x3042));
}

/*
 * Letters and digits are not punctuation, and the full-width *latin* option is
 * what decides their form. Upper case is checked because the predicate that
 * classifies them had only ever been asked about lower case.
 */
void test_latin_width()
{
    WidthSettings settings = full_width();

    settings.latin = PATHIME_WIDTH_HALF;
    PT_CHECK_STR(emit('a', settings, nullptr), "a");
    PT_CHECK_STR(emit('Z', settings, nullptr), "Z");
    PT_CHECK_STR(emit('7', settings, nullptr), "7");
    PT_CHECK_STR(emit(' ', settings, nullptr), " ");

    settings.latin = PATHIME_WIDTH_FULL;
    PT_CHECK_STR(emit('a', settings, nullptr), "\xEF\xBD\x81");   /* ａ */
    PT_CHECK_STR(emit('Z', settings, nullptr), "\xEF\xBC\xBA");   /* Ｚ */
    PT_CHECK_STR(emit('7', settings, nullptr), "\xEF\xBC\x97");   /* ７ */
    PT_CHECK_STR(emit(' ', settings, nullptr), "\xE3\x80\x80");   /* ideographic space */
}

}  // namespace

int main(void)
{
    test_single_quotes_alternate();
    test_double_quotes_alternate_independently();
    test_half_width_passes_through_without_alternating();
    test_period_after_digit_is_a_decimal_point();
    test_observe_document_recovers_the_alternation();
    test_observe_document_recovers_the_digit_look_behind();
    test_empty_document_changes_nothing();
    test_emittable();
    test_latin_width();
    return pt_report("core.punctuation");
}
