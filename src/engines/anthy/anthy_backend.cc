/*
 * Implementation of the Japanese adapter declared in anthy_backend.h.
 *
 * ---------------------------------------------------------------------------
 * The shape of a composition here
 * ---------------------------------------------------------------------------
 *
 * Two states, and every method below begins by branching on which one it is
 * in, because anthy is two libraries stitched together at that seam:
 *
 *   typing      No conversion has been asked for. anthy has not been called
 *               and holds nothing. The composition is the romaji front end's
 *               kana buffer, all of it in model->active, with no candidates:
 *               there is nothing to choose between until anthy has segmented
 *               something.
 *   converting  anthy_set_string() has run and the context holds N segments.
 *               The active-segment index is ours and private (backend.h rule
 *               3): segments before it appear in model->settled at their
 *               chosen candidate, the active one in model->active, and the
 *               segments after it in model->tail at
 *               NTH_UNCONVERTED_CANDIDATE — their reading, since the user has
 *               not reached them and the engine has not been asked what it
 *               thinks of them yet.
 *
 * Greedy left-to-right resolution moves the index forward one segment at a
 * time and never back. That is the phone-keyboard target's call, and its cost
 * is stated plainly: a user who mis-segments a sentence cannot walk back to
 * fix segment 1, and anthy_resize_segment() is not called at all. What they
 * can do is cancel the whole conversion and retype.
 *
 * ---------------------------------------------------------------------------
 * Learning, and why the commit calls are not where a reader expects them
 * ---------------------------------------------------------------------------
 *
 * anthy_commit_segment(ctx, seg, cand) does *not* commit anything on its own:
 * it sets seg->committed and nothing else (src-main/main.c:376-410). The
 * personal dictionary is only written when the last segment is committed, at
 * which point main.c:412 runs anthy_proc_commit() and anthy_save_history().
 * So the calls are per-segment but the learning is all-or-nothing, and an
 * adapter that told anthy only about the segments the user actively chose
 * between would never write anything at all.
 *
 * Hence: every segment gets committed exactly once, at the moment the user
 * confirms it — at selection for the segments they walked through, and at
 * PATHIME_KEY_RETURN for the ones they left as readings, those at
 * NTH_UNCONVERTED_CANDIDATE, which main.c:394-402 resolves to whichever
 * candidate equals the reading. Never during candidate navigation, which is
 * the corruption docs/anthy-mapping.md mismatch #4 warns about. And never at
 * all when PATHIME_OPT_LEARNING is false: anthy has no switch for it, so the
 * library implements the option by withholding the call, exactly as the
 * option's own documentation says it does.
 */

#include "engines/anthy/anthy_backend.h"

#include <anthy/anthy.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engines/anthy/romaji.h"
#include "keys.h"
#include "paths.h"

namespace pathime {
namespace {

/* ---------------------------------------------------------------------------
 * One composition in flight
 * ------------------------------------------------------------------------- */

class AnthyContextBackend final : public ContextBackend {
public:
    explicit AnthyContextBackend(anthy_context_t context) : context_(context) {}

    ~AnthyContextBackend() override
    {
        if (context_) anthy_release_context(context_);
    }

    AnthyContextBackend(const AnthyContextBackend &) = delete;
    AnthyContextBackend &operator=(const AnthyContextBackend &) = delete;

    bool process_key(const KeyEvent &key,
                     const OptionReader &options,
                     const SurroundingTextView &doc,
                     Composition *model,
                     Output *out) override;

    void reset(Composition *model, Output *out) override;

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
    bool key_while_typing(const KeyEvent &key,
                          const OptionReader &options,
                          const RomajiSettings &settings,
                          Composition *model,
                          Output *out);
    bool key_while_converting(const KeyEvent &key,
                              const OptionReader &options,
                              const RomajiSettings &settings,
                              Composition *model,
                              Output *out);

    bool fetch_segment(int segment, int candidate, std::string *out);

    void show_kana(Composition *model, const RomajiSettings &settings);
    void show_segments(Composition *model);

    bool begin_conversion(Composition *model, const RomajiSettings &settings);
    void cancel_conversion(Composition *model, const RomajiSettings &settings);
    void advance_candidate(const OptionReader &options, Composition *model);
    bool show_candidate(int cursor, Composition *model);

    void commit_kana(Composition *model, Output *out, const RomajiSettings &settings);
    void commit_conversion(Composition *model, Output *out, const OptionReader &options);
    void record_choices(int through_segment);
    void forget_conversion();

    anthy_context_t context_ = nullptr;
    RomajiComposer  composer_;

    /** False while the kana buffer is being typed, true once anthy has run. */
    bool converting_ = false;

    /** Segment count from anthy_get_stat(); 0 unless converting_. */
    int segment_count_ = 0;

    /**
     * The leftmost unsettled segment. Private by rule 3 of backend.h: the API
     * exposes one span, and this is the index that decides which one.
     */
    int active_ = 0;

    /**
     * The candidate index currently shown for each segment — Finding 2's
     * "the currently shown candidate is ours", per segment because anthy's
     * candidate lists are (mismatch #1). Initialized to 0, anthy's own best
     * guess for each.
     */
    std::vector<int> chosen_;

    /** Which segments have already been handed to anthy_commit_segment(). */
    std::vector<char> recorded_;

    /**
     * The segment model->candidates was last built for, or -1 for none.
     *
     * Cycling candidates within one segment must leave the list alone —
     * nothing about it changed — while moving to another segment must replace
     * it wholesale. Without this the choice would be between refetching every
     * candidate string on every arrow key and letting a stale list survive a
     * segment boundary, and the second one is a correctness bug.
     */
    int listed_segment_ = -1;
};

/*
 * The two-call length protocol (docs/anthy-mapping.md, "Candidate string
 * retrieval"): buf = NULL, buf_len = 0 measures without copying, and a buffer
 * that is not strictly larger than the answer is refused with -1
 * (main.c:349-357). seg_len is not usable for this and is not consulted
 * anywhere in this file — it counts input reading xchars, not bytes
 * (main.c:282).
 *
 * The result is copied into a std::string here and nowhere else, which is
 * backend.h rule 1: what anthy hands back is freed by anthy the moment the
 * next mutating call runs.
 */
bool AnthyContextBackend::fetch_segment(int segment, int candidate, std::string *out)
{
    const int length = anthy_get_segment(context_, segment, candidate, nullptr, 0);
    if (length < 0) return false;

    std::string buffer(static_cast<size_t>(length) + 1, '\0');
    const int written = anthy_get_segment(context_, segment, candidate, &buffer[0],
                                          static_cast<int>(buffer.size()));
    if (written < 0) return false;

    buffer.resize(static_cast<size_t>(written));
    *out = buffer;
    return true;
}

/* ---------------------------------------------------------------------------
 * Projection
 * ------------------------------------------------------------------------- */

/** The typing state: the kana buffer, whole, with nothing to choose from. */
void AnthyContextBackend::show_kana(Composition *model, const RomajiSettings &settings)
{
    model->settled.clear();
    model->active = composer_.display(settings);
    model->tail.clear();
    model->candidates.clear();
    model->cursor = 0;
    listed_segment_ = -1;
}

/** The converting state: N segments as the three strings the API has. */
void AnthyContextBackend::show_segments(Composition *model)
{
    model->settled.clear();
    model->active.clear();
    model->tail.clear();

    std::string text;
    for (int i = 0; i < segment_count_; ++i) {
        const size_t slot = static_cast<size_t>(i);
        if (i < active_) {
            if (fetch_segment(i, chosen_[slot], &text)) model->settled += text;
        } else if (i == active_) {
            if (fetch_segment(i, chosen_[slot], &text)) model->active = text;
        } else if (fetch_segment(i, NTH_UNCONVERTED_CANDIDATE, &text)) {
            model->tail += text;
        }
    }

    if (listed_segment_ != active_) {
        model->candidates.clear();
        listed_segment_ = active_;
    }
    model->cursor = static_cast<size_t>(chosen_[static_cast<size_t>(active_)]);
}

/* ---------------------------------------------------------------------------
 * Conversion state transitions
 * ------------------------------------------------------------------------- */

/**
 * Hand the kana to anthy and enter the converting state.
 *
 * @return true always: the key that asked for conversion was consumed whether
 *         or not anthy could do anything with it. A backend failure leaves the
 *         kana buffer exactly as it was, which is the only state a user can
 *         recover from — losing their input because a dictionary file is
 *         unreadable would be strictly worse than showing it unconverted.
 */
bool AnthyContextBackend::begin_conversion(Composition *model, const RomajiSettings &settings)
{
    const std::string reading = composer_.reading(settings);
    if (reading.empty()) return true;

    /* A status code, 0 or -1 — not a segment count (main.c:202). The count
     * comes from anthy_get_stat(), below. Reading it as a count is the mistake
     * docs/anthy-mapping.md calls out by name. */
    if (anthy_set_string(context_, reading.c_str()) < 0) return true;

    struct anthy_conv_stat stat;
    if (anthy_get_stat(context_, &stat) < 0 || stat.nr_segment <= 0) return true;

    segment_count_ = stat.nr_segment;
    chosen_.assign(static_cast<size_t>(segment_count_), 0);
    recorded_.assign(static_cast<size_t>(segment_count_), 0);
    active_ = 0;
    converting_ = true;
    listed_segment_ = -1;

    show_segments(model);
    return true;
}

/** Back to the kana buffer, which the composer never stopped holding. */
void AnthyContextBackend::cancel_conversion(Composition *model, const RomajiSettings &settings)
{
    /* Not required for correctness — anthy_set_string() calls
     * anthy_do_reset_context() as its first action (main.c:212) — but it frees
     * the segment list and releases the dictionary session now rather than at
     * the next conversion. That is the justification docs/anthy-mapping.md
     * mismatch #5 gives for calling it where ibus-anthy does not. */
    anthy_reset_context(context_);
    forget_conversion();
    show_kana(model, settings);
}

void AnthyContextBackend::forget_conversion()
{
    converting_ = false;
    segment_count_ = 0;
    active_ = 0;
    chosen_.clear();
    recorded_.clear();
    listed_segment_ = -1;
}

/**
 * Tell anthy what the user chose, for every segment up to and including
 * @a through_segment that has not been told yet. Segments the user actually
 * walked through go at their chosen candidate; anything past the active one
 * goes at NTH_UNCONVERTED_CANDIDATE, because the reading is what is on screen
 * and committing something the user never saw would teach anthy a preference
 * they never expressed.
 *
 * Only ever reached when PATHIME_OPT_LEARNING is true; see the file comment.
 */
void AnthyContextBackend::record_choices(int through_segment)
{
    for (int i = 0; i <= through_segment && i < segment_count_; ++i) {
        const size_t slot = static_cast<size_t>(i);
        if (recorded_[slot]) continue;
        const int candidate = (i <= active_) ? chosen_[slot] : NTH_UNCONVERTED_CANDIDATE;
        anthy_commit_segment(context_, i, candidate);
        recorded_[slot] = 1;
    }
}

/* ---------------------------------------------------------------------------
 * Commit
 * ------------------------------------------------------------------------- */

/**
 * PATHIME_KEY_RETURN with no conversion asked for: the kana, finished.
 *
 * commit_text() and not display(): a pending "n" is displayed as "n" while
 * typing, because one more key still decides whether it is ん or な, but
 * committing decides it — so "nihon" then Return commits にほん and not にほn.
 */
void AnthyContextBackend::commit_kana(Composition *model,
                                      Output *out,
                                      const RomajiSettings &settings)
{
    out->commit = composer_.commit_text(settings);
    composer_.clear();
    model->clear();
    listed_segment_ = -1;
}

/**
 * PATHIME_KEY_RETURN mid-conversion: what is on screen, exactly.
 *
 * Not "every segment at its best candidate" — the tail is displayed at its
 * reading, and committing anything else would insert text the user was never
 * shown. ibus-anthy can commit the converted form because it displays the
 * converted form; the difference follows from the tail being unconverted here,
 * which in turn follows from greedy resolution.
 */
void AnthyContextBackend::commit_conversion(Composition *model,
                                            Output *out,
                                            const OptionReader &options)
{
    if (options.flag(PATHIME_OPT_LEARNING)) record_choices(segment_count_ - 1);

    out->commit = model->preedit();
    anthy_reset_context(context_);
    forget_conversion();
    composer_.clear();
    model->clear();
}

/* ---------------------------------------------------------------------------
 * Candidates
 * ------------------------------------------------------------------------- */

void AnthyContextBackend::advance_candidate(const OptionReader &options,
                                            Composition *model)
{
    struct anthy_segment_stat stat;
    if (anthy_get_segment_stat(context_, active_, &stat) < 0 || stat.nr_candidate <= 0) return;

    /* Cycling is capped at the same PATHIME_OPT_MAX_CANDIDATES the list is,
     * so the cursor can never point past the end of what the client was given.
     * A client that raises the cap as the user scrolls widens both together,
     * which is what that option's "composition-safe" promise is for. */
    int count = stat.nr_candidate;
    const int64_t cap = options.number(PATHIME_OPT_MAX_CANDIDATES);
    if (cap > 0 && cap < count) count = static_cast<int>(cap);

    /* Wrapping is this function's own, and belongs to Space rather than to the
     * cursor: pressing convert repeatedly must never dead-end. A client
     * navigating with set_cursor() decides for itself whether its own ends
     * wrap, which is why that path takes an absolute index and does no
     * arithmetic. */
    int cursor = chosen_[static_cast<size_t>(active_)] + 1;
    if (cursor >= count) cursor = 0;

    show_candidate(cursor, model);
}

pathime_status_t AnthyContextBackend::set_cursor(size_t index,
                                                 const OptionReader &options,
                                                 Composition *model)
{
    (void)options;

    /* Not PATHIME_ERROR_UNSUPPORTED — that is for an engine with no candidates
     * at all. This one has them, just not before anything has been converted,
     * so a hover now is an out-of-range index. The same distinction
     * select_candidate() draws. */
    if (!converting_) return PATHIME_ERROR_INVALID_ARGUMENT;

    if (!show_candidate(static_cast<int>(index), model)) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }
    return PATHIME_OK;
}

/**
 * Show candidate @a cursor of the active segment: record it, and rewrite the
 * active span to its text so the preedit previews what is being hovered.
 *
 * The one place the shown candidate changes, reached from both the client's
 * set_cursor() and Space's advance_candidate(). Keeping chosen_, model->cursor
 * and model->active in step is exactly the bookkeeping Finding 2 says is ours,
 * and having one writer is what stops the three drifting apart.
 */
bool AnthyContextBackend::show_candidate(int cursor, Composition *model)
{
    std::string text;
    if (cursor < 0 || !fetch_segment(active_, cursor, &text)) return false;

    chosen_[static_cast<size_t>(active_)] = cursor;
    model->active = text;
    model->cursor = static_cast<size_t>(cursor);
    return true;
}

void AnthyContextBackend::materialize_candidates(size_t cap,
                                                 const OptionReader &options,
                                                 Composition *model)
{
    (void)options;

    /* Nothing to enumerate before conversion: the kana buffer is not a span
     * anthy has an opinion about yet. */
    if (!converting_) return;

    struct anthy_segment_stat stat;
    if (anthy_get_segment_stat(context_, active_, &stat) < 0 || stat.nr_candidate <= 0) return;

    /* Appends from wherever the list already ends, so raising the cap extends
     * it and nothing already handed to a client is renumbered. Running out
     * before the cap is normal and is not an error. */
    std::string text;
    for (size_t i = model->candidates.size();
         i < cap && i < static_cast<size_t>(stat.nr_candidate); ++i) {
        if (!fetch_segment(active_, static_cast<int>(i), &text)) break;
        model->candidates.push_back(text);
    }
    listed_segment_ = active_;
}

pathime_status_t AnthyContextBackend::select_candidate(size_t index,
                                                       const OptionReader &options,
                                                       Composition *model,
                                                       Output *out)
{
    /* Not PATHIME_ERROR_UNSUPPORTED: that is for an engine with no candidates
     * at all, which is Hangul. This engine has them, just not before anything
     * has been converted, so a selection now is an out-of-range index. */
    if (!converting_) return PATHIME_ERROR_INVALID_ARGUMENT;

    std::string text;
    if (!fetch_segment(active_, static_cast<int>(index), &text)) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    chosen_[static_cast<size_t>(active_)] = static_cast<int>(index);

    /* The moment the user confirms — mismatch #4's ordering requirement. */
    if (options.flag(PATHIME_OPT_LEARNING)) record_choices(active_);

    model->active = text;
    model->settle_active();
    ++active_;

    if (active_ >= segment_count_) {
        /* Nothing is left unsettled, so settle_active() has already put the
         * whole text in model->settled and preedit() is exactly that. */
        out->commit = model->preedit();
        anthy_reset_context(context_);
        forget_conversion();
        composer_.clear();
        model->clear();
    } else {
        show_segments(model);
    }
    return PATHIME_OK;
}

/* ---------------------------------------------------------------------------
 * Keys
 * ------------------------------------------------------------------------- */

/**
 * The six characters PATHIME_OPT_ANTHY_ON_PERIOD acts on, as reachable from a
 * key event.
 *
 * The header fixes the set as , . 、 。 ， ． — but four of those are what the
 * front end *produces* from the two ASCII keys, under
 * PATHIME_OPT_ANTHY_PERIOD_STYLE and the width options. Testing the key rather
 * than the output covers all six without having to ask which projection was in
 * force. The kana keysyms a JIS layout could send instead are declined by the
 * composer anyway; that is the kana-entry gap romaji.cc records.
 */
bool is_sentence_end_key(const KeyEvent &key)
{
    const char c = keysym_to_ascii(key.keysym);
    return c == ',' || c == '.';
}

bool AnthyContextBackend::process_key(const KeyEvent &key,
                                      const OptionReader &options,
                                      const SurroundingTextView &doc,
                                      Composition *model,
                                      Output *out)
{
    /* Unused: anthy holds its composition in the preedit, so it never revises
     * text already in the client's document. See backend.h. */
    (void)doc;

    /* Ctrl/Alt/Super chords are the client's shortcuts, never ours — even
     * mid-composition, where absorbing one would make Ctrl+S stop saving. */
    if (is_chorded(key)) return false;

    const RomajiSettings settings = romaji_settings(options);
    return converting_ ? key_while_converting(key, options, settings, model, out)
                       : key_while_typing(key, options, settings, model, out);
}

bool AnthyContextBackend::key_while_typing(const KeyEvent &key,
                                           const OptionReader &options,
                                           const RomajiSettings &settings,
                                           Composition *model,
                                           Output *out)
{
    switch (key.keysym) {
    case PATHIME_KEY_BACKSPACE:
        /* Declined when there is nothing to delete, so the client's own
         * backspace still reaches its document. */
        if (!composer_.backspace()) return false;
        show_kana(model, settings);
        return true;

    case PATHIME_KEY_RETURN:
        if (composer_.empty()) return false;
        commit_kana(model, out, settings);
        return true;

    case PATHIME_KEY_ESCAPE:
    case PATHIME_KEY_MUHENKAN:
        /* Discards the buffer without committing it, which is what cancelling
         * means here — there is no conversion to fall back to. */
        if (composer_.empty()) return false;
        composer_.clear();
        model->clear();
        listed_segment_ = -1;
        return true;

    case PATHIME_KEY_SPACE:
    case PATHIME_KEY_HENKAN:
        /* Space is the convert key only when there is something to convert.
         * With an empty buffer it is declined so the client inserts its own
         * space, rather than absorbed into a full-width 　 nobody asked for. */
        if (composer_.empty()) return false;
        return begin_conversion(model, settings);

    default:
        break;
    }

    if (!composer_.insert(key, settings)) return false;
    show_kana(model, settings);

    /* PATHIME_OPT_ANTHY_ON_PERIOD, after the character is in the buffer: the
     * option is about what typing it does *beyond* inserting it. */
    if (is_sentence_end_key(key)) {
        switch (options.number(PATHIME_OPT_ANTHY_ON_PERIOD)) {
        case PATHIME_ANTHY_ON_PERIOD_CONVERT:
            begin_conversion(model, settings);
            break;
        case PATHIME_ANTHY_ON_PERIOD_COMMIT:
            commit_kana(model, out, settings);
            break;
        default:
            break;
        }
    }
    return true;
}

bool AnthyContextBackend::key_while_converting(const KeyEvent &key,
                                               const OptionReader &options,
                                               const RomajiSettings &settings,
                                               Composition *model,
                                               Output *out)
{
    switch (key.keysym) {
    /*
     * Space keeps meaning "convert" once conversion has begun, which is what
     * every Japanese IME does: the second press is the next candidate. It is
     * the one key whose meaning the header fixes across every engine that
     * composes, so it stays here.
     *
     * The arrow keys used to sit alongside it and no longer do. Navigating a
     * candidate list is the client's — it owns the key bindings, its own
     * pagination, and whether either end wraps — and
     * pathime_context_set_candidate_cursor() is how it says so. An engine that
     * also swallowed Up and Down would take the decision back, because a key
     * this adapter reports handled never reaches the client's binding at all.
     */
    case PATHIME_KEY_SPACE:
    case PATHIME_KEY_HENKAN:
        advance_candidate(options, model);
        return true;

    case PATHIME_KEY_RETURN:
        commit_conversion(model, out, options);
        return true;

    case PATHIME_KEY_ESCAPE:
    case PATHIME_KEY_MUHENKAN:
    case PATHIME_KEY_BACKSPACE:
        /* Backspace joins the two cancel keys rather than deleting a kana:
         * mid-conversion the buffer it would delete from is not what is on
         * screen, so undoing the conversion first is the only reading that
         * leaves the user somewhere they recognize. */
        cancel_conversion(model, settings);
        return true;

    default:
        break;
    }

    /* Anything printable ends the conversion and starts a new composition with
     * it — the user has moved on. The commit has to happen first so the
     * converted text lands before the new preedit appears. */
    if (keysym_to_ascii(key.keysym) == 0) return false;
    commit_conversion(model, out, options);
    if (composer_.insert(key, settings)) show_kana(model, settings);
    return true;
}

void AnthyContextBackend::reset(Composition *model, Output *out)
{
    (void)model;
    (void)out;

    /* Nothing goes into `out`: everything this context holds is preedit, which
     * reset discards by definition. The header's "must not commit implicitly"
     * is not a rule we have to work around here — it is already what
     * cancelling a composition means for this engine. */
    anthy_reset_context(context_);
    forget_conversion();
    composer_.clear();
}

/* ---------------------------------------------------------------------------
 * The engine layer
 * ------------------------------------------------------------------------- */

/**
 * anthy shares nothing between contexts that is not already process-global.
 *
 * Its dictionaries, personality and conf database all live behind anthy_init()
 * (Finding 3) and are reached by the vendor library through file-scope state,
 * not through a handle we could hold. So this class is empty and says so,
 * rather than inventing a per-engine object for symmetry with the other two
 * adapters.
 */
class AnthyEngineBackend final : public EngineBackend {
public:
    std::unique_ptr<ContextBackend> create_context(const OptionReader &options) override
    {
        (void)options;

        anthy_context_t context = anthy_create_context();
        if (!context) return nullptr;

        /* Before anything else: contexts are born in anthy's compiled default
         * encoding, which is EUC-JP (main.c:95), and every string that crosses
         * this adapter is UTF-8 (Finding 4). Missing this call would not fail
         * — it would quietly produce mojibake. */
        if (anthy_context_set_encoding(context, ANTHY_UTF8_ENCODING) != ANTHY_UTF8_ENCODING) {
            anthy_release_context(context);
            return nullptr;
        }

        /* anthy_set_reconversion_mode() is deliberately left at its default:
         * reconversion means feeding already-committed text back in, and the
         * only route to that is the surrounding-text surface, which nothing in
         * this adapter uses (docs/anthy-mapping.md mismatch #6). */

        return std::unique_ptr<ContextBackend>(new AnthyContextBackend(context));
    }
};

}  // namespace

/* ---------------------------------------------------------------------------
 * The process-global layer
 * ------------------------------------------------------------------------- */

/*
 * Both directories, and the personality trap data_dir was designed around.
 *
 * Everything anthy reads or writes is decided by four entries in its conf
 * database, and all four are set here before anthy_init() ever runs. That is
 * the whole configuration surface: anthy_conf_get_str() consults the database
 * first and falls back to getenv() only for names nothing has overridden, so a
 * name that is set here cannot be reached by the environment, by a conf file,
 * or by whatever a system-wide anthy installation left lying around.
 *
 *  - CONFFILE is set empty, which means "there is no conf file". Without it
 *    anthy reads its compiled-in one, so a machine with anthy installed and a
 *    machine without it would configure this library differently.
 *  - DIC_FILE is the dictionary, beneath resource_dir. This is the value that
 *    would otherwise come out of that conf file, and passing it directly is
 *    also what lets it be any path at all: a conf file's values are split on
 *    whitespace, so a dictionary reached through one could never live under a
 *    directory with a space in its name.
 *  - XDG_CONFIG_HOME is data_dir. anthy computes its per-user directory —
 *    the record file learning writes, and the private dictionary — in
 *    anthy_get_user_dir() (src-worddic/priv_dic.c:76) as $XDG_CONFIG_HOME/anthy
 *    when that name has a value and $HOME/.config/anthy otherwise, and it
 *    creates the directory itself (anthy_check_user_dir, priv_dic.c:114).
 *  - HOME is data_dir too, as the belt to that brace: it is what
 *    anthy_get_user_dir() falls back to, and leaving it to the environment
 *    would put one stray write in the developer's real home directory.
 *
 * anthy_set_personality() is what the data_dir arrangement avoids. It is
 * public, but it is process-global and write-once, which is precisely why
 * pathime_init() has a data_dir at all: making the *directory* the identity
 * means a client wanting a second profile passes a second path, and nothing in
 * this library ever contends for the one personality slot. That decision is
 * the header's, at pathime_init_params_t::data_dir; this is the code that
 * honours it.
 *
 * One residual limit, stated rather than papered over: anthy_get_user_dir(1) —
 * the legacy ~/.anthy location, consulted once when migrating an old private
 * dictionary (src-worddic/textdict.c:129) — is computed from the HOME set
 * above rather than from the user's real one, so a client that changes
 * data_dir between runs does not carry an old private dictionary across. It is
 * read-only and is only reached if such a directory exists.
 */
bool anthy_global_init(const char *data_dir, const char *resource_dir)
{
    const std::string dic =
        path_join(path_join(resource_dir, "anthy"), "anthy.dic");

    /*
     * Checked in front of anthy_init() rather than left to it. anthy reports a
     * missing dictionary the same way it reports a corrupt one — -1, plus two
     * lines on its logger — and a build that simply does not ship Japanese is
     * not an error worth printing to a client's stderr. Testing for the file
     * first turns the ordinary case into a quiet false, and leaves anthy's
     * complaint to mean what it says: the dictionary is there and unusable.
     */
    if (!is_regular_file(dic)) {
        return false;
    }

    anthy_conf_override("CONFFILE", "");
    anthy_conf_override("DIC_FILE", dic.c_str());
    if (data_dir && data_dir[0] != '\0') {
        anthy_conf_override("XDG_CONFIG_HOME", data_dir);
        anthy_conf_override("HOME", data_dir);
    }

    /* Nonzero is failure — the one anthy entry point that reports that way
     * round. It is also the call that opens the dictionaries, and so most of
     * why pathime_init() is documented as the one slow call in the API. */
    return anthy_init() == 0;
}

void anthy_global_shutdown()
{
    anthy_quit();
}

std::unique_ptr<EngineBackend> anthy_create_engine()
{
    return std::unique_ptr<EngineBackend>(new AnthyEngineBackend());
}

}  // namespace pathime
