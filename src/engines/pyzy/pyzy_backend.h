/*
 * Chinese adapter over pyzy — one backend supplying two engine ids,
 * PATHIME_ENGINE_PINYIN and PATHIME_ENGINE_BOPOMOFO (the one place
 * PATHIME_WITH_* is not one-to-one with an engine). The mapping is
 * docs/pyzy-mapping.md; what makes this more than a shim:
 *
 *  - pyzy's InputType is fixed at context creation, so pinyin vs bopomofo is
 *    decided when the pathime context is created, not switched later.
 *  - Its preedit is three parts (selectedText | conversionText | restText)
 *    with the middle provisional and its own focused-candidate index
 *    (Finding 1).
 *  - cursor() is a byte offset into the raw ASCII input; never conflate it
 *    with output scalar positions (Finding 4).
 *  - hasCandidate(i) is lazy and mutating, which is why core materializes
 *    candidates eagerly before dispatching callbacks (candidates.cc); this
 *    adapter is where that pump actually touches pyzy.
 *  - Mutations fire six Observer callbacks synchronously mid-call; the
 *    dirty-flag observer that reconciles push with the core's pull model is
 *    observer.* (Finding 5).
 *  - pyzy schedules its user-database save through g_timeout_add and a
 *    GTimer, which needs a GMainLoop we do not run — the save would never
 *    fire, so this adapter drives it explicitly (docs/design-history.md §5).
 *  - Input is [a-z] and apostrophe only (Finding 6). Every other printable
 *    ASCII key is therefore this adapter's to emit rather than pyzy's to
 *    take, which is what PATHIME_OPT_LATIN_WIDTH and
 *    PATHIME_OPT_PUNCTUATION_WIDTH govern; punctuation.* is that layer.
 *
 * One claim re-verified here, and the answer is split (docs/design-history.md §1, "One claim
 * to re-check"). Tracing pyzy's bopomofo_table (PinyinParserTable.h:6622) into
 * the pinyin_table entries it points at: of its 479 rows, 75 reach an entry
 * with a non-zero `flags` field, and check_flags() (PinyinParser.cc:34-49)
 * makes parseBopomofo() *stop* at a syllable whose flag bit is clear.
 *
 *   - PATHIME_OPT_PINYIN_FUZZY is reachable from bopomofo. 61 rows carry a
 *     PINYIN_FUZZY_* bit across 16 distinct bits, and the behaviour is
 *     visible: ㄈㄨㄥ parses as "fong" and yields 红 with PINYIN_FUZZY_F_H
 *     set, and parses as "fu" with ㄥ left in restText without it.
 *   - PATHIME_OPT_PINYIN_CORRECTION is not. No row of bopomofo_table reaches
 *     an entry carrying a PINYIN_CORRECT_* bit — the original reasoning holds
 *     for corrections, which are Latin typing slips with no bopomofo spelling.
 *
 * So the option table's `engines` for PATHIME_OPT_PINYIN_FUZZY is narrower
 * than the truth. Widening it is additive and is options.cc's to make; nothing
 * here depends on the change, because the mask is pulled through OptionReader
 * either way and an unsupported option still resolves to its default of every
 * bit — which is pyzy's own default too.
 */

#ifndef LIBPATHIME_SRC_ENGINES_PYZY_BACKEND_H
#define LIBPATHIME_SRC_ENGINES_PYZY_BACKEND_H

#include <PyZy/InputContext.h>

#include "backend.h"
#include "engines/pyzy/observer.h"
#include "engines/pyzy/punctuation.h"

namespace pathime {

/**
 * One composition in flight: a PyZy::InputContext and the observer it reports
 * through.
 *
 * The InputType is held only as a record of what the live context was built
 * with. The wanted type is recomputed from PATHIME_OPT_PINYIN_SCHEME on every
 * mutating call and compared against it — never cached as a decision, which is
 * backend.h rule 4. That option is documented as resetting the composition
 * precisely because pyzy fixes the type at create() time
 * (InputContext.cc:67-81), so when the two differ this object rebuilds its own
 * PyZy::InputContext. That rebuild is what makes a scheme change take effect at
 * all: ContextBackend has no "replace me" hook, and the core cannot be expected
 * to know that one vendor's context is cheap to throw away.
 */
class PyzyContext final : public ContextBackend {
public:
    PyzyContext(pathime_engine_id_t id, const OptionReader &options);
    ~PyzyContext() override;

    /** False if pyzy refused to make a context; the engine then makes none. */
    bool valid() const { return context_ != nullptr; }

    bool process_key(const KeyEvent &key,
                     const OptionReader &options,
                     const SurroundingTextView &doc,
                     Composition *model,
                     Output *out) override;

    void reset(Composition *model, Output *out) override;

    void options_changed(const OptionReader &options,
                         Composition *model,
                         Output *out) override;

    pathime_status_t select_candidate(size_t index,
                                      const OptionReader &options,
                                      Composition *model,
                                      Output *out) override;

    pathime_status_t set_cursor(size_t index,
                                const OptionReader &options,
                                Composition *model) override;

    void materialize_candidates(size_t cap,
                                const OptionReader &options,
                                Composition *model) override;

private:
    /** The InputType @a options currently ask for, given this engine id. */
    PyZy::InputContext::InputType wanted_type(const OptionReader &options) const;

    /**
     * Push every option pyzy models as a property, rebuilding the context
     * first if the phonetic type changed. Called at the top of each mutating
     * call: options are pulled when needed and never cached (backend.h rule
     * 4), and the header's promise is that a change takes effect immediately.
     */
    void apply_options(const OptionReader &options);

    /** Replace the live context with a fresh one of @a type. */
    void recreate(PyZy::InputContext::InputType type);

    /**
     * Copy out whatever the observer says the last mutation changed. This is
     * the post-call assembly of Finding 5 and the only place pyzy's borrowed
     * strings are read.
     */
    void harvest(const OptionReader &options, Composition *model, Output *out);

    /**
     * End whatever is composing, the way ibus-pinyin's auto-commit path does:
     * take the hovered candidate if there is one, then commit the rest raw.
     * Called before emitting a character of our own, so that the punctuation
     * lands after the text it follows rather than in front of it.
     */
    void finish_composition(const Composition &model);

    /**
     * Commit exactly what the client was last shown as the preedit, with the
     * separators pyzy renders between syllables removed, and discard the
     * composition. This is PATHIME_KEY_RETURN.
     *
     * Derived from @a model rather than delegated to
     * PyZy::InputContext::commit(), so that the header's guarantee — ending a
     * composition commits the preedit — holds by construction. See the call
     * site for the double-pinyin case that makes the distinction real.
     */
    void commit_preedit(const Composition &model, Output *out);

    PyzyObserver observer_;
    PyZy::InputContext *context_ = nullptr;
    PyZy::InputContext::InputType type_;
    pathime_engine_id_t id_;

    /**
     * The quote alternation and digit look-behind of punctuation.h, which are
     * per-context because a client may have two documents open with two
     * unclosed quotes between them. Cleared wherever the composition is
     * discarded outright — reset() and a context rebuild — because that is
     * where ibus-pinyin clears its copy (FallbackEditor::reset()).
     */
    PunctuationState punctuation_;
};

/**
 * What the two Chinese engines share. pyzy keeps its dictionaries in a
 * process-global Database rather than per engine (Finding 3), so this holds
 * only the id that decides which InputType its contexts are built with —
 * which is the whole reason pyzy_create_engine() takes one.
 */
class PyzyEngine final : public EngineBackend {
public:
    explicit PyzyEngine(pathime_engine_id_t id) : id_(id) {}

    std::unique_ptr<ContextBackend> create_context(const OptionReader &options) override;

private:
    pathime_engine_id_t id_;
};

}  // namespace pathime

#endif /* LIBPATHIME_SRC_ENGINES_PYZY_BACKEND_H */
