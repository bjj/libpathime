/* Full-pinyin conversion end to end, through the public PyZy::InputContext API.
 *
 * This is the test that puts weight on the Windows port's String.h graft. Every
 * candidate lookup goes Database::query() -> a SQL statement assembled with
 * String::operator<<, over ids that are `size_t` and `unsigned char`. Under
 * LP64 those all landed on one of pyzy's three original overloads; on Windows
 * they do not, and the port added exact-match overloads to resolve the
 * ambiguity. If any of the added overloads rendered a number differently from
 * the one Linux picks, the WHERE clause would select different rows and the
 * candidates below would come out different — which is precisely what these
 * expectations catch.
 *
 * The expected values are upstream's own, taken from pyzy's src/tests/basic.cc
 * (testFullPinyin / testCommit). They therefore describe how pyzy behaves on
 * Linux against the bundled android.db, which makes them a cross-platform
 * comparison rather than a transcription of whatever Windows happens to do.
 */
#include "pyzy_assert.h"
#include "pyzy_context.h"

#include <PyZy/Const.h>
#include <PyZy/Variant.h>

#include <memory>
#include <string>

using PyZy::Candidate;
using PyZy::InputContext;
using pyzy_test::Recorder;
using pyzy_test::insertKeys;

namespace {

void checkConversion ()
{
    Recorder observer;
    std::unique_ptr<InputContext> context (
        InputContext::create (InputContext::FULL_PINYIN, &observer));

    insertKeys (context.get (), "nihao");
    PYZY_EXPECT_INT (context->cursor (), 5);
    PYZY_EXPECT_STR (context->inputText (),      "nihao");
    PYZY_EXPECT_STR (context->selectedText (),   "");
    PYZY_EXPECT_STR (context->conversionText (), "你好");   /* ni hao */
    PYZY_EXPECT_STR (context->restText (),       "");
    PYZY_EXPECT_STR (context->auxiliaryText (),  "ni hao|");
    PYZY_EXPECT (context->hasCandidate (0));
    PYZY_EXPECT_STR (observer.committed (),      "");
    PYZY_EXPECT (observer.notifications () > 0);

    /* The first candidate is the conversion shown in the preedit, and it comes
     * out of android.db rather than the special-phrase table. */
    Candidate candidate;
    PYZY_EXPECT (context->getCandidate (0, candidate));
    PYZY_EXPECT_STR (candidate.text, "你好");
    PYZY_EXPECT_INT (candidate.type, PyZy::NORMAL_PHRASE);
    PYZY_EXPECT (context->getPreparedCandidatesSize () > 1);

    context->reset ();
    PYZY_EXPECT_INT (context->cursor (), 0);
    PYZY_EXPECT_STR (context->inputText (),      "");
    PYZY_EXPECT_STR (context->conversionText (), "");
    PYZY_EXPECT_STR (context->auxiliaryText (),  "");
    PYZY_EXPECT (!context->hasCandidate (0));
}

/* The three commit types on the same input. Nothing has been selected, so even
 * TYPE_CONVERTED commits the raw keys — for pinyin the conversion is only
 * fixed by selectCandidate(). */
void checkCommitTypes ()
{
    Recorder observer;
    std::unique_ptr<InputContext> context (
        InputContext::create (InputContext::FULL_PINYIN, &observer));

    static const InputContext::CommitType kTypes[] = {
        InputContext::TYPE_RAW,
        InputContext::TYPE_PHONETIC,
        InputContext::TYPE_CONVERTED,
    };

    for (size_t i = 0; i < sizeof (kTypes) / sizeof (kTypes[0]); ++i) {
        context->reset ();
        observer.clear ();
        insertKeys (context.get (), "nihao");
        PYZY_EXPECT_STR (context->conversionText (), "你好");

        context->commit (kTypes[i]);
        PYZY_EXPECT_STR (observer.committed (), "nihao");
        PYZY_EXPECT_INT (context->cursor (), 0);
        PYZY_EXPECT_STR (context->inputText (), "");
        PYZY_EXPECT (!context->hasCandidate (0));
    }
}

void checkSelectCandidate ()
{
    Recorder observer;
    std::unique_ptr<InputContext> context (
        InputContext::create (InputContext::FULL_PINYIN, &observer));

    insertKeys (context.get (), "nihao");
    observer.clear ();

    /* Selecting the only candidate consumes all the input, so pyzy commits
     * immediately rather than leaving a selection behind. */
    PYZY_EXPECT (context->selectCandidate (0));
    PYZY_EXPECT_STR (observer.committed (), "你好");
    PYZY_EXPECT_INT (context->cursor (), 0);
    PYZY_EXPECT_STR (context->inputText (),    "");
    PYZY_EXPECT_STR (context->selectedText (), "");
    PYZY_EXPECT (!context->hasCandidate (0));
}

/* Special phrases come from phrases.txt, not the database, and are the one
 * candidate source that does not go through the SQL path. */
void checkSpecialPhrase ()
{
    Recorder observer;
    std::unique_ptr<InputContext> context (
        InputContext::create (InputContext::FULL_PINYIN, &observer));

    insertKeys (context.get (), "aazhi");
    PYZY_EXPECT_STR (context->inputText (),      "aazhi");
    PYZY_EXPECT_STR (context->conversionText (), "AA制");
    PYZY_EXPECT_STR (context->auxiliaryText (),  "aazhi|");

    Candidate candidate;
    PYZY_EXPECT (context->getCandidate (0, candidate));
    PYZY_EXPECT_STR (candidate.text, "AA制");
    PYZY_EXPECT_INT (candidate.type, PyZy::SPECIAL_PHRASE);

    observer.clear ();
    PYZY_EXPECT (context->selectCandidate (0));
    PYZY_EXPECT_STR (observer.committed (), "AA制");

    /* Turning special phrases off must fall back to the database conversion of
     * the same keys. */
    context->reset ();
    PYZY_EXPECT (context->setProperty (InputContext::PROPERTY_SPECIAL_PHRASE,
                                       PyZy::Variant::fromBool (false)));
    insertKeys (context.get (), "aazhi");
    PYZY_EXPECT_STR (context->conversionText (), "啊啊之");  /* a a zhi */
    PYZY_EXPECT_STR (context->auxiliaryText (),  "a a zhi|");
}

/* Cursor motion and deletion, which re-segment the input and therefore
 * re-query the database with a different pinyin array each time. */
void checkEditing ()
{
    Recorder observer;
    std::unique_ptr<InputContext> context (
        InputContext::create (InputContext::FULL_PINYIN, &observer));

    insertKeys (context.get (), "nihao");
    PYZY_EXPECT (context->moveCursorLeft ());
    PYZY_EXPECT_INT (context->cursor (), 4);
    PYZY_EXPECT_STR (context->inputText (), "nihao");

    PYZY_EXPECT (context->moveCursorRight ());
    PYZY_EXPECT_INT (context->cursor (), 5);
    PYZY_EXPECT_STR (context->conversionText (), "你好");
    PYZY_EXPECT_STR (context->auxiliaryText (),  "ni hao|");

    PYZY_EXPECT (context->removeCharBefore ());
    PYZY_EXPECT_INT (context->cursor (), 4);
    PYZY_EXPECT_STR (context->inputText (), "niha");

    PYZY_EXPECT (context->removeWordBefore ());
    PYZY_EXPECT_STR (context->inputText (), "ni");
    PYZY_EXPECT_STR (context->conversionText (), "你");
    PYZY_EXPECT_STR (context->auxiliaryText (),  "ni|");

    PYZY_EXPECT (context->moveCursorToBegin ());
    PYZY_EXPECT_INT (context->cursor (), 0);
    PYZY_EXPECT (context->moveCursorToEnd ());
    PYZY_EXPECT_INT (context->cursor (), 2);
}

}  // namespace

int main ()
{
    InputContext::init (pyzy_test::userCacheDir (), pyzy_test::userConfigDir ());

    checkConversion ();
    checkCommitTypes ();
    checkSelectCandidate ();
    checkSpecialPhrase ();
    checkEditing ();

    InputContext::finalize ();
    return PYZY_TEST_RESULT;
}
