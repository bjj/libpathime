/*
 * Implementation of the engine-agnostic key-event layer declared in keys.h.
 */

#include "keys.h"

#include <cstddef>
#include <cstring>

namespace pathime {

namespace {

/*
 * The accepted layouts of pathime_key_event_t. Exactly one for now, so this is
 * a one-element set; it grows by appending when a field is added, at which
 * point an older caller's smaller struct stays usable and the fields it lacks
 * read as zero. A *larger* value stays an error either way: it means the caller
 * set fields this library would silently ignore.
 */
constexpr size_t kKeyEventStructSizes[] = {sizeof(pathime_key_event_t)};

bool known_struct_size(size_t value)
{
    for (size_t known : kKeyEventStructSizes) {
        if (value == known) {
            return true;
        }
    }
    return false;
}

/* The X11 encoding for scalars at or above U+0100. */
constexpr uint32_t kUnicodeKeysymBase = 0x01000000u;
constexpr uint32_t kUnicodeKeysymMask = 0x00FFFFFFu;

}  // namespace

pathime_status_t key_event_from_public(const pathime_key_event_t *event, KeyEvent *out)
{
    if (event == nullptr || !known_struct_size(event->struct_size)) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    /*
     * The one thing the library checks about a keysym. Zero is not a key; it
     * is what an uninitialized struct holds, so rejecting it catches the
     * commonest client mistake without pretending to validate a space that
     * cannot be validated.
     */
    if (event->keysym == 0) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    out->keysym = event->keysym;
    out->layout_key = event->layout_key;
    out->modifiers = event->modifiers;
    return PATHIME_OK;
}

bool is_chorded(const KeyEvent &key)
{
    return key.has(PATHIME_MOD_CONTROL) || key.has(PATHIME_MOD_ALT) ||
           key.has(PATHIME_MOD_SUPER);
}

uint32_t keysym_to_scalar(uint32_t keysym)
{
    /*
     * Below U+0100 the keysym *is* the scalar — the Latin-1 identity the X11
     * keysym space is built on. This range also contains PATHIME_KEY_SPACE
     * (0x0020), which is printable and deliberately so: it is both a character
     * and the usual convert key, and which of the two it means is the
     * adapter's decision rather than this function's.
     */
    if (keysym >= 0x20u && keysym < 0x100u) {
        if (keysym == 0x7Fu) {
            return 0;  /* DEL is a control character, not a printable one. */
        }
        return keysym;
    }

    if ((keysym & ~kUnicodeKeysymMask) == kUnicodeKeysymBase) {
        const uint32_t scalar = keysym & kUnicodeKeysymMask;
        /* The encoding can express values that are not scalar values at all.
         * A client that sends one gets "not printable" here rather than a
         * malformed character somewhere downstream. */
        if (scalar == 0 || scalar > 0x10FFFFu ||
            (scalar >= 0xD800u && scalar <= 0xDFFFu)) {
            return 0;
        }
        return scalar;
    }

    /*
     * Everything else: the 0xff00 page of named keys, the function keys, and
     * whatever a client has invented. None of them denotes a character.
     */
    return 0;
}

char keysym_to_ascii(uint32_t keysym)
{
    const uint32_t scalar = keysym_to_scalar(keysym);
    if (scalar < 0x20u || scalar > 0x7Eu) {
        return 0;
    }
    return static_cast<char>(scalar);
}

namespace {

/** The character a US-QWERTY key produces with Shift held. */
char shifted_ascii(char c)
{
    if (c >= 'a' && c <= 'z') {
        return static_cast<char>(c - 'a' + 'A');
    }
    static const char kUnshifted[] = "`1234567890-=[]\\;',./";
    static const char kShifted[]   = "~!@#$%^&*()_+{}|:\"<>?";
    static_assert(sizeof(kUnshifted) == sizeof(kShifted),
                  "the two halves of the US-QWERTY shift row must line up "
                  "position for position; the lookup below indexes one with an "
                  "offset found in the other");
    if (c != '\0') {
        const char *p = std::strchr(kUnshifted, c);
        if (p != nullptr && *p != '\0') {
            return kShifted[p - kUnshifted];
        }
    }
    return c;
}

}  // namespace

char us_layout_char(const KeyEvent &key)
{
    const uint32_t k = key.position_key();
    if (k < 0x20u || k > 0x7Eu) {
        return 0;
    }

    char c = static_cast<char>(k);

    if (key.layout_key == 0 && key.has(PATHIME_MOD_CAPS)) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        } else if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }

    if (key.has(PATHIME_MOD_SHIFT)) {
        c = shifted_ascii(c);
    }
    return c;
}

}  // namespace pathime
