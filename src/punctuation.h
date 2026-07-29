/*
 * What the Chinese engines emit for a key their converter will not take: the
 * half/full width conversion and the Chinese punctuation substitution behind
 * PATHIME_OPT_LATIN_WIDTH and PATHIME_OPT_PUNCTUATION_WIDTH.
 *
 * ---------------------------------------------------------------------------
 * Why this sits in src/ rather than under one adapter
 * ---------------------------------------------------------------------------
 *
 * Both Chinese engines need it, and the two options must mean one thing. The
 * behaviour here is ibus-pinyin's. ibus-table's own answer, in
 * docs/ibus-table-mapping.md § 11.4, disagrees with it on four characters —
 * `^` (…… against …), `[` and `<` (variant-dependent here, fixed there), and
 * the period, where ibus-table switches on sentence position while this keeps
 * "1.5" intact.
 *
 * Following each reference per engine would make PATHIME_OPT_PUNCTUATION_WIDTH
 * mean two different things depending on which Chinese engine a client had
 * chosen, against an API that deliberately presents one behaviour per concept.
 * So the table engine uses this, and the cost — ibus-table parity on those four
 * characters — is stated here rather than hidden. It is a real cost and a small
 * one; the `1.5` handling is also simply better.
 *
 * No src/engines/common/ appears, because shared code does not get a
 * subdirectory: it belongs in src/ beside the rest of the core.
 *
 * ---------------------------------------------------------------------------
 * Why this is ours to write, and where the reference puts it
 * ---------------------------------------------------------------------------
 *
 * pyzy takes [a-z] and the apostrophe and nothing else. It has no
 * opinion about a comma, and neither of these two options exists anywhere in
 * its API — they are ibus-pinyin's, and in ibus-pinyin they live in a separate
 * *editor*: FallbackEditor, the one that runs when no phonetic editor claims
 * the key (refs/ibus-pinyin/src/PYFallbackEditor.cc). This file is that
 * editor's substance, minus the parts that are IBus plumbing.
 *
 * Where the reference puts it is also what settles where this belongs. The
 * substitution runs when *nothing is composing*: PinyinEditor::processPunct
 * returns FALSE only on empty input (PYPinyinEditor.cc:83-87), which is what
 * lets the key fall through to FallbackEditor. While composing, ibus-pinyin
 * swallows the key and loses the character unless its auto-commit option is
 * on. So the placement in pyzy_backend.cc is the "nothing is composing"
 * branch, not the default arm of the composing switch.
 *
 * ---------------------------------------------------------------------------
 * Two deliberate departures from ibus-pinyin
 * ---------------------------------------------------------------------------
 *
 * 1. **Punctuation width governs all punctuation.** ibus-pinyin applies its
 *    variant table first and then falls back to the *Latin* width flag for any
 *    punctuation the table has no row for — so with full-width punctuation and
 *    half-width Latin, its `@` stays ASCII (PYFallbackEditor.cc:198). Here the
 *    fallback is the punctuation width, which is what the header promises
 *    ("the same choice for punctuation") and what the anthy front end already
 *    does with the same two options (romaji.cc's kSymbolTable covers ＠＃％ and
 *    the rest). Latin width keeps letters, digits and space, exactly as its own
 *    doc comment says.
 *
 * 2. **A key that arrives mid-composition is not lost.** ibus-pinyin's default
 *    swallows it. The adapter ends the composition first and then emits, which
 *    is ibus-pinyin's own auto-commit path (PYPinyinEditor.cc:121-126) promoted
 *    from an option to the rule. Losing text the user typed is not a behaviour
 *    worth copying.
 *
 * ---------------------------------------------------------------------------
 * The state, and why there is any
 * ---------------------------------------------------------------------------
 *
 * Three of the substitutions cannot be decided from the key alone: the two
 * quote characters alternate between their opening and closing forms, and a
 * period directly after a digit stays a period so that "1.5" is not mangled
 * into "1。5". ibus-pinyin keeps the same three values on its FallbackEditor
 * and clears them in reset(); PunctuationState is that, and the adapter clears
 * it in the same places.
 */

#ifndef LIBPATHIME_SRC_PUNCTUATION_H
#define LIBPATHIME_SRC_PUNCTUATION_H

#include <cstdint>
#include <string>
#include <string_view>

#include <pathime/pathime.h>

#include "backend.h"

namespace pathime {

/** The two width options plus the variant that picks a substitution table. */
struct WidthSettings {
    pathime_width_t latin       = PATHIME_WIDTH_HALF;
    pathime_width_t punctuation = PATHIME_WIDTH_FULL;

    /**
     * Which of the two punctuation tables applies, from
     * PATHIME_OPT_CHINESE_VARIANT.
     *
     * pyzy narrows that option to the two exclusive values, but the table engine
     * accepts all five, so the mapping is by *preference* rather than by
     * exclusion: the traditional table applies to TRADITIONAL_ONLY and
     * TRADITIONAL_FIRST, the simplified one to the other three. ANY has no
     * preference to honour and takes the simplified table, which is what pyzy
     * already did for everything that was not TRADITIONAL_ONLY.
     *
     * For a table engine this usually resolves through tier 3 to the table's own
     * LANGUAGE_FILTER, so cangjie5 and quick5 (`cm1`) punctuate the traditional
     * way, wubi-jidian86 (`cm2`) the simplified way, and stroke5 and zhuyin
     * (`cm3`) the traditional way — without anything here knowing a table name.
     */
    bool simplified = true;
};

/** Read the three values above out of @a options. */
WidthSettings width_settings(const OptionReader &options);

/**
 * What the alternating and look-behind substitutions need to remember.
 *
 * Value-initialised to the state a fresh composition starts in: the next
 * quote of either kind is an opening one, and nothing has been emitted.
 */
struct PunctuationState {
    bool quote_open        = true;  /**< Next `'` is ‘ rather than ’. */
    bool double_quote_open = true;  /**< Next `"` is “ rather than ”. */
    bool prev_was_digit    = false; /**< Last committed text was one ASCII digit. */

    void clear() { *this = PunctuationState(); }

    /**
     * Record text that has just been committed, from here or from pyzy.
     *
     * The look-behind is over the *document*, not over this layer's own
     * output, so a Chinese character committed by the engine between a digit
     * and a period has to disarm it — otherwise "1.5" and "1好." would be
     * punctuated the same way. Calling it for both sources, in the order the
     * two commits actually reach the client, is what makes that true; the
     * caller does the ordering, because only it knows which came first.
     */
    void note_commit(const std::string &text)
    {
        if (!text.empty()) {
            prev_was_digit = text.size() == 1 && text[0] >= '0' && text[0] <= '9';
        }
    }
};

/**
 * True for a keysym this layer will emit: printable ASCII, U+0020 to U+007E.
 *
 * The bound matters as much as the rule. Everything outside it — the named
 * keys, the function keys, anything a keymap produces above ASCII — stays the
 * client's, which is FallbackEditor's `default: break` and the reason it
 * returns FALSE for them (PYFallbackEditor.cc:270-272).
 */
bool emittable(uint32_t keysym);

/**
 * The text for one emittable key, as UTF-8.
 *
 * @a state is read throughout and updated for the quote alternation only; the
 * digit look-behind is the caller's to record through note_commit(), because
 * pyzy's own commits count towards it too and only the caller sees those.
 */
std::string emit_text(char c, const WidthSettings &settings, PunctuationState *state);

/**
 * Correct @a state against what the client's document actually shows.
 *
 * Both rules in this layer are about the text before the insertion position —
 * a full stop after a digit is a decimal point, a quotation mark alternates
 * with the one before it — but PunctuationState answers them from what the
 * *engine* emitted, which is a different question. It diverges whenever the
 * document moved without the engine: a caret moved to somewhere else in the
 * field, a paste, an undo, a commit that ended one composition before another
 * began, or text that was in the field before the user ever typed into it.
 *
 * So where the snapshot can answer, it wins; where it cannot, the tracked
 * value stands. The asymmetry is the whole design and it runs one way only:
 * @a before_cursor showing a thing is proof, and @a before_cursor not showing
 * it is not proof of absence — the snapshot may be a fragment whose start is
 * not a document boundary, so a quotation mark ten words back may simply be
 * outside it. Nothing here is ever *cleared* on the strength of not finding
 * it.
 *
 * Concretely:
 *
 *   - The digit look-behind reads one scalar, the one immediately before the
 *     cursor. Either the snapshot shows it, in which case the answer is
 *     certain both ways, or the snapshot is empty and nothing is touched.
 *   - Each quote alternation scans back for the nearest curly quote of its own
 *     kind. Finding one settles the next form, because the engine's output
 *     alternates and so the most recent mark encodes the state. Finding none
 *     settles nothing.
 *
 * Cheap enough to call per emitted key: the digit half is O(1) and the scans
 * stop at the first mark they find, which for the case that matters — a quote
 * the user typed a moment ago — is immediate.
 */
void observe_document(std::string_view before_cursor, PunctuationState *state);

}  // namespace pathime

#endif /* LIBPATHIME_SRC_PUNCTUATION_H */
