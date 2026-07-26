/* Double-pinyin conversion through PyZy::InputContext.
 *
 * Double pinyin maps each syllable onto exactly two keys via a keyboard schema
 * (default MSPY), so it reaches the same database query path as full pinyin but
 * through a different front end — DoublePinyinContext rather than
 * FullPinyinContext. Worth covering separately because the two contexts share
 * none of their input handling.
 *
 * Expected values are upstream's, from pyzy's src/tests/basic.cc
 * (testDoublePinyin): "nihk" is ni + hao on the MSPY schema.
 */
#include "pyzy_assert.h"
#include "pyzy_context.h"

#include <PyZy/Const.h>
#include <PyZy/Variant.h>

#include <memory>
#include <set>
#include <string>

using PyZy::InputContext;
using pyzy_test::Recorder;
using pyzy_test::insertKeys;

namespace {

void checkConversion ()
{
    Recorder observer;
    std::unique_ptr<InputContext> context (
        InputContext::create (InputContext::DOUBLE_PINYIN, &observer));

    insertKeys (context.get (), "nihk");
    PYZY_EXPECT_INT (context->cursor (), 4);
    PYZY_EXPECT_STR (context->inputText (),      "nihk");
    PYZY_EXPECT_STR (context->selectedText (),   "");
    PYZY_EXPECT_STR (context->conversionText (), "你好");   /* ni hao */
    PYZY_EXPECT_STR (context->restText (),       "");
    /* The auxiliary text spells out the pinyin the two-key sequences expanded
     * to, which is the whole point of the schema. */
    PYZY_EXPECT_STR (context->auxiliaryText (),  "ni hao|");
    PYZY_EXPECT (context->hasCandidate (0));
    PYZY_EXPECT_STR (observer.committed (),      "");

    context->reset ();
    PYZY_EXPECT_INT (context->cursor (), 0);
    PYZY_EXPECT_STR (context->inputText (),      "");
    PYZY_EXPECT_STR (context->conversionText (), "");
    PYZY_EXPECT_STR (context->auxiliaryText (),  "");
    PYZY_EXPECT (!context->hasCandidate (0));
}

void checkCommitAndSelect ()
{
    Recorder observer;
    std::unique_ptr<InputContext> context (
        InputContext::create (InputContext::DOUBLE_PINYIN, &observer));

    /* Committing without selecting yields the raw keystrokes, not the pinyin
     * they expand to and not the conversion. */
    insertKeys (context.get (), "nihk");
    observer.clear ();
    context->commit (InputContext::TYPE_CONVERTED);
    PYZY_EXPECT_STR (observer.committed (), "nihk");

    context->reset ();
    insertKeys (context.get (), "nihk");
    observer.clear ();
    PYZY_EXPECT (context->selectCandidate (0));
    PYZY_EXPECT_STR (observer.committed (), "你好");
    PYZY_EXPECT_STR (context->inputText (), "");
    PYZY_EXPECT (!context->hasCandidate (0));
}

/* The keyboard schema is what turns key pairs into syllables, so switching it
 * has to be honoured. */
void checkSchemaProperty ()
{
    Recorder observer;
    std::unique_ptr<InputContext> context (
        InputContext::create (InputContext::DOUBLE_PINYIN, &observer));

    /* MSPY is the documented default, and the expectation above is for it. */
    PYZY_EXPECT_INT (
        context->getProperty (InputContext::PROPERTY_DOUBLE_PINYIN_SCHEMA)
                .getUnsignedInt (),
        DOUBLE_PINYIN_KEYBOARD_MSPY);

    /* Every schema must be settable and read back, and the same keystrokes
     * must not mean the same thing under all six — several of the schemas are
     * near-identical to each other (ZRM differs from MSPY in two cells), so
     * this compares the whole set rather than any particular pair. */
    std::set<std::string> renderings;
    for (unsigned int schema = 0; schema < DOUBLE_PINYIN_KEYBOARD_LAST; ++schema) {
        context->reset ();
        PYZY_EXPECT (context->setProperty (
            InputContext::PROPERTY_DOUBLE_PINYIN_SCHEMA,
            PyZy::Variant::fromUnsignedInt (schema)));
        PYZY_EXPECT_INT (
            context->getProperty (InputContext::PROPERTY_DOUBLE_PINYIN_SCHEMA)
                    .getUnsignedInt (),
            schema);
        insertKeys (context.get (), "nihk");
        renderings.insert (context->auxiliaryText ());
    }
    PYZY_EXPECT (renderings.size () > 1);

    /* An out-of-range schema must be rejected rather than indexing off the end
     * of the keyboard table. */
    PYZY_EXPECT (!context->setProperty (
        InputContext::PROPERTY_DOUBLE_PINYIN_SCHEMA,
        PyZy::Variant::fromUnsignedInt (DOUBLE_PINYIN_KEYBOARD_LAST)));
}

}  // namespace

int main ()
{
    InputContext::init (pyzy_test::userCacheDir (), pyzy_test::userConfigDir ());

    checkConversion ();
    checkCommitAndSelect ();
    checkSchemaProperty ();

    InputContext::finalize ();
    return PYZY_TEST_RESULT;
}
