/*
 * Implementation of the options machinery declared in options.h, plus the
 * twenty-one public entry points of the header's Options section.
 *
 * Everything here is core code: no backend is named, nothing crosses
 * backend.h, and the file compiles identically whatever the PATHIME_WITH_*
 * macros say. That is why it can be complete on its own — the descriptor
 * table, the two-level store, the validation order and the resolution order
 * are all decidable from include/pathime/pathime.h alone.
 *
 * A resolved value reaches its adapter through ContextBackend::options_changed()
 * (see commit_change), which exists because a mid-composition change has to be
 * felt now rather than at the next keystroke, of which there may be none.
 * Tier 3 — the value a table file itself declares — is the one resolution input
 * that is not held here: it lives in a data file, so resolve_number() and
 * resolve_string() read it through EngineBackend::declared_number/declared_text.
 *
 * Three things worth knowing before editing:
 *
 *  - The descriptor table is the single source of truth for the inventory the
 *    header documents, and the two must not drift. Rows are in enum order and
 *    indexed by the option id; a static_assert ties the row count to the last
 *    enumerator so an option added to the header without a row here fails to
 *    compile.
 *  - Names are ABI. pathime_option_name() is documented as a client's storage
 *    key that never changes once an option ships, so the rule that generated
 *    them is written down at the table and must be applied to every new row.
 *  - Validation order is the library-wide one: NULL and otherwise invalid
 *    arguments first, then initialization, then whether the engine implements
 *    the option, then whether the value is legal. Out-parameters are untouched
 *    on every failure. The exceptions are pathime_option_count/name, which the
 *    header promises are static table lookups usable before pathime_init(),
 *    and the two is_set functions, which have no error channel at all.
 */

#include "options.h"

#include <cstring>
#include <new>
#include <string>

#include <pathime/config.h>

#include "backend.h"
#include "context.h"
#include "engine.h"
#include "init.h"
#include "utf8.h"

namespace pathime {

namespace {

/* ---------------------------------------------------------------------------
 * Engine sets
 *
 * Each option's doc comment in the header names the engines that implement it
 * ("BOOL, default true. Pinyin, Bopomofo, Table."). These are the recurring
 * combinations, named after what the engines have in common rather than after
 * the list, so a row reads as a reason.
 * ------------------------------------------------------------------------- */

constexpr uint32_t kHangul   = engine_bit(PATHIME_ENGINE_HANGUL);
constexpr uint32_t kAnthy    = engine_bit(PATHIME_ENGINE_ANTHY);
constexpr uint32_t kPinyin   = engine_bit(PATHIME_ENGINE_PINYIN);
constexpr uint32_t kBopomofo = engine_bit(PATHIME_ENGINE_BOPOMOFO);
constexpr uint32_t kTable    = engine_bit(PATHIME_ENGINE_TABLE);

/**
 * "Anthy, Pinyin, Bopomofo, Table" — every engine that converts by choosing
 * among candidates, which is every engine except Hangul. Hangul composes
 * syllables from jamo and produces no candidates at all — hanja, libhangul's
 * only candidate-bearing feature, is out of scope — so the options that
 * describe candidate production report themselves unsupported there.
 */
constexpr uint32_t kConverting = kAnthy | kPinyin | kBopomofo | kTable;

/** "Pinyin, Bopomofo, Table" — the engines that emit Chinese. */
constexpr uint32_t kChinese = kPinyin | kBopomofo | kTable;

/** "Pinyin, Bopomofo" — the two engine ids the one pyzy backend supplies. */
constexpr uint32_t kPyzy = kPinyin | kBopomofo;

/* ---------------------------------------------------------------------------
 * Descriptor construction
 * ------------------------------------------------------------------------- */

/**
 * The legal-value mask for an ENUM whose enumerators run 0..count-1. Every
 * value enum in the header is contiguous from zero, so callers pass
 * `LAST_ENUMERATOR + 1` and the mask cannot drift when a value is appended.
 */
constexpr uint64_t enum_mask(int count)
{
    return count >= 64 ? ~UINT64_C(0) : ((UINT64_C(1) << count) - 1);
}

/**
 * The mask of every bit up to and including @a highest, for the FLAGS options.
 * Both flag enums are contiguous from bit 0, and both default to every bit.
 */
constexpr uint64_t flags_mask(uint64_t highest)
{
    return (highest << 1) - 1;
}

/*
 * Field order in OptionDescriptor is name, type, resets_composition, engines,
 * default_value, min_value, max_value, valid_values, default_string. These
 * factories exist so a table row states only what the header states about that
 * option, and so an unused field cannot be filled in by accident.
 */

constexpr OptionDescriptor make_bool(const char *name, uint32_t engines, bool def)
{
    return OptionDescriptor{name, PATHIME_OPTION_BOOL, false, engines,
                            def ? 1 : 0, 0, 0, 0, ""};
}

constexpr OptionDescriptor make_int(const char *name, uint32_t engines,
                                    int64_t def, int64_t min, int64_t max)
{
    return OptionDescriptor{name, PATHIME_OPTION_INT, false, engines,
                            def, min, max, 0, ""};
}

constexpr OptionDescriptor make_enum(const char *name, uint32_t engines,
                                     int64_t def, uint64_t valid,
                                     bool resets = false)
{
    return OptionDescriptor{name, PATHIME_OPTION_ENUM, resets, engines,
                            def, 0, 0, valid, ""};
}

constexpr OptionDescriptor make_flags(const char *name, uint32_t engines,
                                      int64_t def, uint64_t valid)
{
    return OptionDescriptor{name, PATHIME_OPTION_FLAGS, false, engines,
                            def, 0, 0, valid, ""};
}

constexpr OptionDescriptor make_string(const char *name, uint32_t engines,
                                       const char *def, bool resets = false)
{
    return OptionDescriptor{name, PATHIME_OPTION_STRING, resets, engines,
                            0, 0, 0, 0, def};
}

/* ---------------------------------------------------------------------------
 * The descriptor table
 *
 * One row per pathime_option_t, in enum order, indexed by the option id
 * directly — which is what pathime_option_count()'s density promise rests on.
 * Every field is transcribed from that option's doc comment in the header; if
 * the two disagree, the header is right and this table is a bug.
 *
 * Naming rule, applied to every row and to every row ever added: the enumerator
 * name minus the PATHIME_OPT_ prefix, lowercased, underscores turned into
 * hyphens. PATHIME_OPT_CHINESE_VARIANT is "chinese-variant" (the example the
 * header gives), PATHIME_OPT_ANTHY_TYPING_METHOD is "anthy-typing-method".
 * These names are ABI: a client stores them in its own configuration and the
 * header promises they never change once an option ships. Deriving them
 * mechanically is what makes that promise cheap to keep.
 * ------------------------------------------------------------------------- */

constexpr OptionDescriptor kOptions[] = {
    /* ---- Common -------------------------------------------------------- */

    /*
     * PATHIME_OPT_MAX_CANDIDATES. The header states a minimum of 1 and no
     * maximum, so the descriptor states none either: this is a ceiling on how
     * much a lazy backend is pumped, not an allocation, and every backend runs
     * out of candidates long before any plausible cap. Zero is excluded by the
     * minimum rather than special-cased — an engine that converts by selection
     * cannot make progress without a candidate.
     */
    make_int("max-candidates", kConverting, PATHIME_DEFAULT_MAX_CANDIDATES, 1, INT64_MAX),

    /*
     * "Anthy, Table" and not kConverting. pyzy is excluded deliberately: it
     * learns *inside* selectCandidate() and commit() via PhraseEditor::commit(),
     * with no public switch to withhold — only resetCandidate() to unlearn one
     * entry afterwards. Anthy is implementable because anthy_commit_segment()
     * is a separate call the adapter can skip.
     *
     * The rejected alternative was pointing pyzy's user database at a
     * disposable directory when learning is off. It was turned down because it
     * does not answer the second mismatch: this option is per-context, and
     * pyzy's learned data is process-global, so two contexts disagreeing about
     * learning could not both be honoured whatever the directory. Reporting
     * unsupported is the honest answer; PATHIME_ERROR_UNSUPPORTED at the setter
     * tells a client the truth, where silently ignoring the value would not.
     */
    make_bool("learning", kAnthy | kTable, true),
    make_enum("latin-width", kConverting, PATHIME_WIDTH_HALF,
              enum_mask(PATHIME_WIDTH_FULL + 1)),
    make_enum("punctuation-width", kConverting, PATHIME_WIDTH_FULL,
              enum_mask(PATHIME_WIDTH_FULL + 1)),
    make_enum("chinese-variant", kChinese, PATHIME_CHINESE_SIMPLIFIED_ONLY,
              enum_mask(PATHIME_CHINESE_ANY + 1)),
    /*
     * Default true: the phone-keyboard target reads candidates from the first
     * keystroke, and pyzy's are unconditional anyway, so one out-of-the-box
     * behaviour covers every engine that has candidates at all. A
     * desktop-style client turns it off and gets convert-on-request, which is
     * the other shipping paradigm — the header's doc carries both sides.
     */
    make_bool("prediction", kAnthy | kTable, true),
    make_bool("special-phrases", kPyzy, true),
    make_bool("incomplete-input", kChinese, true),

    /* ---- Hangul -------------------------------------------------------- */

    make_enum("hangul-layout", kHangul, PATHIME_HANGUL_LAYOUT_2SET,
              enum_mask(PATHIME_HANGUL_LAYOUT_AHNMATAE + 1)),
    make_bool("hangul-auto-reorder", kHangul, false),
    make_bool("hangul-double-stroke-combine", kHangul, false),
    make_bool("hangul-non-choseong-combine", kHangul, true),
    make_enum("hangul-preedit", kHangul, PATHIME_HANGUL_PREEDIT_SYLLABLE,
              enum_mask(PATHIME_HANGUL_PREEDIT_NONE + 1)),

    /* ---- Anthy --------------------------------------------------------- */

    /*
     * PATHIME_OPT_ANTHY_TYPING_METHOD resets: a pending romaji fragment has no
     * meaning once the keys are read as kana. One of the four options in the
     * inventory that do.
     */
    make_enum("anthy-typing-method", kAnthy, PATHIME_ANTHY_TYPING_ROMAJI,
              enum_mask(PATHIME_ANTHY_TYPING_KANA + 1), true),
    make_enum("anthy-kana-script", kAnthy, PATHIME_ANTHY_SCRIPT_HIRAGANA,
              enum_mask(PATHIME_ANTHY_SCRIPT_HALFWIDTH_KATAKANA + 1)),
    make_enum("anthy-period-style", kAnthy, PATHIME_ANTHY_PERIOD_KUTEN,
              enum_mask(PATHIME_ANTHY_PERIOD_FULLWIDTH + 1)),
    make_enum("anthy-symbol-style", kAnthy, PATHIME_ANTHY_SYMBOL_CORNER_SLASH,
              enum_mask(PATHIME_ANTHY_SYMBOL_BRACKET_MIDDOT + 1)),
    make_enum("anthy-on-period", kAnthy, PATHIME_ANTHY_ON_PERIOD_NOTHING,
              enum_mask(PATHIME_ANTHY_ON_PERIOD_COMMIT + 1)),
    make_bool("anthy-latin-with-shift", kAnthy, true),

    /* ---- Pinyin -------------------------------------------------------- */

    /* Resets: pyzy fixes the scheme when its context is created. */
    make_enum("pinyin-scheme", kPinyin, PATHIME_PINYIN_SCHEME_FULL,
              enum_mask(PATHIME_PINYIN_SCHEME_DOUBLE_XHE + 1), true),
    /*
     * Both FLAGS options default to every bit. Their engine sets differ, and
     * the difference was measured rather than reasoned: bopomofo_table
     * (PinyinParserTable.h:6622, 479 rows) was traced into the pinyin_table
     * entries it points at, and the result confirmed behaviourally.
     *
     * Fuzzy *is* reachable from bopomofo: 61 of those rows carry a
     * PINYIN_FUZZY_* bit across 16 distinct bits, and check_flags
     * (PinyinParser.cc:34-49) makes parseBopomofo stop at a syllable whose bit
     * is clear. Typing ㄈㄨㄥ parses as "fong" and yields 红 with
     * PATHIME_PINYIN_FUZZY_F_H set, and as "fu" with ㄥ stranded when it is
     * clear. So it is kPyzy — both ids the one backend supplies — not kPinyin.
     * Not kChinese: the table engine takes its own key sequences, not pinyin
     * spellings, and has nothing for these bits to mean.
     *
     * Correction is not reachable: zero rows of bopomofo_table reach an entry
     * carrying a PINYIN_CORRECT_* bit. Corrections are Latin typing slips, and
     * there is no bopomofo spelling to slip in.
     *
     * Both options keep their PATHIME_OPT_PINYIN_ prefix and their place in the
     * header's Pinyin section even though fuzzy reaches bopomofo too. The name
     * describes what the rules are *about* — alternate pinyin spellings — and
     * bopomofo reaches them only by being parsed into pinyin first.
     */
    make_flags("pinyin-fuzzy", kPyzy,
               static_cast<int64_t>(flags_mask(PATHIME_PINYIN_FUZZY_ING_IN)),
               flags_mask(PATHIME_PINYIN_FUZZY_ING_IN)),
    make_flags("pinyin-correction", kPinyin,
               static_cast<int64_t>(flags_mask(PATHIME_PINYIN_CORRECT_ON_ONG)),
               flags_mask(PATHIME_PINYIN_CORRECT_ON_ONG)),

    /* ---- Bopomofo ------------------------------------------------------ */

    /* Resets: pyzy stores the new arrangement without re-reading typed keys. */
    make_enum("bopomofo-layout", kBopomofo, PATHIME_BOPOMOFO_LAYOUT_STANDARD,
              enum_mask(PATHIME_BOPOMOFO_LAYOUT_IBM + 1), true),

    /* ---- Table --------------------------------------------------------- */

    /*
     * PATHIME_OPT_TABLE_FILE. No tier-4 default: the header says reading it
     * with no table resolved yields an empty string, and that there is no
     * distinction between unset and empty. Resets, because changing the table
     * changes what the accumulated keys even mean. It is also the option that
     * supplies tier 3 for every other table option.
     */
    make_string("table-file", kTable, "", true),

    make_bool("table-auto-commit", kTable, false),
    make_bool("table-auto-select", kTable, false),
    make_string("table-single-wildcard", kTable, ""),
    make_string("table-multi-wildcard", kTable, ""),
    make_bool("table-single-char-only", kTable, false),
    make_enum("table-invalid-input", kTable, PATHIME_TABLE_INVALID_COMMIT_CANDIDATE,
              enum_mask(PATHIME_TABLE_INVALID_COMMIT_RAW + 1)),
    make_bool("table-pinyin-fallback", kTable, false)
};

constexpr size_t kOptionCount = sizeof(kOptions) / sizeof(kOptions[0]);

/*
 * The whole point of the table being indexed by option id: a row per option, no
 * more and no fewer. An option appended to the header without a row here — or a
 * row added without its enumerator — stops the build rather than silently
 * shifting every id after the insertion point.
 */
static_assert(kOptionCount == static_cast<size_t>(PATHIME_OPT_TABLE_PINYIN_FALLBACK) + 1,
              "the descriptor table has drifted from pathime_option_t");

/* OptionDescriptor::engines is a uint32_t bitmask; see engine_bit(). */
static_assert(kEngineCount <= 32, "too many engine ids for OptionDescriptor::engines");

/* ---------------------------------------------------------------------------
 * Value names
 *
 * The names pathime_option_value_name() hands out, for the two types that have
 * values worth naming. A side table rather than a tenth OptionDescriptor field
 * because only 15 of the 31 options have any: carrying an always-null array
 * through every row, and through five constexpr factories, would cost more
 * than the lookup below.
 *
 * Indexed by *bit position* for both types, which is the one thing to know
 * before reading a row. For an ENUM that is the value itself; for a FLAGS it
 * is the bit's position, so PATHIME_PINYIN_FUZZY_Z_ZH — 1u << 2 — is entry 2.
 * That makes each row read in enumerator order and match the descriptor's
 * valid_values, which is a bitmask in exactly the same terms.
 *
 * Names follow the rule pathime_option_name() follows, one level down: the
 * enumerator minus the prefix its own enum shares, lowercased, underscores
 * turned into hyphens. PATHIME_CHINESE_TRADITIONAL_FIRST is
 * "traditional-first", PATHIME_PINYIN_CORRECT_GN_NG is "gn-ng". These are ABI
 * for the same reason the option names are — a client stores them — so
 * deriving them mechanically is what keeps the promise cheap.
 * ------------------------------------------------------------------------- */

/** Wide enough for the widest row: PATHIME_OPT_PINYIN_FUZZY's twenty bits. */
constexpr size_t kMaxValueNames = 20;

struct OptionValueNames {
    pathime_option_t option;
    /** By bit position. A null entry ends the row; trailing entries are null. */
    const char *names[kMaxValueNames];
};

constexpr OptionValueNames kValueNames[] = {
    {PATHIME_OPT_LATIN_WIDTH, {"half", "full"}},
    {PATHIME_OPT_PUNCTUATION_WIDTH, {"half", "full"}},

    {PATHIME_OPT_CHINESE_VARIANT,
     {"simplified-only", "traditional-only", "simplified-first",
      "traditional-first", "any"}},

    {PATHIME_OPT_HANGUL_LAYOUT,
     {"2set", "2set-yet", "3set-2", "3set-390", "3set-final", "3set-noshift",
      "3set-yet", "romaja", "ahnmatae"}},
    {PATHIME_OPT_HANGUL_PREEDIT, {"syllable", "word", "none"}},

    {PATHIME_OPT_ANTHY_TYPING_METHOD, {"romaji", "kana"}},
    {PATHIME_OPT_ANTHY_KANA_SCRIPT,
     {"hiragana", "katakana", "halfwidth-katakana"}},
    {PATHIME_OPT_ANTHY_PERIOD_STYLE, {"kuten", "fullwidth"}},
    {PATHIME_OPT_ANTHY_SYMBOL_STYLE,
     {"corner-slash", "corner-middot", "bracket-slash", "bracket-middot"}},
    {PATHIME_OPT_ANTHY_ON_PERIOD, {"nothing", "convert", "commit"}},

    {PATHIME_OPT_PINYIN_SCHEME,
     {"full", "double-mspy", "double-zrm", "double-abc", "double-zgpy",
      "double-pyjj", "double-xhe"}},
    {PATHIME_OPT_BOPOMOFO_LAYOUT, {"standard", "ching-yeah", "eten", "ibm"}},

    /* The two FLAGS options. Initials first, finals after, in the order the
     * header groups them. */
    {PATHIME_OPT_PINYIN_FUZZY,
     {"c-ch", "ch-c", "z-zh", "zh-z", "s-sh", "sh-s", "l-n", "n-l", "f-h",
      "h-f", "l-r", "r-l", "k-g", "g-k", "an-ang", "ang-an", "en-eng",
      "eng-en", "in-ing", "ing-in"}},
    {PATHIME_OPT_PINYIN_CORRECTION,
     {"gn-ng", "mg-ng", "iou-iu", "uei-ui", "uen-un", "ue-ve", "v-u",
      "on-ong"}},

    {PATHIME_OPT_TABLE_INVALID_INPUT, {"commit-candidate", "commit-raw"}},
};

/** The row for @a option, or nullptr if it has no named values. */
const OptionValueNames *value_names_row(pathime_option_t option)
{
    for (const OptionValueNames &row : kValueNames) {
        if (row.option == option) {
            return &row;
        }
    }
    return nullptr;
}

/* ---------------------------------------------------------------------------
 * Setter kinds
 * ------------------------------------------------------------------------- */

/**
 * Whether the setter the caller reached for matches the option's type. The
 * header's rule: "Each setter takes the value in its natural form.
 * PATHIME_OPTION_ENUM and PATHIME_OPTION_FLAGS use the int form."
 */
bool setter_matches(pathime_option_type_t setter_kind, pathime_option_type_t type)
{
    switch (setter_kind) {
    case PATHIME_OPTION_BOOL:
        return type == PATHIME_OPTION_BOOL;
    case PATHIME_OPTION_INT:
        return type == PATHIME_OPTION_INT ||
               type == PATHIME_OPTION_ENUM ||
               type == PATHIME_OPTION_FLAGS;
    case PATHIME_OPTION_STRING:
        return type == PATHIME_OPTION_STRING;
    case PATHIME_OPTION_ENUM:
    case PATHIME_OPTION_FLAGS:
        /* No entry point implies these kinds; there is no enum or flags setter,
         * only the int one. Answering exactly is cheaper than asserting. */
        return type == setter_kind;
    }
    return false;
}

/* ---------------------------------------------------------------------------
 * Resolution
 *
 * The header's tier list, walked in order: a value set on the context, a value
 * set on its engine, the table's own declaration, the descriptor default. An
 * engine resolves through the same list minus the tier that cannot apply to it.
 * ------------------------------------------------------------------------- */

/**
 * The explicitly set value that wins, or nullptr when nothing above tier 4 has
 * one. @a ctx is null when resolving for an engine.
 */
const OptionValue *resolve_value(const pathime_engine_t *engine,
                                 const pathime_context_t *ctx,
                                 pathime_option_t option)
{
    if (ctx != nullptr) {
        if (const OptionValue *v = ctx->options.find(option)) {
            return v;  /* tier 1 */
        }
    }

    if (const OptionValue *v = engine->options.find(option)) {
        return v;  /* tier 2 */
    }

    /*
     * Tier 3 is deliberately not here. It is not a stored value — it lives in a
     * data file only the backend can read — so it cannot be returned as an
     * OptionValue pointing at storage that outlives the call. resolve_number()
     * and resolve_string() consult it themselves, each between this function
     * answering nullptr and the descriptor default being applied, which is
     * exactly the position the header's tier list gives it.
     */
    return nullptr;  /* tier 4: the caller applies the descriptor default */
}

/**
 * The table whose declaration supplies tier 3 at the level being resolved for:
 * the context's PATHIME_OPT_TABLE_FILE if it set one, otherwise the engine's.
 *
 * Deliberately *not* resolve_string() on PATHIME_OPT_TABLE_FILE, which would
 * recurse: resolving that option consults tier 3, and tier 3 is keyed by it.
 * Tiers 1 and 2 are the only ones it can come from — the header says the option
 * has no tier-4 default, and a table naming itself is not a thing — so reading
 * the two stores directly is both correct and terminating.
 *
 * Returns "" when neither level names a table, which every declared_*()
 * implementation must answer false for.
 */
const char *tier3_table(const pathime_engine_t *engine, const pathime_context_t *ctx)
{
    if (engine->id != PATHIME_ENGINE_TABLE || engine->backend == nullptr) {
        return "";
    }

    if (ctx != nullptr) {
        if (const OptionValue *v = ctx->options.find(PATHIME_OPT_TABLE_FILE)) {
            return v->text.c_str();
        }
    }
    if (const OptionValue *v = engine->options.find(PATHIME_OPT_TABLE_FILE)) {
        return v->text.c_str();
    }
    return "";
}

/**
 * The resolved number, including the one capping rule in the API.
 *
 * PATHIME_HANGUL_PREEDIT_NONE builds the syllable inside the client's document
 * by deleting what it committed a moment ago, so it is unusable without
 * delete_surrounding_text. A context-level set is rejected and so is context
 * creation, but for a context already running there is nothing left to reject —
 * the value can only have arrived from the engine level, whose setter must not
 * fail because of some unrelated context. So the capability caps the effective
 * value for that context alone, and its getter reports the capped value. This
 * is the one place in the API where a capability caps rather than refuses.
 */
int64_t resolve_number(const pathime_engine_t *engine,
                       const pathime_context_t *ctx,
                       const OptionDescriptor *desc,
                       pathime_option_t option)
{
    const OptionValue *v = resolve_value(engine, ctx, option);

    int64_t value;
    int64_t declared = 0;
    if (v != nullptr) {
        value = v->number;
    } else if (option != PATHIME_OPT_TABLE_FILE &&
               engine->backend != nullptr &&
               engine->backend->declared_number(tier3_table(engine, ctx), option, &declared)) {
        value = declared;  /* tier 3 */
    } else {
        value = desc->default_value;
    }

    if (option == PATHIME_OPT_HANGUL_PREEDIT && ctx != nullptr &&
        value == PATHIME_HANGUL_PREEDIT_NONE && ctx->delete_surrounding_text == nullptr) {
        value = PATHIME_HANGUL_PREEDIT_SYLLABLE;
    }

    return value;
}

/**
 * The resolved string as a borrowed slice. It points either into the winning
 * OptionStore's own std::string — stable until the next call that mutates that
 * engine or context, which is the ordinary lifetime the header promises — or at
 * the descriptor's static default. Never at a temporary.
 */
pathime_str_t resolve_string(const pathime_engine_t *engine,
                             const pathime_context_t *ctx,
                             const OptionDescriptor *desc,
                             pathime_option_t option)
{
    pathime_str_t out;
    const char *bytes = nullptr;

    if (const OptionValue *v = resolve_value(engine, ctx, option)) {
        out.bytes = v->text.c_str();
        out.len = v->text.size();
        return out;
    }

    /*
     * Tier 3. PATHIME_OPT_TABLE_FILE is excluded because it is the key tier 3 is
     * looked up *by*; see tier3_table().
     */
    if (option != PATHIME_OPT_TABLE_FILE && engine->backend != nullptr) {
        bytes = engine->backend->declared_text(tier3_table(engine, ctx), option);
    }
    if (bytes == nullptr) {
        bytes = desc->default_string;  /* tier 4 */
    }

    out.bytes = bytes;
    out.len = std::strlen(bytes);
    return out;
}

/* ---------------------------------------------------------------------------
 * Entry-point prologues
 *
 * The shared validation order, in one place so the public entry points cannot
 * disagree about it.
 * ------------------------------------------------------------------------- */

pathime_status_t check_engine_call(const pathime_engine_t *engine,
                                   pathime_option_t option,
                                   const OptionDescriptor **out_desc)
{
    if (engine == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    const OptionDescriptor *desc = option_descriptor(option);
    if (desc == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    if (!initialized()) {
        return PATHIME_ERROR_NOT_INITIALIZED;
    }

    if (!option_supported(option, engine->id)) {
        return PATHIME_ERROR_UNSUPPORTED;
    }

    *out_desc = desc;
    return PATHIME_OK;
}

pathime_status_t check_context_call(const pathime_context_t *ctx,
                                    pathime_option_t option,
                                    const OptionDescriptor **out_desc)
{
    if (ctx == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }
    return check_engine_call(ctx->engine, option, out_desc);
}

/** The getter half of the kind check; the setters get it from the checkers. */
pathime_status_t check_getter_kind(const OptionDescriptor *desc,
                                   pathime_option_type_t getter_kind)
{
    return setter_matches(getter_kind, desc->type) ? PATHIME_OK
                                                   : PATHIME_ERROR_INVALID_ARGUMENT;
}

/* ---------------------------------------------------------------------------
 * Storing a value
 * ------------------------------------------------------------------------- */

/*
 * The store is a std::map of std::string, so both writes can allocate. An
 * exception must not escape a C entry point, and the header already has the
 * right word for this: OUT_OF_MEMORY is a *failure*, not a rejection — work may
 * have been done. In practice nothing has been done yet at this point, which is
 * strictly better than what the client is promised.
 */

pathime_status_t store_number(OptionStore &store, pathime_option_t option, int64_t value)
{
    try {
        store.set_number(option, value);
    } catch (const std::bad_alloc &) {
        return PATHIME_ERROR_OUT_OF_MEMORY;
    }
    return PATHIME_OK;
}

pathime_status_t store_string(OptionStore &store, pathime_option_t option, const char *value)
{
    try {
        store.set_string(option, value);
    } catch (const std::bad_alloc &) {
        return PATHIME_ERROR_OUT_OF_MEMORY;
    }
    return PATHIME_OK;
}

/**
 * Everything that happens after a level's store has changed: push the newly
 * resolved value at the backend, then put every affected context back in sync.
 *
 * @a ctx is null for an engine-level change, in which case every context that
 * has not overridden @a option is affected — that is what "resolution is late"
 * means in practice, and it is why engine setters are not callback-safe.
 */
/**
 * Hand a context's backend the news that a resolved option moved. Split out
 * because both the per-context path and the engine-level broadcast need it,
 * and because a context with no backend — which every hand-built test context
 * is — must be a no-op rather than a crash.
 */
void notify_options_changed(pathime_context_t *ctx)
{
    if (ctx->backend == nullptr) {
        return;
    }
    const ContextOptions options(ctx);
    ctx->output.clear();
    ctx->backend->options_changed(options, &ctx->model, &ctx->output);
}

pathime_status_t commit_change(pathime_engine_t *engine,
                               pathime_context_t *ctx,
                               const OptionDescriptor *desc,
                               pathime_option_t option)
{
    /*
     * PATHIME_OPT_TABLE_FILE loads its table earlier than this, in
     * set_string()'s call to EngineBackend::prepare_string() — before the store,
     * so a name that does not resolve fails at the setter rather than arriving
     * here as a change to broadcast. That is also what makes tier 3 readable
     * from the moment the option is set.
     *
     * Everything else reaches the backend through ContextBackend's
     * options_changed() hook below, which each affected context is given before
     * its composition is re-assembled. There is no engine-level equivalent and
     * no engine-level state to update: an engine holds dictionaries and tables,
     * not a conversion in progress, so an engine-level set is felt entirely
     * through the contexts it resolves for.
     */

    if (ctx != nullptr) {
        if (desc->resets_composition) {
            /*
             * "Those options reset the context as pathime_context_reset() would,
             * unconditionally and whether or not a composition is in progress."
             * Routing through the public entry point rather than reimplementing
             * it keeps the one reset path in context.cc; the callback it
             * dispatches follows that function's own rule.
             */
            return pathime_context_reset(ctx);
        }
        /*
         * Tell the backend before re-assembling. Without this a mid-composition
         * change is stored and reported by the getters but not felt: the header
         * promises a change takes effect immediately, and for a backend holding
         * a converted result that means re-deriving now rather than at the next
         * keystroke.
         */
        notify_options_changed(ctx);

        /*
         * Force the dispatch: the effective value changed for this context, so
         * its client's view of the composition is replaced even though the
         * assembled text may be identical.
         */
        refresh_composition(ctx, true);
        return PATHIME_OK;
    }

    /*
     * The engine-level broadcast. "An engine-level set changes the effective
     * value for every context that has not overridden that option, immediately,
     * and dispatches composition_changed to each of them."
     *
     * A context that sets the option itself is untouched: tier 1 already wins
     * for it, so nothing about it changed. Note that a context which overrides
     * PATHIME_OPT_TABLE_FILE is likewise untouched by an engine-level change to
     * *that* option, and keeps drawing tier 3 from its own table.
     */
    pathime_status_t first_error = PATHIME_OK;

    for (pathime_context_t *affected : engine->contexts) {
        if (affected->options.is_set(option)) {
            continue;
        }

        if (desc->resets_composition) {
            const pathime_status_t status = pathime_context_reset(affected);
            if (status != PATHIME_OK && first_error == PATHIME_OK) {
                /* Report the first failure, but visit every context: the value
                 * is stored either way, and abandoning the loop would leave the
                 * remaining contexts running on a stale composition. */
                first_error = status;
            }
        } else {
            notify_options_changed(affected);
            refresh_composition(affected, true);
        }
    }

    return first_error;
}

/**
 * The context-level capability rejection for PATHIME_HANGUL_PREEDIT_NONE.
 * Applies to the value the client is asking for, not to the resolved one; the
 * engine level has no client and so never rejects.
 */
pathime_status_t check_client_capability(const pathime_context_t *ctx,
                                         pathime_option_t option,
                                         int64_t value)
{
    if (option == PATHIME_OPT_HANGUL_PREEDIT &&
        value == PATHIME_HANGUL_PREEDIT_NONE &&
        ctx->delete_surrounding_text == nullptr) {
        return PATHIME_ERROR_MISSING_CALLBACK;
    }
    return PATHIME_OK;
}

/* ---------------------------------------------------------------------------
 * Shared bodies
 *
 * Each public setter/getter pair differs only in which level it names, so the
 * body is written once and the entry points supply the level.
 * ------------------------------------------------------------------------- */

pathime_status_t set_number(pathime_engine_t *engine,
                            pathime_context_t *ctx,
                            pathime_option_t option,
                            pathime_option_type_t setter_kind,
                            int64_t value)
{
    const OptionDescriptor *desc = nullptr;
    pathime_status_t status = (ctx != nullptr)
                                  ? check_context_call(ctx, option, &desc)
                                  : check_engine_call(engine, option, &desc);
    if (status != PATHIME_OK) {
        return status;
    }
    if (ctx != nullptr) {
        engine = ctx->engine;
    }

    status = option_check_number(option, engine->id, setter_kind, value);
    if (status != PATHIME_OK) {
        return status;
    }

    if (ctx != nullptr) {
        status = check_client_capability(ctx, option, value);
        if (status != PATHIME_OK) {
            return status;
        }
    }

    /*
     * The last rejection before the store, because it depends on the resolved
     * table rather than on the value: turning the pinyin fallback on requires
     * the table to have been compiled with pinyin data. Tier 3 is the authority
     * on that — the backend reports the option declared only when the compiled
     * database actually carries pinyin rows, which for every table this library
     * ships it does not (TableProperties::pinyin_data).
     *
     * Turning it *off* is always allowed: a client should not have to own a
     * pinyin table to say it does not want the fallback.
     */
    if (option == PATHIME_OPT_TABLE_PINYIN_FALLBACK && value != 0) {
        int64_t declared = 0;
        if (engine->backend == nullptr ||
            !engine->backend->declared_number(tier3_table(engine, ctx), option, &declared) ||
            declared == 0) {
            return PATHIME_ERROR_UNSUPPORTED;
        }
    }

    status = store_number(ctx != nullptr ? ctx->options : engine->options, option, value);
    if (status != PATHIME_OK) {
        return status;
    }

    return commit_change(engine, ctx, desc, option);
}

pathime_status_t set_string(pathime_engine_t *engine,
                            pathime_context_t *ctx,
                            pathime_option_t option,
                            const char *value)
{
    const OptionDescriptor *desc = nullptr;
    pathime_status_t status = (ctx != nullptr)
                                  ? check_context_call(ctx, option, &desc)
                                  : check_engine_call(engine, option, &desc);
    if (status != PATHIME_OK) {
        return status;
    }
    if (ctx != nullptr) {
        engine = ctx->engine;
    }

    status = option_check_string(option, engine->id, PATHIME_OPTION_STRING, value);
    if (status != PATHIME_OK) {
        return status;
    }

    /*
     * The last rejection before the store, and the only one that does work
     * rather than checking a value: PATHIME_OPT_TABLE_FILE loads its table here
     * so a bad name fails at the setter. EngineBackend::prepare_string() carries
     * the reasoning.
     */
    if (engine->backend != nullptr) {
        status = engine->backend->prepare_string(option, value);
        if (status != PATHIME_OK) {
            return status;
        }
    }

    status = store_string(ctx != nullptr ? ctx->options : engine->options, option, value);
    if (status != PATHIME_OK) {
        return status;
    }

    return commit_change(engine, ctx, desc, option);
}

pathime_status_t reset_option(pathime_engine_t *engine,
                              pathime_context_t *ctx,
                              pathime_option_t option)
{
    const OptionDescriptor *desc = nullptr;
    pathime_status_t status = (ctx != nullptr)
                                  ? check_context_call(ctx, option, &desc)
                                  : check_engine_call(engine, option, &desc);
    if (status != PATHIME_OK) {
        return status;
    }
    if (ctx != nullptr) {
        engine = ctx->engine;
    }

    OptionStore &store = (ctx != nullptr) ? ctx->options : engine->options;

    /*
     * "Resetting an option that was never set is a no-op." Read literally, and
     * in preference to the neighbouring "behaves in every other respect like a
     * setter": nothing was dropped, so nothing resolved differently afterwards,
     * so there is nothing to tell a client about — and a reset of an untouched
     * option must not discard a composition in progress just because the option
     * happens to be one of the four that reset.
     */
    if (!store.is_set(option)) {
        return PATHIME_OK;
    }

    store.reset(option);

    return commit_change(engine, ctx, desc, option);
}

/**
 * The body of pathime_context_isolate_options(). Copies rather than sets:
 * every value written is the value the context already resolves — the Hangul
 * cap included, since resolve_number() applies it — so validation has nothing
 * to check and dispatch has nothing to announce. The setter machinery above is
 * deliberately not involved: commit_change() exists to make a change felt, and
 * there is none.
 */
pathime_status_t isolate_options(pathime_context_t *ctx)
{
    if (ctx == nullptr || ctx->engine == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }
    if (!initialized()) {
        return PATHIME_ERROR_NOT_INITIALIZED;
    }

    const pathime_engine_t *engine = ctx->engine;

    for (size_t i = 0; i < kOptionCount; i++) {
        const pathime_option_t option = static_cast<pathime_option_t>(i);
        const OptionDescriptor *desc = &kOptions[i];

        if (!option_supported(option, engine->id) || ctx->options.is_set(option)) {
            continue;
        }

        pathime_status_t status;
        if (desc->type == PATHIME_OPTION_STRING) {
            /*
             * PATHIME_OPT_TABLE_FILE included, and included when it resolves
             * empty: the explicit empty string is a legal value meaning "no
             * table", so a context isolated before any table was chosen pins
             * that, and an engine-level table choice later does not reach it.
             */
            const pathime_str_t value = resolve_string(engine, ctx, desc, option);
            status = store_string(ctx->options, option, value.bytes);
        } else {
            status = store_number(ctx->options, option,
                                  resolve_number(engine, ctx, desc, option));
        }
        if (status != PATHIME_OK) {
            /*
             * OUT_OF_MEMORY, and the header calls it a failure: the options
             * already copied stay copied. Each copy is inert on its own, and a
             * later call resumes where this one stopped, because copied options
             * answer is_set.
             */
            return status;
        }
    }

    return PATHIME_OK;
}

pathime_status_t get_number(const pathime_engine_t *engine,
                            const pathime_context_t *ctx,
                            pathime_option_t option,
                            pathime_option_type_t getter_kind,
                            int64_t *out_number)
{
    if (out_number == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    const OptionDescriptor *desc = nullptr;
    pathime_status_t status = (ctx != nullptr)
                                  ? check_context_call(ctx, option, &desc)
                                  : check_engine_call(engine, option, &desc);
    if (status != PATHIME_OK) {
        return status;
    }
    if (ctx != nullptr) {
        engine = ctx->engine;
    }

    status = check_getter_kind(desc, getter_kind);
    if (status != PATHIME_OK) {
        return status;
    }

    *out_number = resolve_number(engine, ctx, desc, option);
    return PATHIME_OK;
}

pathime_status_t get_string(const pathime_engine_t *engine,
                            const pathime_context_t *ctx,
                            pathime_option_t option,
                            pathime_str_t *out_value)
{
    if (out_value == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    const OptionDescriptor *desc = nullptr;
    pathime_status_t status = (ctx != nullptr)
                                  ? check_context_call(ctx, option, &desc)
                                  : check_engine_call(engine, option, &desc);
    if (status != PATHIME_OK) {
        return status;
    }
    if (ctx != nullptr) {
        engine = ctx->engine;
    }

    status = check_getter_kind(desc, PATHIME_OPTION_STRING);
    if (status != PATHIME_OK) {
        return status;
    }

    *out_value = resolve_string(engine, ctx, desc, option);
    return PATHIME_OK;
}

bool option_is_set(const pathime_engine_t *engine,
                   const pathime_context_t *ctx,
                   pathime_option_t option)
{
    /*
     * No error channel, by design: "the useful reading of all of those is the
     * same — no value has been set here." option_supported() answers false for a
     * value that is not an option id, so it covers that case too.
     */
    if (ctx != nullptr) {
        if (ctx->engine == nullptr) {
            return false;
        }
        engine = ctx->engine;
    } else if (engine == nullptr) {
        return false;
    }

    if (!initialized() || !option_supported(option, engine->id)) {
        return false;
    }

    return (ctx != nullptr) ? ctx->options.is_set(option) : engine->options.is_set(option);
}

}  // namespace

/* ===========================================================================
 * options.h
 * ======================================================================== */

const OptionValue *OptionStore::find(pathime_option_t option) const
{
    const auto it = values_.find(option);
    return (it == values_.end()) ? nullptr : &it->second;
}

void OptionStore::set_number(pathime_option_t option, int64_t value)
{
    OptionValue &slot = values_[option];
    slot.number = value;
    slot.text.clear();
}

void OptionStore::set_string(pathime_option_t option, const char *value)
{
    OptionValue &slot = values_[option];
    slot.number = 0;
    slot.text.assign(value != nullptr ? value : "");
}

void OptionStore::reset(pathime_option_t option)
{
    values_.erase(option);
}

const OptionDescriptor *option_descriptor(pathime_option_t option)
{
    /* Unsigned, so a negative value wraps past the end and one test suffices. */
    const uint64_t index = static_cast<uint64_t>(option);
    return (index < kOptionCount) ? &kOptions[index] : nullptr;
}

bool option_supported(pathime_option_t option, pathime_engine_id_t engine)
{
    const OptionDescriptor *desc = option_descriptor(option);
    if (desc == nullptr || static_cast<uint64_t>(engine) >= kEngineCount) {
        return false;
    }
    return (desc->engines & engine_bit(engine)) != 0;
}

uint64_t option_valid_values(pathime_option_t option, pathime_engine_id_t engine)
{
    const OptionDescriptor *desc = option_descriptor(option);
    if (desc == nullptr) {
        return 0;
    }

    /*
     * The only narrowing in the inventory today. pyzy models the variant as a
     * single simplified-or-traditional flag with no mixed mode
     * (`pyzy_backend.cc`, where it collapses to a bool), so the two engines it
     * supplies accept only the two
     * exclusive values while the table engine accepts all five. Reported rather
     * than hidden, so a client can present exactly the choices that will work.
     */
    if (option == PATHIME_OPT_CHINESE_VARIANT &&
        (engine == PATHIME_ENGINE_PINYIN || engine == PATHIME_ENGINE_BOPOMOFO)) {
        return (UINT64_C(1) << PATHIME_CHINESE_SIMPLIFIED_ONLY) |
               (UINT64_C(1) << PATHIME_CHINESE_TRADITIONAL_ONLY);
    }

    return desc->valid_values;
}

pathime_status_t option_check_number(pathime_option_t option,
                                     pathime_engine_id_t engine,
                                     pathime_option_type_t setter_kind,
                                     int64_t value)
{
    const OptionDescriptor *desc = option_descriptor(option);
    if (desc == nullptr || !setter_matches(setter_kind, desc->type)) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    const uint64_t valid = option_valid_values(option, engine);

    switch (desc->type) {
    case PATHIME_OPTION_BOOL:
        /* The bool setter cannot produce anything else, but the store holds an
         * int64_t and this is the one place that says so. */
        return (value == 0 || value == 1) ? PATHIME_OK : PATHIME_ERROR_INVALID_ARGUMENT;

    case PATHIME_OPTION_INT:
        return (value >= desc->min_value && value <= desc->max_value)
                   ? PATHIME_OK
                   : PATHIME_ERROR_INVALID_ARGUMENT;

    case PATHIME_OPTION_ENUM:
        /* 64 is the ABI ceiling the header states for an enum value. */
        if (value < 0 || value >= 64 || ((valid >> value) & UINT64_C(1)) == 0) {
            return PATHIME_ERROR_INVALID_ARGUMENT;
        }
        return PATHIME_OK;

    case PATHIME_OPTION_FLAGS:
        /* Unknown bits are rejected rather than ignored: silently dropping one
         * would let a client believe a rule is in force that is not. */
        if (value < 0 || (static_cast<uint64_t>(value) & ~valid) != 0) {
            return PATHIME_ERROR_INVALID_ARGUMENT;
        }
        return PATHIME_OK;

    case PATHIME_OPTION_STRING:
        break;
    }

    return PATHIME_ERROR_INVALID_ARGUMENT;
}

pathime_status_t option_check_string(pathime_option_t option,
                                     pathime_engine_id_t engine,
                                     pathime_option_type_t setter_kind,
                                     const char *value)
{
    (void)engine;  /* No string option narrows per engine. Kept for symmetry
                    * with option_check_number(), and because the table engine's
                    * wildcards are the obvious future exception. */

    const OptionDescriptor *desc = option_descriptor(option);
    if (desc == nullptr || !setter_matches(setter_kind, desc->type) || value == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    size_t scalars = 0;
    if (!utf8_validate_z(value, &scalars)) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    /*
     * The two wildcard options are documented as "One character, or empty to
     * disable", where one character means one Unicode scalar value. Everything
     * else — today only PATHIME_OPT_TABLE_FILE, a path — takes any length.
     */
    if ((option == PATHIME_OPT_TABLE_SINGLE_WILDCARD ||
         option == PATHIME_OPT_TABLE_MULTI_WILDCARD) && scalars > 1) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    return PATHIME_OK;
}

size_t option_string_value_count(pathime_option_t option)
{
#if PATHIME_WITH_TABLE
    if (option == PATHIME_OPT_TABLE_FILE) {
        return table_installed_count();
    }
#else
    (void)option;
#endif
    return 0;
}

const char *option_string_value_name(pathime_option_t option, size_t index)
{
#if PATHIME_WITH_TABLE
    if (option == PATHIME_OPT_TABLE_FILE) {
        return table_installed_name(index);
    }
#else
    (void)option;
    (void)index;
#endif
    return "";
}

int64_t resolve_option_number(const pathime_engine_t *engine,
                              const pathime_context_t *ctx,
                              pathime_option_t option)
{
    const OptionDescriptor *desc = option_descriptor(option);
    if (desc == nullptr) {
        return 0;
    }
    if (engine == nullptr && ctx != nullptr) {
        engine = ctx->engine;
    }
    if (engine == nullptr) {
        return desc->default_value;
    }
    return resolve_number(engine, ctx, desc, option);
}

pathime_str_t resolve_option_string(const pathime_engine_t *engine,
                                    const pathime_context_t *ctx,
                                    pathime_option_t option)
{
    pathime_str_t empty;
    empty.bytes = "";
    empty.len = 0;

    const OptionDescriptor *desc = option_descriptor(option);
    if (desc == nullptr || desc->type != PATHIME_OPTION_STRING) {
        return empty;
    }
    if (engine == nullptr && ctx != nullptr) {
        engine = ctx->engine;
    }
    if (engine == nullptr) {
        empty.bytes = desc->default_string;
        empty.len = std::strlen(desc->default_string);
        return empty;
    }
    return resolve_string(engine, ctx, desc, option);
}

}  // namespace pathime

/* ===========================================================================
 * Public entry points
 *
 * C linkage comes from the public header's extern "C" block, so these are
 * defined at file scope with no wrapper and no namespace of their own.
 * ======================================================================== */

size_t pathime_option_count(void)
{
    /* Deliberately no initialization check: the header promises a static table
     * lookup usable before pathime_init(), which is what lets a client build its
     * settings interface before it has decided to start the library. */
    return pathime::kOptionCount;
}

const char *pathime_option_name(pathime_option_t option)
{
    const pathime::OptionDescriptor *desc = pathime::option_descriptor(option);
    return (desc != nullptr) ? desc->name : "";
}

const char *pathime_option_value_name(pathime_option_t option, int64_t value)
{
    /* Static table lookup, so no initialization check — the same promise
     * pathime_option_name() makes, for the same reason: a client builds its
     * settings interface before it decides to start the library. */
    const pathime::OptionDescriptor *desc = pathime::option_descriptor(option);
    if (desc == nullptr) {
        return "";
    }

    /*
     * The one type whose values are not a static table. A string option
     * enumerates what the installation holds, so `value` is an index rather
     * than an enumerator or a bit, and the answer is "" until pathime_init()
     * has looked. That is the header's promise, stated there in as many words.
     */
    if (desc->type == PATHIME_OPTION_STRING) {
        if (value < 0) {
            return "";
        }
        return pathime::option_string_value_name(option,
                                                 static_cast<size_t>(value));
    }

    /*
     * Turn the argument into a bit position, which is how both kinds of row are
     * indexed. An ENUM value is already one. A FLAGS value is a single bit and
     * must be exactly that: zero names nothing, and a combination has no single
     * name to give, so both fall out here rather than being resolved to the
     * lowest bit set — silently naming one of several bits would be worse than
     * saying nothing.
     */
    size_t index = 0;
    if (desc->type == PATHIME_OPTION_ENUM) {
        if (value < 0 || static_cast<uint64_t>(value) >= pathime::kMaxValueNames) {
            return "";
        }
        index = static_cast<size_t>(value);
    } else if (desc->type == PATHIME_OPTION_FLAGS) {
        const uint64_t bit = static_cast<uint64_t>(value);
        if (bit == 0 || (bit & (bit - 1)) != 0) {
            return "";
        }
        while ((bit >> index) != 1) {
            index++;
        }
        if (index >= pathime::kMaxValueNames) {
            return "";
        }
    } else {
        /* BOOL, INT and STRING have no values worth naming. */
        return "";
    }

    /*
     * The descriptor is what decides whether the value exists at all, so an
     * enumerator this option does not define is "" even if the row happens to
     * carry a name at that position. valid_values is unnarrowed here — this
     * function takes no engine, and the name of a value does not depend on who
     * implements it — so an engine that does not honour a bit still gets the
     * name for it, which is what lets a client label an option it has narrowed
     * for itself.
     */
    if ((desc->valid_values & (UINT64_C(1) << index)) == 0) {
        return "";
    }

    const pathime::OptionValueNames *row = pathime::value_names_row(option);
    if (row == nullptr || row->names[index] == nullptr) {
        return "";
    }
    return row->names[index];
}

pathime_status_t pathime_engine_option_info(const pathime_engine_t *engine,
                                            pathime_option_t option,
                                            pathime_option_info_t *out_info)
{
    if (engine == nullptr || out_info == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    /*
     * The in-and-out struct_size protocol, in full. The caller has set it to the
     * size it allocated; we may write at most that many bytes, and at most as
     * many as we know how to fill.
     *
     * pathime_option_info_t has exactly one layout, so the only recognized
     * sizes are that layout and anything larger — a caller compiled against a
     * newer header, which we serve by writing what we know and telling it so.
     * Anything smaller matches no layout of the struct at all:
     * PATHIME_ERROR_INVALID_ARGUMENT with nothing written. When a second layout
     * is added, this becomes a lookup against the list of accepted prefix
     * sizes, and `writable` starts differing from sizeof().
     */
    if (out_info->struct_size < sizeof(pathime_option_info_t)) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }
    const size_t writable = sizeof(pathime_option_info_t);

    const pathime::OptionDescriptor *desc = pathime::option_descriptor(option);
    if (desc == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    if (!pathime::initialized()) {
        return PATHIME_ERROR_NOT_INITIALIZED;
    }

    /*
     * An option this engine does not implement is reported through `supported`,
     * not as an error — that is the whole point of the query. The header calls
     * the remaining members unspecified in that case; filling them from the
     * descriptor anyway costs nothing and is friendlier than leaving whatever
     * the caller's allocation happened to contain.
     */
    pathime_option_info_t staged = {};
    staged.struct_size = writable;
    staged.type = desc->type;
    staged.supported = pathime::option_supported(option, engine->id);
    staged.resets_composition = desc->resets_composition;
    staged.default_value = desc->default_value;
    staged.min_value = desc->min_value;
    staged.max_value = desc->max_value;
    staged.valid_values = pathime::option_valid_values(option, engine->id);
    staged.default_string.bytes = desc->default_string;
    staged.default_string.len = std::strlen(desc->default_string);

    /*
     * Only for an engine that implements the option, unlike every other member
     * above. Those are descriptor facts and are true whoever asks; this one is
     * a claim about what a client may *do*, and offering the installed tables
     * as the legal values of an option Hangul does not have would be an
     * invitation to a call that fails.
     */
    staged.valid_value_count = staged.supported
                                   ? pathime::option_string_value_count(option)
                                   : 0;

    /* Staged and copied rather than written member by member so that a partial
     * write, once there is an older layout to serve, is one line and not a
     * second list of assignments that can fall out of step with this one. */
    std::memcpy(out_info, &staged, writable);

    return PATHIME_OK;
}

/* ---- Setting ---------------------------------------------------------- */

pathime_status_t pathime_engine_set_option_bool(pathime_engine_t *engine,
                                                pathime_option_t option,
                                                bool value)
{
    return pathime::set_number(engine, nullptr, option, PATHIME_OPTION_BOOL, value ? 1 : 0);
}

pathime_status_t pathime_engine_set_option_int(pathime_engine_t *engine,
                                               pathime_option_t option,
                                               int64_t value)
{
    return pathime::set_number(engine, nullptr, option, PATHIME_OPTION_INT, value);
}

pathime_status_t pathime_engine_set_option_string(pathime_engine_t *engine,
                                                  pathime_option_t option,
                                                  const char *value)
{
    return pathime::set_string(engine, nullptr, option, value);
}

pathime_status_t pathime_context_set_option_bool(pathime_context_t *ctx,
                                                 pathime_option_t option,
                                                 bool value)
{
    return pathime::set_number(nullptr, ctx, option, PATHIME_OPTION_BOOL, value ? 1 : 0);
}

pathime_status_t pathime_context_set_option_int(pathime_context_t *ctx,
                                                pathime_option_t option,
                                                int64_t value)
{
    return pathime::set_number(nullptr, ctx, option, PATHIME_OPTION_INT, value);
}

pathime_status_t pathime_context_set_option_string(pathime_context_t *ctx,
                                                   pathime_option_t option,
                                                   const char *value)
{
    return pathime::set_string(nullptr, ctx, option, value);
}

pathime_status_t pathime_engine_reset_option(pathime_engine_t *engine,
                                             pathime_option_t option)
{
    return pathime::reset_option(engine, nullptr, option);
}

pathime_status_t pathime_context_reset_option(pathime_context_t *ctx,
                                              pathime_option_t option)
{
    return pathime::reset_option(nullptr, ctx, option);
}

pathime_status_t pathime_context_isolate_options(pathime_context_t *ctx)
{
    return pathime::isolate_options(ctx);
}

/* ---- Reading ---------------------------------------------------------- */

pathime_status_t pathime_engine_get_option_bool(const pathime_engine_t *engine,
                                                pathime_option_t option,
                                                bool *out_value)
{
    /* The shared body checks its own out-parameter, which is the local below;
     * the caller's is this wrapper's to check. */
    if (out_value == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    int64_t number = 0;
    const pathime_status_t status =
        pathime::get_number(engine, nullptr, option, PATHIME_OPTION_BOOL, &number);
    if (status != PATHIME_OK) {
        return status;
    }
    *out_value = (number != 0);
    return PATHIME_OK;
}

pathime_status_t pathime_engine_get_option_int(const pathime_engine_t *engine,
                                               pathime_option_t option,
                                               int64_t *out_value)
{
    return pathime::get_number(engine, nullptr, option, PATHIME_OPTION_INT, out_value);
}

pathime_status_t pathime_engine_get_option_string(const pathime_engine_t *engine,
                                                  pathime_option_t option,
                                                  pathime_str_t *out_value)
{
    return pathime::get_string(engine, nullptr, option, out_value);
}

pathime_status_t pathime_context_get_option_bool(const pathime_context_t *ctx,
                                                 pathime_option_t option,
                                                 bool *out_value)
{
    if (out_value == nullptr) {
        return PATHIME_ERROR_INVALID_ARGUMENT;
    }

    int64_t number = 0;
    const pathime_status_t status =
        pathime::get_number(nullptr, ctx, option, PATHIME_OPTION_BOOL, &number);
    if (status != PATHIME_OK) {
        return status;
    }
    *out_value = (number != 0);
    return PATHIME_OK;
}

pathime_status_t pathime_context_get_option_int(const pathime_context_t *ctx,
                                                pathime_option_t option,
                                                int64_t *out_value)
{
    return pathime::get_number(nullptr, ctx, option, PATHIME_OPTION_INT, out_value);
}

pathime_status_t pathime_context_get_option_string(const pathime_context_t *ctx,
                                                   pathime_option_t option,
                                                   pathime_str_t *out_value)
{
    return pathime::get_string(nullptr, ctx, option, out_value);
}

bool pathime_engine_option_is_set(const pathime_engine_t *engine, pathime_option_t option)
{
    return pathime::option_is_set(engine, nullptr, option);
}

bool pathime_context_option_is_set(const pathime_context_t *ctx, pathime_option_t option)
{
    return pathime::option_is_set(nullptr, ctx, option);
}
