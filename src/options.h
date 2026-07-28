/*
 * The options machinery, shared by the engine- and context-level entry points
 * in options.cc:
 *
 *  - the static descriptor table — one row per pathime_option_t carrying its
 *    name, value kind, per-engine support, default, and level — backing
 *    pathime_option_count/name and pathime_engine_option_info();
 *  - the two-level value store (engine defaults, per-context overrides) with
 *    set/reset/is_set semantics;
 *  - the kind-typed setter/getter plumbing, including the kind-mismatch and
 *    unsupported-for-this-engine rejections.
 *
 * The inventory itself is settled: it is the Options section of
 * include/pathime/pathime.h, and each option's backend meaning is documented
 * in the docs/ per-library options inventories. One claim to re-verify while
 * implementing: PATHIME_OPT_PINYIN_FUZZY/_CORRECTION are scoped out of
 * bopomofo on reasoning that was not traced all the way through the
 * bopomofo-to-pinyin tables (docs/design-history.md §1, "One claim to re-check"); widening
 * support later is additive and harmless.
 *
 * Only one level of the store lives in one object: an OptionStore is the set
 * of values explicitly set at that level and nothing else. Resolution walks
 * the tiers described in the header's Options section — context store, engine
 * store, the table's own declaration (tier 3, table engine only), then the
 * descriptor's default — and lives in options.cc, because only it can see both
 * levels at once. Tier 3 is the one that is not a store: it is read through
 * EngineBackend::declared_number/declared_text, because the value lives in a
 * data file only the backend can open.
 */

#ifndef LIBPATHIME_SRC_OPTIONS_H
#define LIBPATHIME_SRC_OPTIONS_H

#include <cstdint>
#include <map>
#include <string>

#include <pathime/pathime.h>

namespace pathime {

/**
 * How many engine ids there are. The enum is dense and append-only, so
 * PATHIME_ENGINE_TABLE is the last one; this is what sizes any per-engine
 * array and what bounds the support bitmask below.
 */
constexpr size_t kEngineCount = PATHIME_ENGINE_TABLE + 1;

/** The bit an engine occupies in OptionDescriptor::engines. */
constexpr uint32_t engine_bit(pathime_engine_id_t id)
{
    return 1u << static_cast<unsigned>(id);
}

/**
 * One value explicitly set at one level. Which member is live follows from the
 * option's descriptor type, never from the value: PATHIME_OPTION_STRING uses
 * `text`, every other kind uses `number` (BOOL as 0/1, ENUM as the enumerator,
 * FLAGS as the mask).
 */
struct OptionValue {
    int64_t number = 0;
    std::string text;
};

/**
 * The values explicitly set at one level. Absence is the whole meaning of
 * "not set here" — there is no sentinel value — which is what makes
 * pathime_*_option_is_set() and pathime_*_reset_option() exact rather than
 * heuristic.
 *
 * Sparse rather than a dense array of one slot per option: a typical client
 * sets a handful, and the store is on no hot path.
 */
class OptionStore {
public:
    /** The value set here, or nullptr if this level sets none. */
    const OptionValue *find(pathime_option_t option) const;

    void set_number(pathime_option_t option, int64_t value);
    void set_string(pathime_option_t option, const char *value);

    /** Drop this level's value, so resolution falls through to the next tier. */
    void reset(pathime_option_t option);

    bool is_set(pathime_option_t option) const { return find(option) != nullptr; }

private:
    std::map<pathime_option_t, OptionValue> values_;
};

/**
 * The static description of one option: everything pathime_engine_option_info()
 * reports, plus the support bitmask behind its `supported` member.
 *
 * This table is the single source of truth for the inventory documented in the
 * header's Options section, and the two must not drift. Rows are in enum order
 * and the table is indexed by the option id directly, which is what
 * pathime_option_count()'s density promise rests on.
 */
struct OptionDescriptor {
    /** Stable machine-readable name, e.g. "chinese-variant". Never NULL. */
    const char *name;

    pathime_option_type_t type;

    /** True if setting it discards composition state, as reset() does. */
    bool resets_composition;

    /** Bitwise OR of engine_bit() for every engine that implements it. */
    uint32_t engines;

    /** BOOL/INT/ENUM/FLAGS: the tier-4 library default. */
    int64_t default_value;

    /** INT only: inclusive bounds. */
    int64_t min_value;
    int64_t max_value;

    /**
     * ENUM: bit i set if value i is legal. FLAGS: the honoured bits. Before
     * per-engine narrowing — see option_valid_values().
     */
    uint64_t valid_values;

    /** STRING only: the tier-4 default. Never NULL; "" when there is none. */
    const char *default_string;
};

/** The descriptor for @a option, or nullptr if it is not an option id. */
const OptionDescriptor *option_descriptor(pathime_option_t option);

/** True if @a engine implements @a option. False for a non-option id. */
bool option_supported(pathime_option_t option, pathime_engine_id_t engine);

/**
 * OptionDescriptor::valid_values as @a engine narrows it.
 *
 * Only PATHIME_OPT_CHINESE_VARIANT narrows anything today: pyzy models the
 * variant as a single simplified-or-traditional flag with no mixed mode, so
 * PINYIN and BOPOMOFO accept only the two exclusive values while the table
 * engine accepts all five. The header promises that difference is reported
 * through valid_values rather than hidden, so a client can present exactly the
 * choices that will work.
 */
uint64_t option_valid_values(pathime_option_t option, pathime_engine_id_t engine);

/**
 * Check a number against the descriptor's kind and bounds: range for INT,
 * membership for ENUM, subset for FLAGS, 0-or-1 for BOOL. Returns
 * PATHIME_ERROR_INVALID_ARGUMENT for anything the descriptor disallows.
 *
 * @a setter_kind is the kind the caller's setter implies, so calling the wrong
 * setter for an option's type is caught here too.
 */
pathime_status_t option_check_number(pathime_option_t option,
                                     pathime_engine_id_t engine,
                                     pathime_option_type_t setter_kind,
                                     int64_t value);

/**
 * The same for a string value. Rejects NULL, invalid UTF-8, and — for the two
 * wildcard options, documented as one Unicode scalar value or empty — anything
 * longer than one scalar.
 */
pathime_status_t option_check_string(pathime_option_t option,
                                     pathime_engine_id_t engine,
                                     pathime_option_type_t setter_kind,
                                     const char *value);

/**
 * The resolved effective value of a BOOL, INT, ENUM or FLAGS option — what the
 * engine is actually doing, not which tier supplied it. This is the same walk
 * the public getters perform, exposed because core files outside options.cc
 * need resolved values too: candidates.cc reads PATHIME_OPT_MAX_CANDIDATES on
 * every materialization pass, and engine.cc will read
 * PATHIME_OPT_HANGUL_PREEDIT to answer pathime_engine_requirements().
 *
 * Pass @a ctx to resolve for a context (tiers 1, 2, 3, 4) and nullptr to
 * resolve for an engine alone (tiers 2, 3, 4). When @a ctx is given, @a engine
 * may be nullptr and is taken from it.
 *
 * Resolving for a context also applies the one capping rule in the API:
 * PATHIME_HANGUL_PREEDIT_NONE caps to PATHIME_HANGUL_PREEDIT_SYLLABLE for a
 * context whose client has no delete_surrounding_text. Resolving for an engine
 * deliberately does not cap — an engine has no client — which is what lets
 * pathime_context_create() see the true engine value and reject a client that
 * cannot serve it.
 *
 * Returns the descriptor default when nothing above tier 4 is set, and 0 for a
 * value that is not an option id. There is no error channel: every caller
 * either knows the option is real or wants the default.
 *
 */
int64_t resolve_option_number(const pathime_engine_t *engine,
                              const pathime_context_t *ctx,
                              pathime_option_t option);

/**
 * The same for a STRING option, as a borrowed slice with the lifetime
 * resolve_string() describes: it points into the winning store, into a loaded
 * table's declaration, or at the descriptor's static default, never at a
 * temporary.
 *
 * Added for the table engine, which is the only backend with string options to
 * pull — the table it inputs from and its two wildcard characters. Returns an
 * empty slice for anything that is not a STRING option, which is the same
 * "no distinction between unset and empty" the header gives
 * PATHIME_OPT_TABLE_FILE.
 */
pathime_str_t resolve_option_string(const pathime_engine_t *engine,
                                    const pathime_context_t *ctx,
                                    pathime_option_t option);

}  // namespace pathime

#endif /* LIBPATHIME_SRC_OPTIONS_H */
