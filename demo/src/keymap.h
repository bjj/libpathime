/*
 * cpp-terminal key presses to pathime_key_event_t.
 *
 * This is the part of a client that platforms normally supply and that a
 * terminal does not: <pathime/pathime.h> asks for an X11 keysym, the physical
 * key as a US-QWERTY keysym, and a modifier mask, and a terminal delivers a
 * decoded character with no key position and no modifier state beyond what the
 * character itself implies. What can be recovered is recovered here, and what
 * cannot is documented rather than guessed at.
 *
 * What a terminal cannot tell us, and what it costs:
 *
 *   - The physical key. A terminal reports 'Q', not "the key left of W with
 *     Shift held". So layout_key is *derived* from the character on the
 *     assumption that the user is on US QWERTY — '!' means the 1 key with
 *     Shift, and so on. On any other physical layout that derivation is wrong,
 *     and the two engines that read key position (Hangul, and Japanese under
 *     PATHIME_ANTHY_TYPING_KANA) will produce the wrong jamo or kana. A real
 *     client on a real windowing system reports the true scancode and has no
 *     such problem.
 *
 *   - CapsLock, NumLock, and Super. Nothing in a terminal's input stream says
 *     whether a lock is latched, so those bits are never set.
 *
 *   - Shift on a key whose shifted form is the same character — Shift+Space,
 *     Shift+Enter. Both arrive indistinguishable from the unshifted key.
 *
 *   - Key releases, which this API does not represent either, so nothing is
 *     lost there.
 */

#ifndef PATHIME_DEMO_KEYMAP_H
#define PATHIME_DEMO_KEYMAP_H

#include <cstdint>
#include <string>

#include <cpp-terminal/key.hpp>
#include <pathime/pathime.h>

namespace demo {

/**
 * Translate a terminal key press into a libpathime key event.
 *
 * Returns false for keys that have no business reaching an engine: Ctrl and
 * Alt chords, which are this program's own shortcuts, and function keys, which
 * no engine dispatches on. That is the client's side of the boundary the
 * header describes under PATHIME_MOD_CONTROL — a chorded key is a client
 * shortcut, and the library's engines would decline it anyway.
 */
bool to_pathime_key(const Term::Key &key, pathime_key_event_t *out);

/** A short human-readable name for a key press, for the status line. */
std::string key_label(const Term::Key &key);

/** A short human-readable name for a translated event, e.g. "keysym 0x61 'a'". */
std::string event_label(const pathime_key_event_t &event);

/**
 * The Unicode scalar a keysym denotes, or false if it denotes none — the
 * inverse of the rule to_pathime_key() applies, and what the demo needs to
 * decide whether a key the engine declined is one it should type itself.
 */
bool keysym_scalar(std::uint32_t keysym, std::uint32_t *out);

}  // namespace demo

#endif /* PATHIME_DEMO_KEYMAP_H */
