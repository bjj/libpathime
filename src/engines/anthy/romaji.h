/*
 * The romaji/kana composing front end — entirely ours to write (Finding 6):
 * anthy takes completed kana, so everything between pathime_key_event_t and
 * kana happens here, including the kana-script projection
 * (PATHIME_OPT_ANTHY_KANA_SCRIPT).
 *
 * PATHIME_OPT_ANTHY_TYPING_METHOD chooses the state machine (romaji vs kana
 * entry), not the conversion table: ibus-anthy's schema ships exactly one
 * romaji table — the MS-IME/ATOK/etc. variants named in its comments were
 * never written — so there is no table choice to offer yet (TODO.md §1).
 * Thumb-shift entry was cut. The reference state machine is ibus-anthy's
 * (refs/ibus-anthy, Python).
 *
 * This lives with the anthy adapter rather than in src/ because only
 * Japanese needs a composing front end before its backend sees input — the
 * per-engine answer to TODO.md §3, question 2. If the table engine turns out
 * to want it shared, hoisting it into src/ is cheap.
 */

#ifndef LIBPATHIME_SRC_ENGINES_ANTHY_ROMAJI_H
#define LIBPATHIME_SRC_ENGINES_ANTHY_ROMAJI_H

namespace pathime {

/* State machine to be defined. */

}  // namespace pathime

#endif /* LIBPATHIME_SRC_ENGINES_ANTHY_ROMAJI_H */
