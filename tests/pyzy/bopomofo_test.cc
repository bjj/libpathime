/* Bopomofo (zhuyin) conversion through PyZy::InputContext.
 *
 * The interesting part of this backend is that it is the only one that carries
 * phonetics around as `wchar_t`. pyzy was written for a platform where that
 * type is 32-bit UCS-4 and takes the liberty in one place — BopomofoContext's
 * auxiliary-text builder used to reinterpret a `const wchar_t *` as a UCS-4
 * string and hand it to g_ucs4_to_utf8. Where wchar_t is 16 bits that reads two
 * zhuyin letters as one nonsensical code point, so the auxiliary text came out
 * empty (with a g_warning) instead of "ㄋㄧ,ㄏㄠ|". The Windows build rewrites
 * that statement in its source mirror; see cmake/ports/pyzy/CMakeLists.txt.
 *
 * The auxiliary-text expectations below are therefore the ones that matter
 * most here: they are the only automated check that the rewrite is in place and
 * that it produces what Linux produces. Expected values are upstream's, from
 * pyzy's src/tests/basic.cc (testBopomofo / testCommit); "sucl" is
 * ㄋㄧ ㄏㄠ = ni hao on the standard keyboard.
 */
#include "pyzy_assert.h"
#include "pyzy_context.h"

#include <PyZy/Const.h>
#include <PyZy/Variant.h>

#include <memory>

using PyZy::InputContext;
using pyzy_test::Recorder;
using pyzy_test::insertKeys;

namespace {

std::unique_ptr<InputContext> makeContext (Recorder *observer)
{
    std::unique_ptr<InputContext> context (
        InputContext::create (InputContext::BOPOMOFO, observer));
    /* basic.cc does the same: the special-phrase table is keyed on latin
     * spellings, so leaving it on lets pinyin phrases hijack zhuyin keys. */
    context->setProperty (InputContext::PROPERTY_SPECIAL_PHRASE,
                          PyZy::Variant::fromBool (false));
    return context;
}

void checkConversion ()
{
    Recorder observer;
    std::unique_ptr<InputContext> context = makeContext (&observer);

    insertKeys (context.get (), "sucl");
    PYZY_EXPECT_INT (context->cursor (), 4);
    /* The input text stays as the keys typed; only the auxiliary text shows
     * the zhuyin they stand for. */
    PYZY_EXPECT_STR (context->inputText (),      "sucl");
    PYZY_EXPECT_STR (context->selectedText (),   "");
    PYZY_EXPECT_STR (context->conversionText (), "你好");
    PYZY_EXPECT_STR (context->restText (),       "");
    PYZY_EXPECT_STR (context->auxiliaryText (),  "ㄋㄧ,ㄏㄠ|");
    PYZY_EXPECT (context->hasCandidate (0));
    PYZY_EXPECT_STR (observer.committed (),      "");

    context->reset ();
    PYZY_EXPECT_INT (context->cursor (), 0);
    PYZY_EXPECT_STR (context->inputText (),     "");
    PYZY_EXPECT_STR (context->auxiliaryText (), "");
    PYZY_EXPECT (!context->hasCandidate (0));
}

void checkCommitTypes ()
{
    Recorder observer;
    std::unique_ptr<InputContext> context = makeContext (&observer);

    /* TYPE_RAW gives back the latin keys... */
    insertKeys (context.get (), "sucl");
    observer.clear ();
    context->commit (InputContext::TYPE_RAW);
    PYZY_EXPECT_STR (observer.committed (), "sucl");

    /* ...TYPE_PHONETIC the zhuyin they map to. This path builds its output one
     * code point at a time, so unlike the auxiliary text it was never affected
     * by the 16-bit wchar_t problem — which makes the pair a useful contrast if
     * one of them ever regresses. */
    context->reset ();
    insertKeys (context.get (), "sucl");
    observer.clear ();
    context->commit (InputContext::TYPE_PHONETIC);
    PYZY_EXPECT_STR (observer.committed (), "ㄋㄧㄏㄠ");

    /* TYPE_CONVERTED with nothing selected also commits the phonetics, not the
     * conversion: fixing the conversion needs selectCandidate(). */
    context->reset ();
    insertKeys (context.get (), "sucl");
    observer.clear ();
    context->commit (InputContext::TYPE_CONVERTED);
    PYZY_EXPECT_STR (observer.committed (), "ㄋㄧㄏㄠ");
}

void checkSelectCandidate ()
{
    Recorder observer;
    std::unique_ptr<InputContext> context = makeContext (&observer);

    insertKeys (context.get (), "sucl");
    observer.clear ();
    PYZY_EXPECT (context->selectCandidate (0));
    PYZY_EXPECT_STR (observer.committed (), "你好");
    PYZY_EXPECT_STR (context->inputText (), "");
    PYZY_EXPECT (!context->hasCandidate (0));
}

/* Editing re-derives the zhuyin string from the remaining keys, so the
 * auxiliary text has to track it. */
void checkEditing ()
{
    Recorder observer;
    std::unique_ptr<InputContext> context = makeContext (&observer);

    insertKeys (context.get (), "sucl");
    PYZY_EXPECT (context->removeCharBefore ());
    PYZY_EXPECT_STR (context->inputText (),     "suc");
    PYZY_EXPECT_STR (context->auxiliaryText (), "ㄋㄧ,ㄏ|");

    PYZY_EXPECT (context->moveCursorLeft ());
    PYZY_EXPECT_INT (context->cursor (), 2);
    PYZY_EXPECT_STR (context->auxiliaryText (), "ㄋㄧ|ㄏ");
}

void checkSchemaProperty ()
{
    Recorder observer;
    std::unique_ptr<InputContext> context = makeContext (&observer);

    PYZY_EXPECT (context->setProperty (
        InputContext::PROPERTY_BOPOMOFO_SCHEMA,
        PyZy::Variant::fromUnsignedInt (BOPOMOFO_KEYBOARD_ETAN)));
    PYZY_EXPECT (!context->setProperty (
        InputContext::PROPERTY_BOPOMOFO_SCHEMA,
        PyZy::Variant::fromUnsignedInt (BOPOMOFO_KEYBOARD_LAST)));
}

}  // namespace

int main ()
{
    /* Before init(): this is where the database is opened. */
    pyzy_test::useStagedDatabase ();
    InputContext::init (pyzy_test::userCacheDir (), pyzy_test::userConfigDir ());

    checkConversion ();
    checkCommitTypes ();
    checkSelectCandidate ();
    checkEditing ();
    checkSchemaProperty ();

    InputContext::finalize ();
    return PYZY_TEST_RESULT;
}
