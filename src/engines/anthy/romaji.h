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
 * Thumb-shift entry was cut. The reference state machines are ibus-anthy's
 * (refs/ibus-anthy, Python): romaji.py for one, kana.py for the other.
 *
 * The two machines differ in what they read off a key, and the difference is
 * not incidental. Romaji entry reads the *character*, because the user is
 * spelling a syllable out. Kana entry reads the *position*, because the user
 * is striking a key whose kana legend no client keymap will report — so it
 * goes through KeyEvent::position_key() and keys.cc's us_layout_char(), the
 * same recombination libhangul's adapter uses, over a table indexed by US-101
 * positions.
 *
 * This lives with the anthy adapter rather than in src/ because only
 * Japanese needs a composing front end before its backend sees input — the
 * per-engine answer to TODO.md §3, question 2. If the table engine turns out
 * to want it shared, hoisting it into src/ is cheap.
 *
 * ---------------------------------------------------------------------------
 * The state, and why it is two strings rather than a segment list
 * ---------------------------------------------------------------------------
 *
 * ibus-anthy keeps a list of RomajiSegment objects, each pairing the Latin
 * characters typed with the kana they resolved to, because it needs a cursor
 * that moves through the pre-conversion buffer and a raw-Latin view for its
 * pseudo-ASCII mode. Neither survives into this API: composition.h has no
 * cursor inside a span, and there is no Latin passthrough mode. What is left
 * is a resolved kana run plus whatever Latin has not resolved yet, so the
 * state is exactly those two strings.
 *
 * The kana run is always **hiragana**. PATHIME_OPT_ANTHY_KANA_SCRIPT is a
 * projection applied on the way out, not a second internal representation —
 * anthy is fed hiragana whatever the script setting is, which is also what
 * ibus-anthy does (it calls get_hiragana() before every set_string,
 * engine.py:1114 and 1387).
 */

#ifndef LIBPATHIME_SRC_ENGINES_ANTHY_ROMAJI_H
#define LIBPATHIME_SRC_ENGINES_ANTHY_ROMAJI_H

#include <string>

#include <pathime/pathime.h>

#include "backend.h"

namespace pathime {

/**
 * The options the front end consults, read once per call at the seam.
 *
 * A snapshot rather than a stored copy, because options are pulled and not
 * pushed (backend.h rule 4) and a change must take effect immediately; a
 * snapshot taken at the top of each key dispatch gives that without the
 * composer caching anything it would then have to invalidate. It is a plain
 * struct so the state machine itself never names an OptionReader and stays
 * testable from a scratch program.
 */
struct RomajiSettings {
    pathime_anthy_typing_t method           = PATHIME_ANTHY_TYPING_ROMAJI;
    pathime_anthy_script_t script           = PATHIME_ANTHY_SCRIPT_HIRAGANA;
    pathime_anthy_period_t period           = PATHIME_ANTHY_PERIOD_KUTEN;
    pathime_anthy_symbol_t symbol           = PATHIME_ANTHY_SYMBOL_CORNER_SLASH;
    bool                   latin_with_shift = true;

    /**
     * The two width options reach the front end because this is where a digit
     * or a punctuation key becomes text at all: ibus-anthy applies them in
     * JaString._chk_text (jastring.py:243), on the same pass as the period and
     * symbol styles. The defaults are not the same — half-width digits with
     * full-width punctuation — so a composer that ignored them would produce
     * １ for "1" out of the box, which is wrong by the header's own default.
     */
    pathime_width_t latin_width       = PATHIME_WIDTH_HALF;
    pathime_width_t punctuation_width = PATHIME_WIDTH_FULL;
};

/** Read the seven values above out of @a options. */
RomajiSettings romaji_settings(const OptionReader &options);

/**
 * The composing state machine: key events in, kana out.
 *
 * It is a state machine and not a table lookup because of the cases where a
 * key cannot be resolved until the next one arrives — "k" pends, "kk" is っ
 * plus a still-pending "k", "n" is ん only once a consonant or a second "n"
 * follows. Those are the pending-consonant rules ibus-anthy spells as three
 * separate tables (romaji_typing_rule_static, the double-consonant rule and
 * the n-correction rule, tables.py:25, 314 and 337); the first is a table
 * here too, the other two are computed, since both are rules over the
 * alphabet rather than lists worth carrying.
 */
class RomajiComposer {
public:
    /** True when nothing has been typed: no kana and no pending Latin. */
    bool empty() const { return kana_.empty() && pending_.empty(); }

    /** True when Latin characters are waiting for the key that resolves them. */
    bool has_pending() const { return !pending_.empty(); }

    void clear()
    {
        kana_.clear();
        pending_.clear();
    }

    /**
     * Replace the buffer with @a kana, dropping any pending Latin. The anthy
     * adapter's eager state uses this to re-seed the composer with the
     * readings anthy reports for the spans a selection did not consume.
     *
     * @a kana is stored verbatim, so it must be hiragana — which is what
     * anthy's readings are — plus whatever raw Latin was passed through
     * unresolved. A pending fragment that round-trips this way stops pending:
     * it was handed to anthy as literal characters and comes back as part of
     * the reading, so a later vowel no longer resolves it. That is the cost
     * of selecting mid-word with garbage in the buffer, and it is paid here
     * rather than hidden.
     */
    void assign_kana(const std::string &kana)
    {
        kana_ = kana;
        pending_.clear();
    }

    /**
     * Put @a kana in front of what is typed, leaving the pending Latin alone.
     * This is the un-settle direction: an eager selection is walked back by
     * restoring the span's reading ahead of whatever has been typed since.
     */
    void prepend_kana(const std::string &kana) { kana_.insert(0, kana); }

    /**
     * Offer one key.
     *
     * @return true if the key was consumed. Only printable ASCII is consumed;
     *         everything else — including kana keysyms a JIS layout might
     *         produce — is declined, which is what puts the named keys and the
     *         client's own shortcuts back in the adapter's hands.
     */
    bool insert(const KeyEvent &key, const RomajiSettings &settings);

    /**
     * Delete one unit: a single pending Latin character if any is waiting,
     * otherwise the last kana. Two granularities rather than one because the
     * pending Latin is not yet kana and the user typed it one key at a time.
     *
     * @return false when there was nothing to delete, so the adapter can
     *         report the key unhandled and let the client's own backspace run.
     */
    bool backspace();

    /**
     * What the user should see: the kana projected into the configured script,
     * with any unresolved Latin trailing it exactly as typed. ibus-anthy shows
     * the same thing (Segment.to_hiragana falls back to the Latin characters
     * when a segment has not resolved, segment.py:71-74).
     */
    std::string display(const RomajiSettings &settings) const;

    /**
     * What anthy_set_string() is given: hiragana, whatever the script setting.
     *
     * A single trailing "n" becomes ん, because that is the one pending state a
     * user routinely expects to be finished for them — ibus-anthy's
     * get_hiragana(commit=True) does exactly this and nothing more
     * (jastring.py:261). Any other unresolved Latin is passed through as
     * itself, again as ibus-anthy does; anthy tolerates it and segments around
     * it, and dropping it would silently lose input the user can see.
     */
    std::string reading(const RomajiSettings &settings) const;

    /**
     * What is committed when the user finishes without converting: display()
     * and reading() agreeing at last — the configured script, and the trailing
     * "n" finished.
     *
     * It has to be a third accessor rather than either of the other two.
     * display() must leave the "n" as an "n", because while typing that is the
     * truth: one more key decides whether it is ん or な. reading() must not
     * apply the script, because anthy is fed hiragana. Committing needs both
     * halves, and using display() here is a real bug — it commits "にほn".
     */
    std::string commit_text(const RomajiSettings &settings) const;

private:
    /** The kana with a trailing "n" resolved to ん; still hiragana. */
    std::string finished_kana() const;

    /** Feed one already-lowercased ASCII character to the resolver. */
    void step(char c, const RomajiSettings &settings);

    /**
     * PATHIME_ANTHY_TYPING_KANA's whole state machine: one key, one kana, with
     * the dakuten and handakuten keys folding into the kana before them.
     *
     * Takes no settings because none of them applies — kana entry has no
     * pending prefix for latin_with_shift to interrupt, and the layout rather
     * than the period/symbol options decides what a punctuation key produces.
     */
    bool insert_kana(const KeyEvent &key);

    std::string kana_;     /**< Resolved, always hiragana. */
    std::string pending_;  /**< Latin typed but not yet resolved, as typed. */
};

}  // namespace pathime

#endif /* LIBPATHIME_SRC_ENGINES_ANTHY_ROMAJI_H */
