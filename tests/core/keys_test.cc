/*
 * src/keys.cc: what a valid key event is, and how an X11 keysym becomes a
 * character.
 *
 * This is the layer every engine sits behind, and the reason it is worth
 * testing directly is that most of it is unreachable from tests/api in any
 * useful way. The validation answers are statuses a client provokes by getting
 * the struct wrong, which the API tests have no reason to do; and the keysym
 * decoding is only observable through an engine, which by then has folded the
 * answer into a composition. Called here, each rule is one assertion.
 *
 * The X11 rules under test are the ones include/pathime/pathime.h commits to in
 * writing, so a change that breaks an assertion here is a change to the public
 * contract rather than to an implementation detail.
 */

#include <cstring>

#include "keys.h"

#include "core_test_util.h"

using namespace pathime;

namespace {

/* A well-formed event, so each test can vary one field from a valid baseline. */
pathime_key_event_t make_event(uint32_t keysym)
{
    pathime_key_event_t event;
    std::memset(&event, 0, sizeof event);
    event.struct_size = sizeof event;
    event.keysym = keysym;
    return event;
}

KeyEvent key(uint32_t keysym, uint32_t layout_key = 0, uint32_t modifiers = 0)
{
    KeyEvent out;
    out.keysym = keysym;
    out.layout_key = layout_key;
    out.modifiers = modifiers;
    return out;
}

/*
 * key_event_from_public() checks exactly two things, and the header promises it
 * checks no more. Both halves are asserted: the rejections below, and the
 * pass-through of an unrecognized keysym further down — an engine reporting a
 * key unhandled is a different outcome from the library refusing it, and a
 * client that invents keysyms depends on the difference.
 */
void test_event_validation()
{
    KeyEvent out;

    /* A null event is a client error, not a crash. */
    PT_CHECK(key_event_from_public(nullptr, &out) == PATHIME_ERROR_INVALID_ARGUMENT);

    /*
     * struct_size must be a layout this library knows. Zero is what a caller who
     * forgot to set it has; a larger value means the caller populated fields
     * this build would silently ignore, which is the case the check exists for.
     * Both are errors, and so is a value merely close to the real one.
     */
    pathime_key_event_t zero_size = make_event(PATHIME_KEY_SPACE);
    zero_size.struct_size = 0;
    PT_CHECK(key_event_from_public(&zero_size, &out) == PATHIME_ERROR_INVALID_ARGUMENT);

    pathime_key_event_t too_large = make_event(PATHIME_KEY_SPACE);
    too_large.struct_size = sizeof(pathime_key_event_t) + 1;
    PT_CHECK(key_event_from_public(&too_large, &out) == PATHIME_ERROR_INVALID_ARGUMENT);

    pathime_key_event_t too_small = make_event(PATHIME_KEY_SPACE);
    too_small.struct_size = sizeof(pathime_key_event_t) - 1;
    PT_CHECK(key_event_from_public(&too_small, &out) == PATHIME_ERROR_INVALID_ARGUMENT);

    /* Zero is not a key; it is what an uninitialized struct holds. */
    pathime_key_event_t no_keysym = make_event(0);
    PT_CHECK(key_event_from_public(&no_keysym, &out) == PATHIME_ERROR_INVALID_ARGUMENT);

    /* A valid event copies all three fields through unchanged. */
    pathime_key_event_t good = make_event('a');
    good.layout_key = 'q';
    good.modifiers = PATHIME_MOD_SHIFT | PATHIME_MOD_CAPS;
    PT_CHECK(key_event_from_public(&good, &out) == PATHIME_OK);
    PT_CHECK(out.keysym == 'a');
    PT_CHECK(out.layout_key == 'q');
    PT_CHECK(out.modifiers == (PATHIME_MOD_SHIFT | PATHIME_MOD_CAPS));

    /*
     * A keysym this library has never heard of is accepted, not rejected. The
     * keysym space is open-ended and the header says so: engines dispatch on
     * what they know and report the rest unhandled, which is what a client
     * wants for keys it invented.
     */
    pathime_key_event_t invented = make_event(0x0EFFFFFFu);
    PT_CHECK(key_event_from_public(&invented, &out) == PATHIME_OK);
    PT_CHECK(out.keysym == 0x0EFFFFFFu);
}

/*
 * keysym_to_scalar() is the printable test the whole dispatch rests on, and its
 * two ranges have sharp edges on both sides. The boundaries are asserted rather
 * than sampled: 0x1F/0x20 and 0xFF/0x100 for the Latin-1 identity, and
 * U+10FFFF/U+110000 for the encoded form.
 */
void test_keysym_to_scalar_latin1()
{
    /* Below U+0100 the keysym is the scalar. Space is in, and deliberately: it
     * is both a character and the usual convert key. */
    PT_CHECK(keysym_to_scalar(0x20u) == 0x20u);
    PT_CHECK(keysym_to_scalar('a') == 'a');
    PT_CHECK(keysym_to_scalar('~') == '~');
    PT_CHECK(keysym_to_scalar(0xE9u) == 0xE9u);  /* é */
    PT_CHECK(keysym_to_scalar(0xFFu) == 0xFFu);  /* ÿ, the last of the range */

    /* Control characters below the range denote nothing. */
    PT_CHECK(keysym_to_scalar(0x1Fu) == 0);
    PT_CHECK(keysym_to_scalar(0x01u) == 0);

    /* DEL sits inside the numeric range but is a control character. */
    PT_CHECK(keysym_to_scalar(0x7Fu) == 0);

    /* 0x100 is past the identity range and is not the encoded form either. */
    PT_CHECK(keysym_to_scalar(0x100u) == 0);
}

void test_keysym_to_scalar_unicode()
{
    /* 0x01000000 + scalar, the X11 encoding for everything at U+0100 and up. */
    PT_CHECK(keysym_to_scalar(0x01000100u) == 0x100u);
    PT_CHECK(keysym_to_scalar(0x01003042u) == 0x3042u);   /* あ */
    PT_CHECK(keysym_to_scalar(0x01020BB7u) == 0x20BB7u);  /* 𠮷, plane 2 */
    PT_CHECK(keysym_to_scalar(0x0110FFFFu) == 0x10FFFFu); /* the last scalar */

    /*
     * The encoding can express values that are not scalar values. A client
     * sending one gets "not printable" rather than a malformed character
     * somewhere downstream, so each malformed class is checked separately.
     */
    PT_CHECK(keysym_to_scalar(0x01000000u) == 0);  /* scalar 0 */
    PT_CHECK(keysym_to_scalar(0x01110000u) == 0);  /* past U+10FFFF */
    PT_CHECK(keysym_to_scalar(0x0100D800u) == 0);  /* leading surrogate */
    PT_CHECK(keysym_to_scalar(0x0100DFFFu) == 0);  /* trailing surrogate */

    /* The scalars either side of the surrogate block stay valid. */
    PT_CHECK(keysym_to_scalar(0x0100D7FFu) == 0xD7FFu);
    PT_CHECK(keysym_to_scalar(0x0100E000u) == 0xE000u);
}

void test_keysym_to_scalar_named_keys()
{
    /*
     * Every PATHIME_KEY_* except SPACE sits in the 0xff00 page and denotes no
     * character. This is what makes keysym_to_scalar() usable as the printable
     * test rather than merely as a converter.
     */
    PT_CHECK(keysym_to_scalar(PATHIME_KEY_BACKSPACE) == 0);
    PT_CHECK(keysym_to_scalar(PATHIME_KEY_RETURN) == 0);
    PT_CHECK(keysym_to_scalar(PATHIME_KEY_ESCAPE) == 0);
    PT_CHECK(keysym_to_scalar(PATHIME_KEY_LEFT) == 0);

    /* SPACE is the exception, and the one every engine has to decide about. */
    PT_CHECK(keysym_to_scalar(PATHIME_KEY_SPACE) == 0x20u);

    /* A keysym in no recognized page at all. */
    PT_CHECK(keysym_to_scalar(0x02000041u) == 0);
}

void test_keysym_to_ascii()
{
    /* The narrow form libhangul and pyzy both want: printable ASCII or 0. */
    PT_CHECK(keysym_to_ascii('a') == 'a');
    PT_CHECK(keysym_to_ascii(0x20u) == ' ');
    PT_CHECK(keysym_to_ascii(0x7Eu) == '~');
    PT_CHECK(keysym_to_ascii(0x7Fu) == 0);

    /* A real scalar that is not ASCII answers 0 rather than truncating. */
    PT_CHECK(keysym_to_ascii(0xE9u) == 0);        /* é */
    PT_CHECK(keysym_to_ascii(0x01003042u) == 0);  /* あ */
    PT_CHECK(keysym_to_ascii(PATHIME_KEY_BACKSPACE) == 0);
}

void test_is_chorded()
{
    /* Control, Alt and Super mean "this is a client shortcut". */
    PT_CHECK(is_chorded(key('c', 0, PATHIME_MOD_CONTROL)));
    PT_CHECK(is_chorded(key('c', 0, PATHIME_MOD_ALT)));
    PT_CHECK(is_chorded(key('c', 0, PATHIME_MOD_SUPER)));
    PT_CHECK(is_chorded(key('c', 0, PATHIME_MOD_CONTROL | PATHIME_MOD_SHIFT)));

    /*
     * Shift, Caps and NumLock are deliberately excluded: Shift reaches a
     * shifted key position and Caps lets an engine undo the layout's
     * capitalization. Neither means "shortcut", and an engine that treated them
     * as one would stop accepting capital letters.
     */
    PT_CHECK(!is_chorded(key('c')));
    PT_CHECK(!is_chorded(key('c', 0, PATHIME_MOD_SHIFT)));
    PT_CHECK(!is_chorded(key('c', 0, PATHIME_MOD_CAPS)));
    PT_CHECK(!is_chorded(key('c', 0, PATHIME_MOD_NUMLOCK)));
    PT_CHECK(!is_chorded(key('c', 0, PATHIME_MOD_SHIFT | PATHIME_MOD_CAPS)));
}

void test_us_layout_char_shift()
{
    /* Position with no modifiers is the character printed on the key. */
    PT_CHECK(us_layout_char(key(0, 'a')) == 'a');
    PT_CHECK(us_layout_char(key(0, '1')) == '1');

    /* Shift is folded back in here, because layout_key is unmodified by
     * construction and an engine dispatching on position cannot do it alone. */
    PT_CHECK(us_layout_char(key(0, 'a', PATHIME_MOD_SHIFT)) == 'A');
    PT_CHECK(us_layout_char(key(0, '1', PATHIME_MOD_SHIFT)) == '!');
    PT_CHECK(us_layout_char(key(0, '/', PATHIME_MOD_SHIFT)) == '?');
    PT_CHECK(us_layout_char(key(0, '`', PATHIME_MOD_SHIFT)) == '~');
    PT_CHECK(us_layout_char(key(0, '\\', PATHIME_MOD_SHIFT)) == '|');
    PT_CHECK(us_layout_char(key(0, '\'', PATHIME_MOD_SHIFT)) == '"');

    /*
     * A position with no shifted form is returned unchanged. Space and an
     * already-uppercase position are both in this class — neither is a
     * lowercase letter nor an entry in the US-QWERTY shift row.
     */
    PT_CHECK(us_layout_char(key(0, ' ', PATHIME_MOD_SHIFT)) == ' ');
    PT_CHECK(us_layout_char(key(0, 'A', PATHIME_MOD_SHIFT)) == 'A');
}

void test_us_layout_char_fallback_and_caps()
{
    /* With no physical key reported, the keysym stands in for the position. */
    PT_CHECK(us_layout_char(key('a', 0)) == 'a');
    PT_CHECK(us_layout_char(key('a', 0, PATHIME_MOD_SHIFT)) == 'A');

    /*
     * CapsLock is undone only on that fallback path. layout_key can carry no
     * lock, having never had one applied; a keysym carries whatever
     * capitalization the client's layout produced, and an engine reading
     * position wants the key rather than the lock state.
     *
     * The correction inverts in both directions, which is what makes it a
     * correction rather than a lowercasing.
     */
    PT_CHECK(us_layout_char(key('a', 0, PATHIME_MOD_CAPS)) == 'A');
    PT_CHECK(us_layout_char(key('A', 0, PATHIME_MOD_CAPS)) == 'a');

    /* With a physical key reported, Caps changes nothing. */
    PT_CHECK(us_layout_char(key('A', 'a', PATHIME_MOD_CAPS)) == 'a');

    /* Caps then Shift compose: 'a' inverts to 'A', which has no shifted form. */
    PT_CHECK(us_layout_char(key('a', 0, PATHIME_MOD_CAPS | PATHIME_MOD_SHIFT)) == 'A');

    /* Caps leaves a non-letter alone. */
    PT_CHECK(us_layout_char(key('1', 0, PATHIME_MOD_CAPS)) == '1');

    /* A position outside printable ASCII is not a character at all. */
    PT_CHECK(us_layout_char(key(PATHIME_KEY_BACKSPACE, 0)) == 0);
    PT_CHECK(us_layout_char(key(0, PATHIME_KEY_LEFT)) == 0);
    PT_CHECK(us_layout_char(key(0x01003042u, 0)) == 0);
}

}  // namespace

int main(void)
{
    test_event_validation();
    test_keysym_to_scalar_latin1();
    test_keysym_to_scalar_unicode();
    test_keysym_to_scalar_named_keys();
    test_keysym_to_ascii();
    test_is_chorded();
    test_us_layout_char_shift();
    test_us_layout_char_fallback_and_caps();
    return pt_report("core.keys");
}
