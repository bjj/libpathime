/*
 * A table's declaration: everything docs/ibus-table-spec.md §3.1 puts in the
 * source file's DEFINITION section, typed.
 *
 * This is deliberately its own file rather than part of the .txt parser,
 * because there are two ways to arrive at it and only one of them involves the
 * source format. A compiled database carries the same fields as `attr`/`val`
 * rows in its `ime` table (§4.1), so opening an installed ibus-table `.db`
 * reconstructs a TableProperties without any .txt ever being read. The parser
 * (table_source.h) and the database (table_db.h) both produce one of these; the
 * engine consumes it and knows neither.
 *
 * The raw attribute map is kept alongside the typed fields, because compilation
 * has to write back every key it read — including the ones a libpathime engine
 * ignores as client policy (§3.1: SELECT_KEYS, PAGE_UP_KEYS, ORIENTATION, …).
 * Dropping those on the floor would mean a table we compiled could not be read
 * by ibus-table itself, and the data contract runs both ways.
 */

#ifndef LIBPATHIME_SRC_ENGINES_TABLE_TABLE_PROPERTIES_H
#define LIBPATHIME_SRC_ENGINES_TABLE_TABLE_PROPERTIES_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <pathime/pathime.h>

namespace pathime {
namespace table {

/**
 * The parsed form of the RULES declaration (§3.5): how a compound phrase's
 * lookup key is derived from its characters' goucima.
 *
 * A position is (character index, index within that character's goucima), both
 * 1-based, with a negative character index counting from the end — `p-11` is
 * the first goucima character of the phrase's last character.
 */
struct Rules {
    using Position = std::pair<int, int>;

    /** `ceN` — the rule for a phrase of exactly N characters. */
    std::map<size_t, std::vector<Position>> exact;

    /**
     * The `caN` threshold: the rule under `exact[above]` also applies to every
     * phrase longer than `above`. Zero when the table declares no `ca` rule,
     * which means phrases longer than the largest `ceN` derive no key.
     */
    size_t above = 0;

    bool empty() const { return exact.empty(); }

    /** The positions to apply to a phrase of @a length characters, or null. */
    const std::vector<Position> *for_length(size_t length) const;
};

/** Parse a RULES value. Returns false on syntax the spec does not allow. */
bool parse_rules(const std::string &value, Rules *out);

/**
 * One table's declaration.
 *
 * Defaults are the spec's (§3.1), applied by the constructor, so a properties
 * object is usable before anything is read into it and a table that declares
 * nothing behaves like ibus-table's own default table.
 */
struct TableProperties {
    /* ---- Metadata ---- */
    std::string name;
    std::string uuid;
    uint64_t serial_number = 0;

    /* ---- Data/lookup ---- */

    /**
     * The locales the table declares, and the two questions the engine actually
     * asks of them: `is_chinese` gates variant filtering (§11.1) and
     * `is_cjk` gates full-width conversion (§11.4).
     */
    std::vector<std::string> languages;
    bool is_chinese = false;
    bool is_cjk = false;

    /** The scalars accepted as key strokes, as a set for membership tests. */
    std::set<char32_t> valid_input_chars;

    /** If non-empty, only these scalars may begin a key run. */
    std::set<char32_t> start_chars;

    /** Scalars excluded from frequency adjustment and phrase learning (§10.2). */
    std::set<char32_t> no_check_chars;

    size_t max_key_length = 4;
    size_t least_commit_length = 0;

    /** Each empty or exactly one scalar; SQL `_` and `%` respectively (§8.1). */
    std::string single_wildcard;
    std::string multi_wildcard;

    Rules rules;

    /** Per-key preedit substitutions (§3.4, §6.2). Empty when none declared. */
    std::map<char32_t, std::string> char_prompts;

    /* ---- Engine options (tier 3) ---- */
    bool auto_wildcard = true;
    bool auto_commit = false;
    bool auto_select = false;
    bool user_can_define_phrase = false;
    bool dynamic_adjust = false;
    bool pinyin_mode = false;
    bool suggestion_mode = false;
    bool def_full_width_punct = true;
    bool def_full_width_letter = false;

    /**
     * LANGUAGE_FILTER as a pathime_chinese_variant_t, or -1 when the table
     * declares none. `cm0`–`cm4` map onto the enum in order, which is not a
     * coincidence — the enum was given ibus-table's five modes deliberately
     * (docs/ibus-table-options.md), and it is why the table engine accepts all
     * five values where pyzy accepts two.
     */
    int language_filter = -1;

    /**
     * Whether the *compiled database* actually carries pinyin and suggestion
     * rows, as distinct from the table merely declaring the mode.
     *
     * These are not parsed from anything. TableDatabase::open() sets them by
     * looking, and a TableProperties built from source text leaves them false.
     *
     * The distinction is load-bearing rather than pedantic. Four of the five
     * shipped tables declare `PINYIN_MODE = TRUE` — cangjie5, quick5, stroke5
     * and wubi-jidian86 — but their pinyin source (`pinyin_table.txt.bz2`)
     * ships with ibus-table rather than with ibus-table-chinese, so this
     * repository has nothing to compile into them and the table is created
     * empty. Reporting PATHIME_OPT_TABLE_PINYIN_FALLBACK from the declaration
     * alone would tell a client the option is on while it does nothing, and the
     * header promises the opposite: enabling it without pinyin data is
     * PATHIME_ERROR_UNSUPPORTED.
     *
     * A `.db` that ibus-table itself compiled *does* carry the rows, and these
     * flags are what lets such a database light the feature up without the
     * engine caring where the file came from.
     */
    bool pinyin_data = false;
    bool suggestion_data = false;

    /** Every attribute as read, for round-tripping through the `ime` table. */
    std::map<std::string, std::string> attrs;

    /**
     * Record one `attr = val` pair, lowercasing @a key, and update whichever
     * typed field it feeds. Unknown keys are kept in `attrs` and otherwise
     * ignored, which is what lets a table carry fields this engine has no
     * meaning for without failing to load.
     */
    void set(const std::string &key, const std::string &value);

    /**
     * Apply the derivations that need the whole declaration: the language
     * predicates, and VALID_INPUT_CHARS's default. Call once after the last
     * set().
     */
    void finalize();

    /**
     * The key-run lengths at which typing another character stages the current
     * segment into the preedit (§7.5).
     *
     * From RULES when the table has them — the output length of each `ceN` rule
     * below the `caN` threshold — and otherwise from LEAST_COMMIT_LENGTH, which
     * makes every length from it up to MAX_KEY_LENGTH a boundary. MAX_KEY_LENGTH
     * is always one, since a run cannot grow past it.
     */
    std::set<size_t> commit_boundaries() const;

    /** True if @a scalar may extend a key run. */
    bool is_input_char(char32_t scalar) const;

    /** True if @a scalar may begin one. START_CHARS narrows this and nothing else. */
    bool is_start_char(char32_t scalar) const;

    /** True if @a scalar is one of the two configured wildcards. */
    bool is_wildcard(char32_t scalar) const;

    /**
     * Tier 3 (backend.h EngineBackend::declared_number): the value this table
     * declares for @a option, if it declares one.
     *
     * Only the options whose meaning a table author actually chooses are here.
     * PATHIME_OPT_TABLE_FILE is excluded structurally — it is the key tier 3 is
     * looked up by — and the client-policy fields of §3.1 are excluded because
     * they are not options at all in this API.
     */
    bool declared_number(pathime_option_t option, int64_t *out) const;

    /** The string counterpart: the two wildcard characters. */
    const char *declared_text(pathime_option_t option) const;
};

}  // namespace table
}  // namespace pathime

#endif /* LIBPATHIME_SRC_ENGINES_TABLE_TABLE_PROPERTIES_H */
