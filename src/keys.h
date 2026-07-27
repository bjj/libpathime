/*
 * The engine-agnostic half of the key-event layer.
 *
 * The backends accept only finished input (TODO.md §2, Finding 6): anthy
 * wants completed kana, pyzy accepts only [a-z] and apostrophe, libhangul
 * takes a US-QWERTY int with uppercase meaning Shift and backspace as a
 * separate call. Everything from pathime_key_event_t down — validation,
 * modifier handling, the PATHIME_KEY_* special keys, routing into the
 * engine's front end, and the handled/unhandled verdict — is ours, and the
 * part of it that does not depend on which engine is loaded lives here.
 *
 * The per-engine composing front ends are deliberately *not* here: the
 * romaji/kana state machine is engines/anthy/romaji.*, because only Japanese
 * needs one before its backend sees input (the per-engine answer to TODO.md
 * §3, question 2 — recorded in docs/source-layout.md, cheap to hoist if the
 * table engine turns out to want it shared).
 *
 * What this file settles, once, so that three adapters cannot each answer it
 * differently: what a valid event is, what counts as a chorded shortcut, and
 * how an X11 keysym becomes a Unicode scalar.
 */

#ifndef LIBPATHIME_SRC_KEYS_H
#define LIBPATHIME_SRC_KEYS_H

#include <cstdint>

#include <pathime/pathime.h>

#include "backend.h"

namespace pathime {

/**
 * Validate a client-supplied event and convert it to the internal form.
 *
 * Checks exactly what the public header promises is checked, and no more.
 * struct_size must be one the library recognizes, and keysym must be nonzero;
 * keysyms are otherwise passed through unvalidated, because the X11 keysym
 * space is open-ended and no useful membership test exists. An unrecognized
 * value is not an error — engines dispatch on the ones they know and report
 * the rest unhandled, which is also what a client wants for keys it invented.
 *
 * @return PATHIME_ERROR_INVALID_ARGUMENT, or PATHIME_OK with @a out written.
 */
pathime_status_t key_event_from_public(const pathime_key_event_t *event, KeyEvent *out);

/**
 * True if the event carries Control, Alt or Super.
 *
 * A chorded key is a client shortcut, and this is the main reason modifiers
 * have to reach the engine at all: engines decline these rather than absorb
 * them, so Ctrl+C stays Ctrl+C instead of becoming a character in someone's
 * preedit. Shift, CapsLock and NumLock are deliberately excluded — Shift
 * reaches a shifted key position, Caps lets an engine undo the layout's
 * capitalization, and neither means "this is a shortcut".
 */
bool is_chorded(const KeyEvent &key);

/**
 * The Unicode scalar a printable keysym denotes, or 0 if it denotes none.
 *
 * The X11 rule the public header commits to: for characters below U+0100 the
 * keysym equals the scalar, above that it is 0x01000000 + the scalar. Named
 * keys — every PATHIME_KEY_* constant except SPACE — sit in the 0xff00 page
 * and yield 0, which is what makes this usable as the printable test.
 */
uint32_t keysym_to_scalar(uint32_t keysym);

/**
 * The ASCII character a keysym denotes, or 0 for anything that is not
 * printable ASCII.
 *
 * The narrow form libhangul and pyzy both want: libhangul takes a US-QWERTY
 * int with case carrying the shift state, pyzy accepts [a-z] and apostrophe.
 * Anything outside 0x20..0x7E answers 0.
 */
char keysym_to_ascii(uint32_t keysym);

}  // namespace pathime

#endif /* LIBPATHIME_SRC_KEYS_H */
