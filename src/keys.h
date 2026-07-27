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
 */

#ifndef LIBPATHIME_SRC_KEYS_H
#define LIBPATHIME_SRC_KEYS_H

#include <pathime/pathime.h>

namespace pathime {

/* Dispatch types to be defined. */

}  // namespace pathime

#endif /* LIBPATHIME_SRC_KEYS_H */
