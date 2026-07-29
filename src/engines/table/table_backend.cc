/*
 * Implementation of the table-driven engine declared in table_backend.h.
 *
 * The state machine reproduces ibus-table's, mapped onto the three strings of
 * composition.h. That mapping is the one structural decision worth
 * reading before the code:
 *
 *   settled  the phrases of the pre-committed segments, in order
 *   active   the current key run as displayed — chars_valid with
 *            char-prompt substitution, then chars_invalid unchanged
 *   tail     always empty
 *
 * `tail` is empty because the model has no cursor inside a span and this engine
 * declines the motions that would need one. ibus-table lets the user move a
 * caret among the pre-committed segments and insert in the middle, which is
 * what its `cursor_precommit` is for; docs/CONCEPTS.md flattens that away, and
 * docs/ibus-table-mapping.md records it as an impedance mismatch. So new input
 * always joins at the end, every pre-committed segment is to the left of it,
 * and `settled` holds all of them. That also makes preedit_settled fall out as
 * it should: the display position is the end of the pre-committed segments.
 *
 * The consequence is the same one anthy and pyzy already pay, and it is stated
 * here rather than hidden: Left, Right, Home and End are declined while
 * composing, so a composition can only be edited from its end — Backspace, or
 * abandon it and retype.
 */

#include "engines/table/table_backend.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <pathime/config.h>

#include "composition.h"
#include "engines/table/ranking.h"
#include "engines/table/table_db.h"
#include "keys.h"
#include "paths.h"
#include "punctuation.h"
#include "utf8.h"

namespace pathime {
namespace table {

namespace {

/* ===========================================================================
 * Process-global state
 *
 * The two directories, kept because path resolution needs them and neither is
 * available to a context. Set once by table_global_init() and read-only after,
 * which is what makes them safe to touch without the locking the API's
 * threading rule already forbids clients from needing.
 * ======================================================================== */

std::string g_table_dir;   /* <resource_dir>/table */
std::string g_user_dir;    /* <data_dir>/table */

/**
 * The bare names of the tables in g_table_dir, sorted, listed once at
 * initialization. These are what PATHIME_OPT_TABLE_FILE accepts and what
 * pathime_option_value_name() hands back, so a client can offer a choice
 * without knowing where the resource directory landed.
 *
 * Listed once rather than on demand because the answer is process state that
 * cannot change while the library is up — the header promises the names stay
 * valid until pathime_shutdown(), and re-listing would break that by
 * reallocating them under a caller that is still holding one.
 */
std::vector<std::string> g_installed;

bool has_separator(const std::string &value)
{
    return value.find('/') != std::string::npos || value.find('\\') != std::string::npos;
}

}  // namespace

std::string resolve_table_path(const std::string &value)
{
    if (value.empty()) {
        return std::string();
    }
    if (has_separator(value)) {
        return value;
    }
    if (g_table_dir.empty()) {
        return std::string();
    }
    return path_join(g_table_dir, (value + ".db").c_str());
}

std::string user_db_path(const std::string &table_path)
{
    if (g_user_dir.empty() || table_path.empty()) {
        return std::string();
    }

    /*
     * Named after the table's file rather than its declared NAME or UUID, so
     * that two tables with the same name in different directories do not share
     * learned frequencies. The basename is enough: a user database is per
     * table, and colliding basenames across directories is a case worth no
     * complexity here.
     */
    size_t begin = table_path.find_last_of("/\\");
    begin = (begin == std::string::npos) ? 0 : begin + 1;
    std::string base = table_path.substr(begin);

    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) {
        base.erase(dot);
    }
    if (base.empty()) {
        return std::string();
    }
    return path_join(g_user_dir, (base + ".db").c_str());
}

namespace {

/* ===========================================================================
 * The engine
 * ======================================================================== */

/**
 * One pre-committed segment: the keys that produced it and the phrase they were
 * staged as. The keys are kept because backspacing out of an empty run pulls
 * the previous segment back apart.
 */
struct Segment {
    std::string keys;
    std::string phrase;
};

class TableEngine : public EngineBackend {
public:
    std::unique_ptr<ContextBackend> create_context(const OptionReader &options) override;

    bool declared_number(const char *table_file, pathime_option_t option,
                         int64_t *out) const override
    {
        const TableDatabase *table = peek(table_file);
        return table != nullptr && table->properties().declared_number(option, out);
    }

    const char *declared_text(const char *table_file, pathime_option_t option) const override
    {
        const TableDatabase *table = peek(table_file);
        return (table == nullptr) ? nullptr : table->properties().declared_text(option);
    }

    /**
     * Load the table PATHIME_OPT_TABLE_FILE names, so that a name which does not
     * resolve fails here instead of turning into a context that reports every
     * key unhandled.
     *
     * The empty string stays legal and loads nothing: the header spells "no
     * table" as the empty value, and a client clearing the option is not making
     * an error.
     */
    pathime_status_t prepare_string(pathime_option_t option, const char *value) override
    {
        if (option != PATHIME_OPT_TABLE_FILE) {
            return PATHIME_OK;
        }
        if (value == nullptr || *value == '\0') {
            return PATHIME_OK;
        }
        /*
         * PATHIME_ERROR_BACKEND rather than PATHIME_ERROR_INVALID_ARGUMENT: the
         * argument is a well-formed table name, and what failed is the data file
         * behind it — which is exactly what that status is for. load() caches
         * the failure, so a client retrying the same bad name pays for one open.
         */
        return (load(value) == nullptr) ? PATHIME_ERROR_BACKEND : PATHIME_OK;
    }

    /**
     * The table @a value names, loading it if this is the first request.
     *
     * Cached for the engine's lifetime and shared by every context naming the
     * same table, which is what the header means when it says per-context
     * tables cost little. A table that fails to load is cached as a null entry
     * so that a context naming a missing file does not retry the open on every
     * keystroke.
     */
    std::shared_ptr<TableDatabase> load(const std::string &value);

private:
    /**
     * The loaded table for @a table_file *without* loading it.
     *
     * Tier-3 resolution goes through here rather than through load(), because
     * resolving an option must not have the side effect of opening a database —
     * options are resolved during pathime_engine_option_get(), where a client
     * has asked a question and not asked for any work. Until the first context
     * exists, tier 3 therefore contributes nothing and resolution falls to the
     * descriptor default, which is the same answer it gives for any engine
     * that publishes no tier-3 source at all.
     */
    const TableDatabase *peek(const char *table_file) const;

    std::map<std::string, std::shared_ptr<TableDatabase>> tables_;
};

/* ===========================================================================
 * The context
 * ======================================================================== */

class TableContext : public ContextBackend {
public:
    explicit TableContext(TableEngine *engine) : engine_(engine) {}

    bool process_key(const KeyEvent &key, const OptionReader &options,
                     const SurroundingTextView &doc, Composition *model, Output *out) override;
    void reset(Composition *model) override;
    void commit(const OptionReader &options,
                Composition *model,
                Output *out) override;
    pathime_status_t select_candidate(size_t index, const OptionReader &options,
                                      Composition *model, Output *out) override;
    pathime_status_t set_cursor(size_t index, const OptionReader &options,
                                Composition *model) override;
    void options_changed(const OptionReader &options, Composition *model, Output *out) override;
    void materialize_candidates(size_t cap, const OptionReader &options,
                                Composition *model) override;

private:
    /* ---- Table selection ---- */

    /**
     * Adopt whatever PATHIME_OPT_TABLE_FILE now resolves to. Returns false when
     * no table is available, which is the state a context starts in and the one
     * in which every key is unhandled.
     */
    bool sync_table(const OptionReader &options);

    const TableProperties &properties() const { return table_->properties(); }

    /* ---- State ---- */

    void clear_state();

    /** The key run as the user sees it: prompts applied, invalid tail appended. */
    std::string displayed_run() const;

    /** Every pre-committed phrase, in order. */
    std::string staged_text() const;

    /** The whole typed run, valid and invalid parts. */
    std::string typed_run() const { return keys_valid_ + keys_invalid_; }

    /* ---- Lookup ---- */

    /** Re-run the lookup for the current valid run and re-rank it. */
    void refresh_matches(const OptionReader &options);

    /** Write the state into @a model, candidates included. */
    void publish(Composition *model) const;

    /* ---- Transitions ---- */

    bool take_input_scalar(uint32_t scalar, const OptionReader &options, Output *out);
    bool take_backspace(const OptionReader &options);

    /** Stage the current run as a segment using candidate @a index. */
    void stage_segment(size_t index, const OptionReader &options);

    /**
     * Commit everything staged plus @a text, and clear.
     *
     * @a chosen_keys is the key run @a text was chosen for, or "" when the text
     * is not a candidate — the literal run Return commits, or a lone character
     * passed through by the trailing-character rule. That distinction is what
     * learning turns on: a user
     * who typed their way out of a composition expressed no preference, and
     * recording one would teach the table something the user did not say.
     */
    void commit_phrase(const std::string &text, const std::string &chosen_keys,
                       const OptionReader &options, Output *out);

    /**
     * Record one (keys, phrase) selection in the user database, if the
     * table adapts and the client left learning on.
     */
    void learn(const std::string &keys, const std::string &phrase,
               const OptionReader &options);

    /**
     * The full-width and Chinese-punctuation conversion of §11.4, or the
     * character unchanged.
     *
     * Shared with the pyzy adapter (`punctuation.h`) rather than transcribed
     * from §11.4, so that PATHIME_OPT_LATIN_WIDTH and
     * PATHIME_OPT_PUNCTUATION_WIDTH mean one thing across both Chinese engines.
     * The four characters where the two references disagree are named in
     * punctuation.h.
     *
     * Applies only to a CJK table, which is §11.4's own `is_db_cjk` gate: a
     * table for a Latin script has no business turning `<` into 《.
     */
    std::string convert_width(uint32_t scalar, const OptionReader &options);

    /**
     * Commit @a scalar at the negotiated width when that changes it, and report
     * whether the key was taken. For the two paths where nothing is composing
     * and the key is not table input.
     */
    bool emit_converted(uint32_t scalar, const OptionReader &options,
                        Composition *model, Output *out);

    TableEngine *engine_;
    std::shared_ptr<TableDatabase> table_;
    std::string table_value_;   /* the option value `table_` was loaded from */

    std::string keys_valid_;    /* the run that still matches something */
    std::string keys_invalid_;  /* what was typed past the last match */
    std::vector<Segment> segments_;
    std::vector<PhraseMatch> matches_;
    size_t cursor_ = 0;

    /**
     * The quote alternation and digit look-behind punctuation.h keeps.
     *
     * Cleared with the composition, on the same reasoning pyzy's copy is: the
     * look-behind is over the document, and a reset means the engine no longer
     * knows what precedes the caret.
     */
    PunctuationState punctuation_;
};

/* ---- Engine --------------------------------------------------------------- */

std::shared_ptr<TableDatabase> TableEngine::load(const std::string &value)
{
    const auto cached = tables_.find(value);
    if (cached != tables_.end()) {
        return cached->second;
    }

    std::shared_ptr<TableDatabase> table;
    const std::string path = resolve_table_path(value);
    if (!path.empty() && is_regular_file(path)) {
        std::string error;
        std::unique_ptr<TableDatabase> opened =
            TableDatabase::open(path, user_db_path(path), &error);
        table = std::move(opened);
    }

    tables_[value] = table;
    return table;
}

const TableDatabase *TableEngine::peek(const char *table_file) const
{
    if (table_file == nullptr || *table_file == '\0') {
        return nullptr;
    }
    const auto cached = tables_.find(table_file);
    if (cached == tables_.end()) {
        return nullptr;
    }
    return cached->second.get();
}

std::unique_ptr<ContextBackend> TableEngine::create_context(const OptionReader &options)
{
    std::unique_ptr<TableContext> context(new TableContext(this));

    /*
     * The table is resolved lazily rather than here, because a context is
     * routinely created before PATHIME_OPT_TABLE_FILE is set on it — the header
     * documents a context with no table as a legitimate state that reports
     * every key unhandled, not as a creation failure. Touching `options` once
     * anyway so the first key does not pay for a cold cache.
     */
    (void)options;
    return context;
}

/* ---- Context: table selection --------------------------------------------- */

bool TableContext::sync_table(const OptionReader &options)
{
    const std::string value = options.text(PATHIME_OPT_TABLE_FILE);
    if (value == table_value_ && table_ != nullptr) {
        return true;
    }

    if (value != table_value_) {
        /*
         * The option resets the composition, so core has already called
         * reset() by the time a changed value is seen here on a later key.
         * Clearing again is belt-and-braces for the path where the value
         * changes at the engine level while this context is mid-composition:
         * the broadcast reaches options_changed(), and the keys accumulated
         * against the old table mean nothing against the new one.
         */
        clear_state();
        table_value_ = value;
        table_ = engine_->load(value);
    }

    return table_ != nullptr;
}

/* ---- Context: state ------------------------------------------------------- */

void TableContext::clear_state()
{
    keys_valid_.clear();
    keys_invalid_.clear();
    segments_.clear();
    matches_.clear();
    cursor_ = 0;
}

std::string TableContext::staged_text() const
{
    std::string out;
    for (const Segment &segment : segments_) {
        out += segment.phrase;
    }
    return out;
}

std::string TableContext::displayed_run() const
{
    const std::map<char32_t, std::string> &prompts = properties().char_prompts;
    if (prompts.empty()) {
        return typed_run();
    }

    /*
     * Prompt substitution applies to the valid run only. The invalid
     * tail is shown as typed, because a character that matched nothing has no
     * prompt to stand for it — and because seeing the raw character is what
     * tells the user which keystroke went wrong.
     */
    std::string out;
    size_t offset = 0;
    uint32_t scalar = 0;
    while (utf8_next_scalar(keys_valid_.data(), keys_valid_.size(), &offset, &scalar)) {
        const auto prompt = prompts.find(static_cast<char32_t>(scalar));
        if (prompt != prompts.end()) {
            out += prompt->second;
        } else {
            utf8_append_scalar(out, scalar);
        }
    }
    return out + keys_invalid_;
}

/* ---- Context: lookup ------------------------------------------------------ */

void TableContext::refresh_matches(const OptionReader &options)
{
    matches_.clear();
    cursor_ = 0;

    if (table_ == nullptr || keys_valid_.empty()) {
        return;
    }

    if (!table_->lookup(keys_valid_, &matches_)) {
        return;
    }

    const pathime_chinese_variant_t variant = static_cast<pathime_chinese_variant_t>(
        options.number(PATHIME_OPT_CHINESE_VARIANT));
    rank_candidates(keys_valid_, variant, properties().is_chinese, &matches_);

    /*
     * PATHIME_OPT_TABLE_SINGLE_CHAR_ONLY drops every multi-character phrase.
     * Applied after ranking rather than as part of it, because it is a client's
     * narrowing of what it wants to see, not one of the table's ordering rules.
     */
    if (options.flag(PATHIME_OPT_TABLE_SINGLE_CHAR_ONLY)) {
        matches_.erase(std::remove_if(matches_.begin(), matches_.end(),
                                      [](const PhraseMatch &match) {
                                          size_t offset = 0;
                                          uint32_t scalar = 0;
                                          return !utf8_next_scalar(match.phrase.data(),
                                                                   match.phrase.size(),
                                                                   &offset, &scalar) ||
                                                 offset != match.phrase.size();
                                      }),
                       matches_.end());
    }
}

void TableContext::publish(Composition *model) const
{
    model->settled = staged_text();
    model->active = displayed_run();
    model->tail.clear();

    model->candidates.clear();
    for (const PhraseMatch &match : matches_) {
        model->candidates.push_back(match.phrase);
    }
    model->cursor = (cursor_ < model->candidates.size()) ? cursor_ : 0;
}

/* ---- Context: transitions ------------------------------------------------- */

std::string TableContext::convert_width(uint32_t scalar, const OptionReader &options)
{
    std::string out;
    if (table_ == nullptr || !properties().is_cjk || !emittable(scalar)) {
        utf8_append_scalar(out, scalar);
        return out;
    }
    return emit_text(static_cast<char>(scalar), width_settings(options), &punctuation_);
}

void TableContext::learn(const std::string &keys, const std::string &phrase,
                         const OptionReader &options)
{
    if (table_ == nullptr || keys.empty() || phrase.empty()) {
        return;
    }

    /*
     * Three gates, and they are not the same question. DYNAMIC_ADJUST is the
     * table author saying this method reorders by use at all; PATHIME_OPT_LEARNING
     * is the client's veto over a table that does; and NO_CHECK_CHARS is the
     * table author excluding particular characters from adjustment (§3.1).
     */
    if (!properties().dynamic_adjust || !options.flag(PATHIME_OPT_LEARNING)) {
        return;
    }

    if (!properties().no_check_chars.empty()) {
        size_t offset = 0;
        uint32_t scalar = 0;
        while (utf8_next_scalar(phrase.data(), phrase.size(), &offset, &scalar)) {
            if (properties().no_check_chars.count(static_cast<char32_t>(scalar)) != 0) {
                return;
            }
        }
    }

    table_->record_selection(keys, phrase);
}

void TableContext::commit_phrase(const std::string &text, const std::string &chosen_keys,
                                 const OptionReader &options, Output *out)
{
    /*
     * Every staged segment reached the client as text the user chose, so each
     * one is learned, not just the last. Learning happens before clear_state()
     * because that is what drops the segments.
     */
    for (const Segment &segment : segments_) {
        learn(segment.keys, segment.phrase, options);
    }
    if (!chosen_keys.empty()) {
        learn(chosen_keys, text, options);
    }

    out->commit += staged_text();
    out->commit += text;

    /*
     * The digit look-behind is over the *document*, not over the punctuation
     * layer's own output, so what this engine commits counts towards it: a
     * Chinese character settled between a digit and a period has to disarm the
     * "1.5" rule. Recorded here, in the order the two commits reach the client,
     * because only the caller knows which came first.
     */
    punctuation_.note_commit(text.empty() ? staged_text() : text);
    clear_state();
}

void TableContext::stage_segment(size_t index, const OptionReader &options)
{
    Segment segment;
    segment.keys = keys_valid_;
    segment.phrase = (index < matches_.size()) ? matches_[index].phrase : typed_run();
    segments_.push_back(segment);

    keys_valid_.clear();
    keys_invalid_.clear();
    refresh_matches(options);
}

bool TableContext::take_input_scalar(uint32_t scalar, const OptionReader &options, Output *out)
{
    const TableProperties &props = properties();

    /*
     * A run that has already reached a boundary stages before
     * the new character joins, so the boundary is enforced against what was
     * typed rather than against what is about to be.
     */
    const size_t run_length = utf8_scalar_count(keys_valid_.data(), keys_valid_.size());
    if (keys_invalid_.empty() && run_length > 0 && !matches_.empty()) {
        const std::set<size_t> boundaries = props.commit_boundaries();
        const bool at_max = run_length >= props.max_key_length;
        const bool at_rule_boundary = boundaries.count(run_length) != 0 && !at_max;

        if (at_rule_boundary || (at_max && options.flag(PATHIME_OPT_TABLE_AUTO_COMMIT))) {
            stage_segment(cursor_, options);
        } else if (at_max) {
            /*
             * At MAX_KEY_LENGTH with auto-commit off there is nowhere for the
             * character to go: the run cannot grow and nothing stages it. The
             * key is absorbed rather than passed to the client, because letting
             * it through would insert a latin letter into the middle of a
             * composition the user is still building.
             */
            return true;
        }
    }

    std::string candidate_run = keys_valid_;
    utf8_append_scalar(candidate_run, scalar);

    /* An invalid tail already exists: nothing more can match, so extend it. */
    if (!keys_invalid_.empty()) {
        utf8_append_scalar(keys_invalid_, scalar);
        return true;
    }

    const std::string previous_valid = keys_valid_;
    const std::vector<PhraseMatch> previous_matches = matches_;

    keys_valid_ = candidate_run;
    refresh_matches(options);

    if (!matches_.empty()) {
        /*
         * A lone exact match under AUTO_COMMIT commits at once,
         * which is what makes a fixed-length table like Wubi feel like typing
         * rather than like selecting.
         */
        if (matches_.size() == 1 && options.flag(PATHIME_OPT_TABLE_AUTO_COMMIT) &&
            matches_[0].tabkeys == keys_valid_) {
            commit_phrase(matches_[0].phrase, keys_valid_, options, out);
        }
        return true;
    }

    /* Step 3: the run now matches nothing. */
    if (options.flag(PATHIME_OPT_TABLE_AUTO_SELECT) && !previous_matches.empty()) {
        /*
         * Drop the character, commit what the previous run produced, then
         * reprocess the character as the start of a fresh run. The recursion is
         * one level deep by construction: the run it restarts into is empty, so
         * this branch cannot be reached again for the same key.
         */
        keys_valid_ = previous_valid;
        matches_ = previous_matches;
        commit_phrase(matches_[cursor_ < matches_.size() ? cursor_ : 0].phrase, keys_valid_,
               options, out);
        refresh_matches(options);
        return take_input_scalar(scalar, options, out);
    }

    keys_valid_ = previous_valid;
    matches_ = previous_matches;
    utf8_append_scalar(keys_invalid_, scalar);
    return true;
}

bool TableContext::take_backspace(const OptionReader &options)
{
    if (!keys_invalid_.empty()) {
        const size_t last = utf8_byte_offset(
            keys_invalid_.data(), keys_invalid_.size(),
            utf8_scalar_count(keys_invalid_.data(), keys_invalid_.size()) - 1);
        keys_invalid_.erase(last);
        return true;
    }

    if (!keys_valid_.empty()) {
        const size_t last = utf8_byte_offset(
            keys_valid_.data(), keys_valid_.size(),
            utf8_scalar_count(keys_valid_.data(), keys_valid_.size()) - 1);
        keys_valid_.erase(last);
        refresh_matches(options);
        return true;
    }

    if (!segments_.empty()) {
        /*
         * The run is empty, so backspace takes apart the most recent staged
         * segment: its keys become the current run again, minus their
         * last character. That is what makes a mis-staged segment repairable
         * without discarding everything before it.
         */
        const Segment segment = segments_.back();
        segments_.pop_back();
        keys_valid_ = segment.keys;
        if (!keys_valid_.empty()) {
            const size_t last = utf8_byte_offset(
                keys_valid_.data(), keys_valid_.size(),
                utf8_scalar_count(keys_valid_.data(), keys_valid_.size()) - 1);
            keys_valid_.erase(last);
        }
        refresh_matches(options);
        return true;
    }

    return false;  /* nothing composing: the client's own backspace */
}

/* ---- Context: the backend interface --------------------------------------- */

bool TableContext::process_key(const KeyEvent &key, const OptionReader &options,
                               const SurroundingTextView &doc, Composition *model, Output *out)
{
    (void)doc;  /* this engine composes in the preedit; the document is not involved */

    if (!sync_table(options)) {
        return false;  /* no table: every key is the client's (header, TABLE_FILE) */
    }

    /*
     * After sync_table(), not before: adopting a different table clears the
     * run, and a `composing` computed against the old one would send Return and
     * Escape down their mid-composition branches with nothing to commit.
     */
    const bool composing = !keys_valid_.empty() || !keys_invalid_.empty() || !segments_.empty();

    /* A chorded key is a client shortcut, never composition input. */
    if (is_chorded(key)) {
        return false;
    }

    const uint32_t scalar = keysym_to_scalar(key.keysym);

    switch (key.keysym) {
    case PATHIME_KEY_BACKSPACE: {
        const bool handled = take_backspace(options);
        if (handled) {
            publish(model);
        }
        return handled;
    }

    case PATHIME_KEY_ESCAPE:
        if (!composing) {
            return false;
        }
        /*
         * Escape discards without committing, which is reset()'s meaning
         * applied to one key. The header does not fix Escape the way it fixes
         * Space and Return, but every engine that composes needs a way to
         * abandon, and discarding is what a table user expects — the run is
         * keys, not text they would want kept.
         */
        clear_state();
        publish(model);
        return true;

    case PATHIME_KEY_RETURN:
        if (!composing) {
            return false;
        }
        /*
         * Commit the literal input, which is ibus-table's rule for Return too.
         * The header's rule is that it ends the composition without applying a
         * conversion the user did not choose, and for this engine what the user
         * typed is the key run itself.
         *
         * Where the table has char prompts, the committed text is therefore not
         * character-for-character what the preedit showed — the preedit renders
         * `a` as 日 and this commits `a`. That is the one place the header's
         * "this is the text that would be committed if the composition ended
         * right now" is inexact for this engine, and it is stated here rather
         * than papered over.
         */
        commit_phrase(typed_run(), std::string(), options, out);
        publish(model);
        return true;

    case PATHIME_KEY_LEFT:
    case PATHIME_KEY_RIGHT:
    case PATHIME_KEY_HOME:
    case PATHIME_KEY_END:
        /*
         * Declined while composing, for the reason the model gives: there is no
         * cursor inside a span to move. Same answer as anthy and pyzy, and the
         * same cost: the run is editable only from its end.
         */
        return false;

    default:
        break;
    }

    if (key.keysym == PATHIME_KEY_SPACE) {
        if (composing) {
            /*
             * The header fixes Space as "ask for conversion", beginning at the
             * hovered candidate. With no candidates — a run that matched
             * nothing — there is nothing to convert, so the literal run
             * commits, which is the same answer Return gives.
             */
            if (!matches_.empty()) {
                const size_t index = (cursor_ < matches_.size()) ? cursor_ : 0;
                commit_phrase(matches_[index].phrase, keys_valid_, options, out);
            } else {
                commit_phrase(typed_run(), std::string(), options, out);
            }
            publish(model);
            return true;
        }

        /*
         * Nothing composing. A space never starts a key run — the header fixes
         * Space as the convert key — so the only question left is the
         * negotiated width.
         */
        return emit_converted(scalar, options, model, out);
    }

    if (scalar == 0) {
        return false;  /* a named key with no character: not ours */
    }

    const char32_t typed = static_cast<char32_t>(scalar);
    const bool starts_run = keys_valid_.empty() && keys_invalid_.empty();
    const bool is_input = starts_run ? properties().is_start_char(typed)
                                     : properties().is_input_char(typed);

    if (is_input) {
        const bool handled = take_input_scalar(scalar, options, out);
        if (handled) {
            publish(model);
        }
        return handled;
    }

    /*
     * Any other character ends the composition and then commits itself.
     * PATHIME_OPT_TABLE_INVALID_INPUT chooses which of the two endings applies
     * — the hovered candidate, or the keys as typed.
     */
    if (composing) {
        std::string ending;
        std::string ending_keys;
        if (options.number(PATHIME_OPT_TABLE_INVALID_INPUT) ==
                PATHIME_TABLE_INVALID_COMMIT_CANDIDATE &&
            !matches_.empty()) {
            ending = matches_[(cursor_ < matches_.size()) ? cursor_ : 0].phrase;
            ending_keys = keys_valid_;
        } else {
            ending = typed_run();
        }
        commit_phrase(ending, ending_keys, options, out);
        /*
         * The character itself follows, at the negotiated width. The commit
         * above has already been recorded against the digit look-behind by
         * commit_phrase(), so a Chinese character settled between a digit and a period
         * disarms it — which is the whole reason note_commit() takes both
         * sources in the order they reach the client.
         */
        const std::string text = convert_width(scalar, options);
        out->commit += text;
        punctuation_.note_commit(text);
        publish(model);
        return true;
    }

    /* Nothing composing and not input: converted if the width says so. */
    return emit_converted(scalar, options, model, out);
}

bool TableContext::emit_converted(uint32_t scalar, const OptionReader &options,
                                  Composition *model, Output *out)
{
    /*
     * Every printable ASCII key is taken when the table is CJK, including the
     * ones that pass through unchanged — the same rule the pyzy adapter follows,
     * and for the same two reasons.
     *
     * The first is that it is what makes the two look-behind substitutions
     * possible at all. The default configuration is full-width punctuation with
     * half-width Latin, so in the *default* case a digit converts to itself; if
     * that meant declining the key, the engine would never see the `1` in
     * "1.5", would not disarm the decimal-point rule, and would commit "1。5" —
     * against an explicit promise at PATHIME_OPT_PUNCTUATION_WIDTH. A key the
     * client inserts itself is one this engine never saw.
     *
     * The second is that it is correct for an IME the user has switched into.
     * The mixed table-and-Latin case that PATHIME_OPT_TABLE_INVALID_INPUT
     * exists for is about a key arriving *mid-composition*, which is handled
     * above and never reaches here.
     *
     * A non-CJK table declines everything, which is §11.4's own `is_db_cjk`
     * gate: a table for a Latin script has no business claiming ASCII.
     */
    if (table_ == nullptr || !properties().is_cjk || !emittable(scalar)) {
        return false;
    }

    const std::string text = convert_width(scalar, options);
    out->commit += text;
    punctuation_.note_commit(text);
    publish(model);
    return true;
}

void TableContext::reset(Composition *model)
{
    (void)model;
    /*
     * Discards without committing, which is what the header requires of
     * pathime_context_reset(). There is no library state to unwind beyond this
     * context's own — the loaded table belongs to the engine and outlives every
     * reset.
     *
     * The punctuation state goes too: its quote alternation and digit
     * look-behind describe what precedes the caret, and after a reset the engine
     * no longer knows that. This is the one thing commit() does differently —
     * it updates that look-behind rather than clearing it, because after a
     * commit the engine knows exactly what precedes the caret.
     */
    clear_state();
    punctuation_.clear();
}

/**
 * Exactly what Return does: commit the literal key run, without applying a
 * conversion the user did not choose.
 *
 * Where the table supplies char prompts the committed text is therefore not
 * character-for-character what the preedit showed — the preedit renders `a` as
 * 日 and this commits `a` — which is the same documented inexactness Return
 * carries, and for the same reason. Staged segments go with it, as they do on
 * every other commit path here.
 */
void TableContext::commit(const OptionReader &options, Composition *model, Output *out)
{
    const bool composing =
        !keys_valid_.empty() || !keys_invalid_.empty() || !segments_.empty();
    if (!composing) {
        return;
    }
    commit_phrase(typed_run(), std::string(), options, out);
    publish(model);
}

pathime_status_t TableContext::select_candidate(size_t index, const OptionReader &options,
                                                Composition *model, Output *out)
{
    if (table_ == nullptr) {
        return PATHIME_ERROR_UNSUPPORTED;
    }
    if (index >= matches_.size()) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    /*
     * A selection finalizes the whole preedit: the staged segments plus the
     * chosen phrase. ibus-table's phrase-building variant — staging instead of
     * committing — is not a second operation here, because staging already
     * happens on its own at the segment boundaries. Giving the client a second
     * meaning for the same call would make the result depend on a modifier the
     * API does not carry.
     */
    commit_phrase(matches_[index].phrase, keys_valid_, options, out);
    refresh_matches(options);
    publish(model);
    return PATHIME_OK;
}

pathime_status_t TableContext::set_cursor(size_t index, const OptionReader &options,
                                          Composition *model)
{
    (void)options;
    if (index >= matches_.size()) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    /*
     * The hover moves and nothing else. Unlike anthy and pyzy, the preedit is
     * not rewritten to preview the hovered candidate: the preedit is the key
     * run, and a table method's key run does not change because the user is
     * looking at a different phrase. So there is no preview to keep in step,
     * which is why this is a one-line implementation where pyzy's is not.
     */
    cursor_ = index;
    model->cursor = index;
    return PATHIME_OK;
}

void TableContext::options_changed(const OptionReader &options, Composition *model, Output *out)
{
    (void)out;

    if (!sync_table(options)) {
        publish(model);
        return;
    }

    /*
     * The candidate list depends on three resolved options — the Chinese
     * variant, the single-character filter, and which table is loaded — so a
     * change to any of them has to re-rank what is already on screen. This is
     * the moment backend.h describes: pulling next time is not enough, because
     * there may be no next time until the user types.
     */
    refresh_matches(options);
    publish(model);
}

void TableContext::materialize_candidates(size_t cap, const OptionReader &options,
                                          Composition *model)
{
    (void)options;

    /*
     * Nothing to pump. The lookup already produced the whole list — a table
     * query is one statement over one table, and §8 caps it at 100 — so the
     * candidates were materialized when the run last changed. This is the
     * cheapest shape the eager-materialization obligation can take, and the
     * opposite of pyzy's, whose hasCandidate() is lazy and mutating.
     */
    model->candidates.clear();
    for (const PhraseMatch &match : matches_) {
        if (model->candidates.size() >= cap) {
            break;
        }
        model->candidates.push_back(match.phrase);
    }
    model->cursor = (cursor_ < model->candidates.size()) ? cursor_ : 0;
}

}  // namespace
}  // namespace table

/* ===========================================================================
 * The process-global hooks backend.h declares
 * ======================================================================== */

bool table_global_init(const char *data_dir, const char *resource_dir)
{
    table::g_table_dir.clear();
    table::g_user_dir.clear();

    if (resource_dir != nullptr) {
        table::g_table_dir = path_join(resource_dir, "table");
    }
    if (data_dir != nullptr) {
        table::g_user_dir = path_join(data_dir, "table");
    }

    /*
     * `.db` and nothing else: the directory may also hold the `.cache` files of
     * spec §5.4 and whatever a packager put beside them, and a name that did
     * not resolve to a table would be a choice that fails when taken.
     */
    for (const std::string &entry : list_directory(table::g_table_dir)) {
        const size_t dot = entry.rfind(".db");
        if (dot != std::string::npos && dot + 3 == entry.size() && dot > 0) {
            table::g_installed.push_back(entry.substr(0, dot));
        }
    }

    /*
     * Reported available whenever there is a resource directory to resolve
     * names against. Deliberately not a test that the directory exists: a
     * client that names an absolute path needs no shipped tables at all, and
     * failing the engine because this build shipped none would deny that client
     * an engine that would have worked for it.
     */
    return !table::g_table_dir.empty();
}

void table_global_shutdown()
{
    table::g_table_dir.clear();
    table::g_user_dir.clear();
    table::g_installed.clear();
}

size_t table_installed_count()
{
    return table::g_installed.size();
}

const char *table_installed_name(size_t index)
{
    if (index >= table::g_installed.size()) {
        return "";
    }
    return table::g_installed[index].c_str();
}

std::unique_ptr<EngineBackend> table_create_engine()
{
    return std::unique_ptr<EngineBackend>(new table::TableEngine());
}

}  // namespace pathime
