/*
 * Implementation of the options machinery declared in options.h, plus the
 * nineteen public entry points of the header's Options section.
 *
 * Everything here is core code: no backend is named, nothing crosses
 * backend.h, and the file compiles identically whatever the PATHIME_WITH_*
 * macros say. That is why it is real rather than stubbed — the descriptor
 * table, the two-level store, the validation order and the resolution order
 * are all decidable from include/pathime/pathime.h alone. The two things that
 * genuinely need an adapter — pushing a resolved value at the backend, and
 * tier 3, the table's own declaration — are marked TODO(impl) at the exact
 * point they slot in.
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

#include "context.h"
#include "engine.h"
#include "init.h"

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
 * syllables from jamo and produces no candidates at all now that hanja is out
 * of scope (TODO.md §1, "Cut in the API review round"), so the options that
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

    make_bool("learning", kConverting, true),
    make_enum("latin-width", kConverting, PATHIME_WIDTH_HALF,
              enum_mask(PATHIME_WIDTH_FULL + 1)),
    make_enum("punctuation-width", kConverting, PATHIME_WIDTH_FULL,
              enum_mask(PATHIME_WIDTH_FULL + 1)),
    make_enum("chinese-variant", kChinese, PATHIME_CHINESE_SIMPLIFIED_ONLY,
              enum_mask(PATHIME_CHINESE_ANY + 1)),
    make_bool("prediction", kAnthy | kTable, false),
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
     * Both FLAGS options default to every bit. Scoped to Pinyin on reasoning
     * that was not traced all the way through pyzy's bopomofo-to-pinyin tables
     * (TODO.md §1, "One claim to re-check"); widening `engines` later is
     * additive and harmless, so the narrower claim is the safe one to ship.
     */
    make_flags("pinyin-fuzzy", kPinyin,
               static_cast<int64_t>(flags_mask(PATHIME_PINYIN_FUZZY_ING_IN)),
               flags_mask(PATHIME_PINYIN_FUZZY_ING_IN)),
    make_flags("pinyin-correction", kPinyin,
               static_cast<int64_t>(flags_mask(PATHIME_PINYIN_CORRECT_ON_ONG)),
               flags_mask(PATHIME_PINYIN_CORRECT_ON_ONG)),
    make_bool("pinyin-show-raw", kPinyin, false),

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
static_assert(kEngineCount <= 32, "engine ids no longer fit OptionDescriptor::engines");

/* ---------------------------------------------------------------------------
 * UTF-8
 *
 * TODO(impl): this belongs in utf8.cc. src/utf8.h is deliberately still empty —
 * its types wait on the encoding-boundary design (docs/source-layout.md,
 * Finding 4) — and adding a signature to it now would pre-empt that round for
 * the sake of one option check. Move this there when utf8.h lands, and delete
 * it from here; there should be exactly one UTF-8 scanner in the library.
 * ------------------------------------------------------------------------- */

/**
 * Validate a NUL-terminated UTF-8 string and count its scalar values.
 *
 * Strict: overlong encodings, surrogates and anything above U+10FFFF are
 * rejected, because an option string is stored, handed back to the client, and
 * in the table engine's case compared against table data. NUL cannot occur
 * mid-string here — the value is NUL-terminated, which is the form the header
 * uses for "short discrete values that name something".
 */
bool utf8_scan(const char *text, size_t *out_scalars)
{
    static const uint32_t kLowest[5] = {0, 0, 0x80, 0x800, 0x10000};

    const unsigned char *p = reinterpret_cast<const unsigned char *>(text);
    size_t scalars = 0;

    while (*p != 0) {
        const unsigned char lead = *p;
        size_t len;
        uint32_t cp;

        if (lead < 0x80) {
            len = 1;
            cp = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            len = 2;
            cp = lead & 0x1Fu;
        } else if ((lead & 0xF0) == 0xE0) {
            len = 3;
            cp = lead & 0x0Fu;
        } else if ((lead & 0xF8) == 0xF0) {
            len = 4;
            cp = lead & 0x07u;
        } else {
            return false;  /* continuation byte or 5-byte lead */
        }

        /* A NUL inside the sequence fails the continuation test, so this never
         * reads past the terminator. */
        for (size_t i = 1; i < len; i++) {
            if ((p[i] & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (p[i] & 0x3Fu);
        }

        if (cp < kLowest[len] || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
            return false;
        }

        p += len;
        scalars++;
    }

    *out_scalars = scalars;
    return true;
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
     * TODO(impl): tier 3 slots in exactly here — the value the effective table
     * declares, for PATHIME_ENGINE_TABLE only. The effective table is whatever
     * PATHIME_OPT_TABLE_FILE resolves to at this same level (the context's if it
     * names one, otherwise the engine's), so this is a lookup in the compiled
     * table's header fields, not another store. It sits below both client levels
     * and above the descriptor default, so a client that set nothing gets what
     * the table author intended. Nothing to consult until the engine of
     * docs/ibus-table-spec.md exists (TODO.md §4).
     */

    return nullptr;  /* tier 4: the caller applies the descriptor default */
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
    int64_t value = (v != nullptr) ? v->number : desc->default_value;

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

    if (const OptionValue *v = resolve_value(engine, ctx, option)) {
        out.bytes = v->text.c_str();
        out.len = v->text.size();
    } else {
        out.bytes = desc->default_string;
        out.len = std::strlen(desc->default_string);
    }

    return out;
}

/* ---------------------------------------------------------------------------
 * Entry-point prologues
 *
 * The shared validation order, in one place so the nineteen entry points cannot
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
pathime_status_t commit_change(pathime_engine_t *engine,
                               pathime_context_t *ctx,
                               const OptionDescriptor *desc,
                               pathime_option_t option)
{
    /*
     * TODO(impl): apply the resolved value to the backend. Nothing crosses
     * backend.h yet, so a set stores the value and stops — the descriptor, the
     * store and the getters are all correct, and only the engine's behaviour
     * does not follow yet. This becomes a call into the adapter's option hook
     * (one per affected context for a per-context setting such as the hangul
     * combination flags or anthy's typing method, once on the engine's own
     * backend state for the shared ones), taking the value resolve_number() /
     * resolve_string() reports for that context. Its signature waits on
     * backend.h (TODO.md §3, question 1).
     *
     * TODO(impl): PATHIME_OPT_TABLE_FILE additionally resolves and compiles the
     * named table here, sharing the compiled result across every context naming
     * the same path, and it is where tier 3 becomes readable.
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
     * TODO(impl): PATHIME_OPT_TABLE_PINYIN_FALLBACK is PATHIME_ERROR_UNSUPPORTED
     * when the resolved table was not compiled with pinyin data — the last
     * rejection before the store, since it depends on the resolved table rather
     * than on the value. Unreachable until tables exist: PATHIME_WITH_TABLE is 0
     * in every build (TODO.md §4), so no engine implementing this option can be
     * created yet.
     */

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
     * (docs/pyzy-options.md), so the two engines it supplies accept only the two
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
    if (!utf8_scan(value, &scalars)) {
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
     * pathime_option_info_t has had exactly one layout so far, so the only
     * recognized sizes are that layout and anything larger — a caller compiled
     * against a newer header, which we serve by writing what we know and telling
     * it so. Anything smaller is a size no shipped header ever had:
     * PATHIME_ERROR_INVALID_ARGUMENT with nothing written. When a second layout
     * ships, this becomes a lookup against the list of prefix sizes that have
     * been released, and `writable` starts differing from sizeof().
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
