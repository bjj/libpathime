#include "keymap.h"

#include <cstdint>
#include <cstdio>

namespace demo {
namespace {

/*
 * The US-QWERTY shifted characters, paired with the key they sit on. Reading
 * this table backwards is how layout_key is recovered: '!' is the 1 key with
 * Shift, so the event is keysym '!', layout_key '1', PATHIME_MOD_SHIFT.
 *
 * The header requires exactly this split — layout_key is the keysym the
 * physical key would produce *unmodified*, so it is independent of the
 * modifier mask by construction, and an engine that cares about position
 * recombines the two itself.
 */
struct ShiftedKey { char shifted; char unshifted; };

const ShiftedKey kShifted[] = {
    {'!', '1'}, {'@', '2'}, {'#', '3'}, {'$', '4'}, {'%', '5'},
    {'^', '6'}, {'&', '7'}, {'*', '8'}, {'(', '9'}, {')', '0'},
    {'_', '-'}, {'+', '='}, {'{', '['}, {'}', ']'}, {'|', '\\'},
    {':', ';'}, {'"', '\''}, {'<', ','}, {'>', '.'}, {'?', '/'},
    {'~', '`'},
};

/* The keysym for a Unicode scalar, by the X11 rule the public header commits
 * to: below U+0100 the keysym is the scalar, above it 0x01000000 + scalar. */
std::uint32_t keysym_for_scalar(std::uint32_t scalar)
{
    return scalar < 0x100u ? scalar : 0x01000000u + scalar;
}

}  // namespace

bool to_pathime_key(const Term::Key &key, pathime_key_event_t *out)
{
    out->struct_size = sizeof(*out);
    out->keysym = 0;
    out->layout_key = 0;
    out->modifiers = 0;

    /*
     * The named keys first, because four of them are control characters and
     * would otherwise be mistaken for Ctrl chords: Backspace is Ctrl+H, Tab is
     * Ctrl+I, Enter is Ctrl+M and Escape is Ctrl+[. Term::Key::hasCtrl()
     * excludes exactly those four for the same reason.
     */
    switch (key.value) {
    case Term::Key::Backspace: out->keysym = PATHIME_KEY_BACKSPACE; break;
    case Term::Key::Tab:       out->keysym = PATHIME_KEY_TAB;       break;
    case Term::Key::Enter:     out->keysym = PATHIME_KEY_RETURN;    break;
    case Term::Key::Esc:       out->keysym = PATHIME_KEY_ESCAPE;    break;
    /* cpp-terminal folds the raw DEL byte a Unix terminal's Backspace sends
     * into Key::Backspace above, and gives Key::Del only to the Delete key's
     * CSI 3~ sequence. So this really is Delete. */
    case Term::Key::Del:       out->keysym = PATHIME_KEY_DELETE;    break;
    case Term::Key::ArrowLeft:  out->keysym = PATHIME_KEY_LEFT;      break;
    case Term::Key::ArrowRight: out->keysym = PATHIME_KEY_RIGHT;     break;
    case Term::Key::ArrowUp:    out->keysym = PATHIME_KEY_UP;        break;
    case Term::Key::ArrowDown:  out->keysym = PATHIME_KEY_DOWN;      break;
    case Term::Key::Home:       out->keysym = PATHIME_KEY_HOME;      break;
    case Term::Key::End:        out->keysym = PATHIME_KEY_END;       break;
    default: break;
    }
    if (out->keysym != 0) {
        out->layout_key = out->keysym;
        return true;
    }

    /* Chorded keys are this program's shortcuts and never reach an engine. */
    if (key.hasCtrl() || key.hasAlt()) return false;

    /* Everything else has to be a printable scalar to mean anything. */
    if (!key.isunicode() || !key.isprint()) return false;

    const std::uint32_t scalar = static_cast<std::uint32_t>(key.value);
    out->keysym = keysym_for_scalar(scalar);

    if (scalar < 0x80u) {
        const char c = static_cast<char>(scalar);
        char unshifted = c;
        bool shift = false;
        if (c >= 'A' && c <= 'Z') {
            unshifted = static_cast<char>(c - 'A' + 'a');
            shift = true;
        } else {
            for (const ShiftedKey &entry : kShifted) {
                if (entry.shifted == c) {
                    unshifted = entry.unshifted;
                    shift = true;
                    break;
                }
            }
        }
        out->layout_key = static_cast<std::uint32_t>(
            static_cast<unsigned char>(unshifted));
        if (shift) out->modifiers |= PATHIME_MOD_SHIFT;
    } else {
        /* A non-ASCII character came from an input method of the user's own,
         * or from a paste. There is no US-QWERTY position for it, and 0 is
         * what the header asks a client with no physical key to report. */
        out->layout_key = 0;
    }
    return true;
}

std::string key_label(const Term::Key &key)
{
    /* cpp-terminal's own name() for the named and chorded keys; the character
     * itself, quoted, for anything printable. */
    if (key.isprint() && !key.hasCtrl() && !key.hasAlt() && key.value < 0x110000)
        return "'" + key.str() + "'";
    return key.name();
}

bool keysym_scalar(std::uint32_t keysym, std::uint32_t *out)
{
    /* The named keys — every PATHIME_KEY_* constant except SPACE — sit in the
     * 0xff00 page, which is what makes the low range usable as the printable
     * test. */
    if (keysym < 0x100u) {
        *out = keysym;
        return true;
    }
    if ((keysym & 0xFF000000u) == 0x01000000u) {
        *out = keysym & 0x00FFFFFFu;
        return true;
    }
    return false;
}

std::string event_label(const pathime_key_event_t &event)
{
    char buf[64];
    const char *mods = "";
    if (event.modifiers & PATHIME_MOD_SHIFT) mods = " +shift";
    std::snprintf(buf, sizeof(buf), "keysym 0x%04x layout 0x%04x%s",
                  event.keysym, event.layout_key, mods);
    return buf;
}

}  // namespace demo
