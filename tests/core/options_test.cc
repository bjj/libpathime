/*
 * src/options.cc — the descriptor table, the two-level store, and the
 * validation and resolution orders behind the header's Options section.
 *
 * This suite exists because the public API cannot reach any of it yet. No
 * engine adapter is written, so pathime_engine_create() always answers
 * PATHIME_ERROR_UNKNOWN_ENGINE and a C client can never hold an engine handle —
 * which puts pathime_engine_option_info(), every setter and getter, and the
 * whole of the two-level resolution out of reach from tests/api. src/engine.h
 * and src/context.h define `struct pathime_engine` and `struct pathime_context`
 * as ordinary C++ aggregates, so a test compiled against the internal sources
 * can build those handles itself and then drive the *public* entry points
 * against them. Everything below therefore exercises shipped, exported
 * behaviour; only the way the handle was obtained is internal.
 *
 * Two rules follow from building handles by hand, and both are load-bearing:
 *
 *   - The handles are automatic objects. pathime_engine_destroy() and
 *     pathime_context_destroy() `delete` their argument, so they are never
 *     called here. A context registers itself in engine.contexts, which is what
 *     makes the engine-level broadcast real, and unregisters nothing — so every
 *     engine below outlives the contexts pointing at it, which is the same
 *     obligation the public contract places on a client.
 *   - The wiring must match what pathime_context_create() produces, or a test
 *     proves something about a shape the library never sees. wire_context()
 *     mirrors it member for member; see src/context.cc.
 *
 * The descriptor expectations in kExpected are transcribed from the doc
 * comments in include/pathime/pathime.h — the option's stated type, default,
 * bounds, legal values and engine list — and deliberately not from
 * src/options.cc's own table, which would only prove that table equals itself.
 * Where the header states something the descriptor must then choose a
 * representation for (max-candidates has "minimum 1" and no maximum), the
 * choice is named at the row.
 *
 * Rejection cases are tested at least as hard as acceptance ones, on the model
 * of tests/core/utf8_test.cc: an option value accepted where the descriptor
 * forbids it does not announce itself, it just quietly configures an engine to
 * do something the client never asked for.
 */

#include "core_test_util.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <pathime/pathime.h>

#include "context.h"
#include "engine.h"

/*
 * core_test_util.h carries no status-code macro — its only other subject,
 * src/utf8.h, answers in bools — so this is the one from
 * tests/api/api_test_util.h, which reports both sides by name rather than by
 * number.
 */
#define PT_CHECK_STATUS(expr, expected)                                        \
    do {                                                                       \
        const pathime_status_t pt_got_ = (expr);                               \
        const pathime_status_t pt_want_ = (expected);                          \
        pt_checks++;                                                           \
        if (pt_got_ != pt_want_)                                               \
            PT_FAILF("%s: got \"%s\", expected \"%s\"", #expr,                 \
                     pathime_status_string(pt_got_),                           \
                     pathime_status_string(pt_want_));                         \
    } while (0)

/* Compare two int64_t values, reporting both sides. */
#define PT_CHECK_I64(expr, expected)                                           \
    do {                                                                       \
        const int64_t pt_got_ = (expr);                                        \
        const int64_t pt_want_ = (expected);                                   \
        pt_checks++;                                                           \
        if (pt_got_ != pt_want_)                                               \
            PT_FAILF("%s: got %lld, expected %lld", #expr,                     \
                     static_cast<long long>(pt_got_),                          \
                     static_cast<long long>(pt_want_));                        \
    } while (0)

/* A check that carries its own message; for loops where #cond names indices
 * rather than the option that failed. */
#define PT_CHECK_MSG(cond, ...)                                                \
    do {                                                                       \
        pt_checks++;                                                           \
        if (!(cond))                                                           \
            PT_FAILF(__VA_ARGS__);                                             \
    } while (0)

namespace {

/* ---------------------------------------------------------------------------
 * Handles
 * ------------------------------------------------------------------------- */

/** What a test client remembers about the callbacks it was handed. */
struct ClientState {
    int changes = 0;                            /**< composition_changed count. */
    const pathime_composition_t *last = nullptr;
};

/**
 * A borrowed slice as a std::string, tolerating the null pointer a *rejected*
 * getter leaves in an untouched out-parameter. Without that tolerance one
 * failure early in the run would abort the process inside std::string and the
 * remaining checks would report nothing at all.
 */
std::string as_string(const pathime_str_t &text)
{
    return (text.bytes != nullptr) ? std::string(text.bytes, text.len)
                                   : std::string("<null>");
}

void on_commit_text(void *, pathime_str_t)
{
    /* Required of every client, never invoked by anything below: nothing here
     * mutates a composition. Present because pathime_context_create() rejects a
     * client without it, so a hand-built context that omitted it would not be
     * the shape the library ever sees. */
}

void on_delete_surrounding_text(void *, ptrdiff_t, size_t)
{
    /* Its presence, not its behaviour, is what the Hangul capping rule turns
     * on — see test_hangul_capping(). */
}

void on_composition_changed(void *user_data, const pathime_composition_t *composition)
{
    ClientState *state = static_cast<ClientState *>(user_data);
    state->changes++;
    state->last = composition;
}

/**
 * Build a context exactly as pathime_context_create() would: engine, user data,
 * the three resolved callback pointers, and registration in engine.contexts.
 *
 * @param with_delete Whether the client supplies delete_surrounding_text. A
 *                    NULL member is how a client declares it cannot do
 *                    something, and it is the input to the one capping rule in
 *                    the API.
 * @param with_changed Whether it supplies composition_changed. Optional by the
 *                    header — "a client with no way to display composition
 *                    state may omit it" — and the broadcast must skip it rather
 *                    than call through a null pointer.
 */
void wire_context(pathime_context &ctx, pathime_engine &engine, ClientState &state,
                  bool with_delete, bool with_changed = true)
{
    ctx.engine = &engine;
    ctx.user_data = &state;
    ctx.commit_text = on_commit_text;
    ctx.delete_surrounding_text = with_delete ? on_delete_surrounding_text : nullptr;
    ctx.composition_changed = with_changed ? on_composition_changed : nullptr;
    engine.contexts.push_back(&ctx);

    /* The last thing pathime_context_create() does: publish the flat
     * composition value, so its zero-length slices point at "" rather than at
     * nullptr. With nothing changed this dispatches no callback. */
    pathime::refresh_composition(&ctx, false);
    state.changes = 0;
}

/* ---------------------------------------------------------------------------
 * Expectations, transcribed from include/pathime/pathime.h
 * ------------------------------------------------------------------------- */

constexpr size_t kEngineCount = static_cast<size_t>(PATHIME_ENGINE_TABLE) + 1;

/* Engine bits in pathime_engine_id_t order. Written out here rather than taken
 * from src/options.h's engine_bit() so the transcription below owes the
 * implementation nothing. */
constexpr unsigned kH = 1u << PATHIME_ENGINE_HANGUL;
constexpr unsigned kA = 1u << PATHIME_ENGINE_ANTHY;
constexpr unsigned kP = 1u << PATHIME_ENGINE_PINYIN;
constexpr unsigned kB = 1u << PATHIME_ENGINE_BOPOMOFO;
constexpr unsigned kT = 1u << PATHIME_ENGINE_TABLE;

/** ENUM valid_values: bit i set for every value from 0 through @a last. */
constexpr uint64_t values_upto(int last)
{
    return (UINT64_C(1) << (last + 1)) - 1;
}

/** ENUM valid_values: the single bit for one enumerator. */
constexpr uint64_t value_bit(int value)
{
    return UINT64_C(1) << value;
}

/** FLAGS valid_values: every bit from 0 through @a highest inclusive. */
constexpr uint64_t bits_upto(uint64_t highest)
{
    return (highest << 1) - 1;
}

struct Expected {
    pathime_option_t option;
    const char *name;
    pathime_option_type_t type;
    unsigned engines;            /**< The engines the doc comment names. */
    bool resets;                 /**< "Resets the composition." */
    int64_t default_value;       /**< BOOL/INT/ENUM/FLAGS. */
    int64_t min_value;           /**< INT only. */
    int64_t max_value;           /**< INT only. */
    uint64_t valid_values;       /**< ENUM/FLAGS only; the widest engine's set. */
    const char *default_string;  /**< STRING only. */
};

/*
 * One row per option, in enum order. Each carries what its doc comment in the
 * header states and nothing more: members the header calls unused for a type
 * are left zero and are not checked, because checking them would pin down a
 * value the header never promised.
 */
const Expected kExpected[] = {
    /* ---- Common -------------------------------------------------------- */

    /* "INT, default PATHIME_DEFAULT_MAX_CANDIDATES, minimum 1. Anthy, Pinyin,
     * Bopomofo, Table." No maximum is stated, so the descriptor must offer the
     * widest one an int64_t has: the option is a ceiling on how hard a lazy
     * backend is pumped, not an allocation. Hangul is absent from the list and
     * that absence is the whole point of the option's last paragraph — it
     * produces no candidates at all. */
    {PATHIME_OPT_MAX_CANDIDATES, "max-candidates", PATHIME_OPTION_INT,
     kA | kP | kB | kT, false, PATHIME_DEFAULT_MAX_CANDIDATES, 1, INT64_MAX, 0, ""},

    /* "BOOL, default true. Anthy, Table." Pinyin and Bopomofo are absent, and
     * the absence is deliberate rather than an oversight in the engine list:
     * pyzy learns inside its selection and commit calls with no switch to
     * withhold, and its learned data is process-global while this option is
     * per-context. The option's last paragraph says so. */
    {PATHIME_OPT_LEARNING, "learning", PATHIME_OPTION_BOOL,
     kA | kT, false, 1, 0, 0, 0, ""},

    /* "ENUM of pathime_width_t, default PATHIME_WIDTH_HALF. Anthy, Pinyin,
     * Bopomofo, Table." */
    {PATHIME_OPT_LATIN_WIDTH, "latin-width", PATHIME_OPTION_ENUM,
     kA | kP | kB | kT, false, PATHIME_WIDTH_HALF, 0, 0,
     values_upto(PATHIME_WIDTH_FULL), ""},

    /* "ENUM of pathime_width_t, default PATHIME_WIDTH_FULL." Same engines. The
     * two width options differ only in their default, and that pair — full
     * punctuation, half digits — is the combination every reference engine
     * models independently for. */
    {PATHIME_OPT_PUNCTUATION_WIDTH, "punctuation-width", PATHIME_OPTION_ENUM,
     kA | kP | kB | kT, false, PATHIME_WIDTH_FULL, 0, 0,
     values_upto(PATHIME_WIDTH_FULL), ""},

    /* "ENUM of pathime_chinese_variant_t, default
     * PATHIME_CHINESE_SIMPLIFIED_ONLY. Pinyin, Bopomofo, Table." All five
     * values on Table; the narrowing to the two exclusive ones on Pinyin and
     * Bopomofo is applied in the loop, since it is per engine rather than per
     * option. */
    {PATHIME_OPT_CHINESE_VARIANT, "chinese-variant", PATHIME_OPTION_ENUM,
     kP | kB | kT, false, PATHIME_CHINESE_SIMPLIFIED_ONLY, 0, 0,
     values_upto(PATHIME_CHINESE_ANY), ""},

    /* "BOOL, default false. Anthy, Table." */
    {PATHIME_OPT_PREDICTION, "prediction", PATHIME_OPTION_BOOL,
     kA | kT, false, 0, 0, 0, 0, ""},

    /* "BOOL, default true. Pinyin, Bopomofo." */
    {PATHIME_OPT_SPECIAL_PHRASES, "special-phrases", PATHIME_OPTION_BOOL,
     kP | kB, false, 1, 0, 0, 0, ""},

    /* "BOOL, default true. Pinyin, Bopomofo, Table." */
    {PATHIME_OPT_INCOMPLETE_INPUT, "incomplete-input", PATHIME_OPTION_BOOL,
     kP | kB | kT, false, 1, 0, 0, 0, ""},

    /* ---- Hangul. Everything under this heading is Hangul-only. --------- */

    /* "ENUM of pathime_hangul_layout_t, default PATHIME_HANGUL_LAYOUT_2SET." */
    {PATHIME_OPT_HANGUL_LAYOUT, "hangul-layout", PATHIME_OPTION_ENUM,
     kH, false, PATHIME_HANGUL_LAYOUT_2SET, 0, 0,
     values_upto(PATHIME_HANGUL_LAYOUT_AHNMATAE), ""},

    /* "BOOL, default false." */
    {PATHIME_OPT_HANGUL_AUTO_REORDER, "hangul-auto-reorder", PATHIME_OPTION_BOOL,
     kH, false, 0, 0, 0, 0, ""},

    /* "BOOL, default false." */
    {PATHIME_OPT_HANGUL_DOUBLE_STROKE_COMBINE, "hangul-double-stroke-combine",
     PATHIME_OPTION_BOOL, kH, false, 0, 0, 0, 0, ""},

    /* "BOOL, default true." */
    {PATHIME_OPT_HANGUL_NON_CHOSEONG_COMBINE, "hangul-non-choseong-combine",
     PATHIME_OPTION_BOOL, kH, false, 1, 0, 0, 0, ""},

    /* "ENUM of pathime_hangul_preedit_t, default
     * PATHIME_HANGUL_PREEDIT_SYLLABLE." All three values are legal; what
     * PATHIME_HANGUL_PREEDIT_NONE additionally needs from a client is a
     * capability question, not a legality one — see test_hangul_capping(). */
    {PATHIME_OPT_HANGUL_PREEDIT, "hangul-preedit", PATHIME_OPTION_ENUM,
     kH, false, PATHIME_HANGUL_PREEDIT_SYLLABLE, 0, 0,
     values_upto(PATHIME_HANGUL_PREEDIT_NONE), ""},

    /* ---- Anthy. Everything under this heading is Anthy-only. ----------- */

    /* "ENUM of pathime_anthy_typing_t, default PATHIME_ANTHY_TYPING_ROMAJI.
     * Resets the composition." One of the four that do. */
    {PATHIME_OPT_ANTHY_TYPING_METHOD, "anthy-typing-method", PATHIME_OPTION_ENUM,
     kA, true, PATHIME_ANTHY_TYPING_ROMAJI, 0, 0,
     values_upto(PATHIME_ANTHY_TYPING_KANA), ""},

    /* "ENUM of pathime_anthy_script_t, default PATHIME_ANTHY_SCRIPT_HIRAGANA." */
    {PATHIME_OPT_ANTHY_KANA_SCRIPT, "anthy-kana-script", PATHIME_OPTION_ENUM,
     kA, false, PATHIME_ANTHY_SCRIPT_HIRAGANA, 0, 0,
     values_upto(PATHIME_ANTHY_SCRIPT_HALFWIDTH_KATAKANA), ""},

    /* "ENUM of pathime_anthy_period_t, default PATHIME_ANTHY_PERIOD_KUTEN." */
    {PATHIME_OPT_ANTHY_PERIOD_STYLE, "anthy-period-style", PATHIME_OPTION_ENUM,
     kA, false, PATHIME_ANTHY_PERIOD_KUTEN, 0, 0,
     values_upto(PATHIME_ANTHY_PERIOD_FULLWIDTH), ""},

    /* "ENUM of pathime_anthy_symbol_t, default
     * PATHIME_ANTHY_SYMBOL_CORNER_SLASH." Four values: the four combinations. */
    {PATHIME_OPT_ANTHY_SYMBOL_STYLE, "anthy-symbol-style", PATHIME_OPTION_ENUM,
     kA, false, PATHIME_ANTHY_SYMBOL_CORNER_SLASH, 0, 0,
     values_upto(PATHIME_ANTHY_SYMBOL_BRACKET_MIDDOT), ""},

    /* "ENUM of pathime_anthy_on_period_t, default
     * PATHIME_ANTHY_ON_PERIOD_NOTHING." */
    {PATHIME_OPT_ANTHY_ON_PERIOD, "anthy-on-period", PATHIME_OPTION_ENUM,
     kA, false, PATHIME_ANTHY_ON_PERIOD_NOTHING, 0, 0,
     values_upto(PATHIME_ANTHY_ON_PERIOD_COMMIT), ""},

    /* "BOOL, default true." */
    {PATHIME_OPT_ANTHY_LATIN_WITH_SHIFT, "anthy-latin-with-shift",
     PATHIME_OPTION_BOOL, kA, false, 1, 0, 0, 0, ""},

    /* ---- Pinyin. Everything under this heading is Pinyin-only, the two
     * FLAGS options included: they sit in the Pinyin section, and the header
     * scopes them there. ------------------------------------------------- */

    /* "ENUM of pathime_pinyin_scheme_t, default PATHIME_PINYIN_SCHEME_FULL.
     * Resets the composition." */
    {PATHIME_OPT_PINYIN_SCHEME, "pinyin-scheme", PATHIME_OPTION_ENUM,
     kP, true, PATHIME_PINYIN_SCHEME_FULL, 0, 0,
     values_upto(PATHIME_PINYIN_SCHEME_DOUBLE_XHE), ""},

    /*
     * "FLAGS of PATHIME_PINYIN_FUZZY_*, default every bit." Twenty bits,
     * contiguous from bit 0, the last being PATHIME_PINYIN_FUZZY_ING_IN.
     *
     * Bopomofo as well as Pinyin, and this is the one row whose engine set the
     * header's section heading no longer gives correctly. Unlike the unprefixed
     * options, this one's doc comment names no engines at all, so the heading is
     * all a reader has — and src/options.cc has since widened it to both ids the
     * pyzy backend supplies, on a trace through pyzy's bopomofo_table showing
     * that 61 of its rows carry a PINYIN_FUZZY_* bit and that check_flags()
     * makes parseBopomofo() stop at a syllable whose bit is clear. The widening
     * is additive and right; the header is what has fallen behind, and its
     * PATHIME_OPT_PINYIN_FUZZY comment wants a "Pinyin, Bopomofo." line of its
     * own so that this stops depending on which file a reader opens.
     *
     * PATHIME_OPT_PINYIN_CORRECTION below is genuinely Pinyin-only, by the same
     * trace: no row of that table reaches a PINYIN_CORRECT_* bit, because a
     * correction is a Latin typing slip and there is no bopomofo spelling to
     * slip in.
     */
    {PATHIME_OPT_PINYIN_FUZZY, "pinyin-fuzzy", PATHIME_OPTION_FLAGS,
     kP | kB, false, static_cast<int64_t>(bits_upto(PATHIME_PINYIN_FUZZY_ING_IN)), 0, 0,
     bits_upto(PATHIME_PINYIN_FUZZY_ING_IN), ""},

    /* "FLAGS of PATHIME_PINYIN_CORRECT_*, default every bit." Eight bits. */
    {PATHIME_OPT_PINYIN_CORRECTION, "pinyin-correction", PATHIME_OPTION_FLAGS,
     kP, false, static_cast<int64_t>(bits_upto(PATHIME_PINYIN_CORRECT_ON_ONG)), 0, 0,
     bits_upto(PATHIME_PINYIN_CORRECT_ON_ONG), ""},

    /* "BOOL, default false." */
    {PATHIME_OPT_PINYIN_SHOW_RAW, "pinyin-show-raw", PATHIME_OPTION_BOOL,
     kP, false, 0, 0, 0, 0, ""},

    /* ---- Bopomofo ------------------------------------------------------ */

    /* "ENUM of pathime_bopomofo_layout_t, default
     * PATHIME_BOPOMOFO_LAYOUT_STANDARD. Resets the composition." */
    {PATHIME_OPT_BOPOMOFO_LAYOUT, "bopomofo-layout", PATHIME_OPTION_ENUM,
     kB, true, PATHIME_BOPOMOFO_LAYOUT_STANDARD, 0, 0,
     values_upto(PATHIME_BOPOMOFO_LAYOUT_IBM), ""},

    /* ---- Table. Everything under this heading is Table-only. ----------- */

    /* "STRING, no default, required. Resets the composition." No default means
     * an empty default_string: reading it with no table resolved yields an
     * empty string, and there is no distinction between unset and empty. */
    {PATHIME_OPT_TABLE_FILE, "table-file", PATHIME_OPTION_STRING,
     kT, true, 0, 0, 0, 0, ""},

    /* "BOOL, default false." */
    {PATHIME_OPT_TABLE_AUTO_COMMIT, "table-auto-commit", PATHIME_OPTION_BOOL,
     kT, false, 0, 0, 0, 0, ""},

    /* "BOOL, default false." */
    {PATHIME_OPT_TABLE_AUTO_SELECT, "table-auto-select", PATHIME_OPTION_BOOL,
     kT, false, 0, 0, 0, 0, ""},

    /* "STRING, default empty. One character, or empty to disable." */
    {PATHIME_OPT_TABLE_SINGLE_WILDCARD, "table-single-wildcard",
     PATHIME_OPTION_STRING, kT, false, 0, 0, 0, 0, ""},

    /* "STRING, default empty. One character, or empty to disable." */
    {PATHIME_OPT_TABLE_MULTI_WILDCARD, "table-multi-wildcard",
     PATHIME_OPTION_STRING, kT, false, 0, 0, 0, 0, ""},

    /* "BOOL, default false." */
    {PATHIME_OPT_TABLE_SINGLE_CHAR_ONLY, "table-single-char-only",
     PATHIME_OPTION_BOOL, kT, false, 0, 0, 0, 0, ""},

    /* "ENUM of pathime_table_invalid_t, default
     * PATHIME_TABLE_INVALID_COMMIT_CANDIDATE." */
    {PATHIME_OPT_TABLE_INVALID_INPUT, "table-invalid-input", PATHIME_OPTION_ENUM,
     kT, false, PATHIME_TABLE_INVALID_COMMIT_CANDIDATE, 0, 0,
     values_upto(PATHIME_TABLE_INVALID_COMMIT_RAW), ""},

    /* "BOOL, default false." */
    {PATHIME_OPT_TABLE_PINYIN_FALLBACK, "table-pinyin-fallback",
     PATHIME_OPTION_BOOL, kT, false, 0, 0, 0, 0, ""}
};

constexpr size_t kExpectedCount = sizeof(kExpected) / sizeof(kExpected[0]);

static_assert(kExpectedCount == static_cast<size_t>(PATHIME_OPT_TABLE_PINYIN_FALLBACK) + 1,
              "an option was added to the header without a row of expectations here");

/** The set of legal ENUM/FLAGS values for @a e as @a id narrows it. */
uint64_t expected_valid_values(const Expected &e, pathime_engine_id_t id)
{
    /*
     * The only narrowing in the inventory, and the header states it outright:
     * "the Pinyin and Bopomofo engines support only the two exclusive ones,
     * because pyzy models this as a single simplified-or-traditional flag with
     * no mixed mode. That difference is reported through valid_values rather
     * than hidden, so a client can present exactly the choices that will work."
     */
    if (e.option == PATHIME_OPT_CHINESE_VARIANT &&
        (id == PATHIME_ENGINE_PINYIN || id == PATHIME_ENGINE_BOPOMOFO)) {
        return value_bit(PATHIME_CHINESE_SIMPLIFIED_ONLY) |
               value_bit(PATHIME_CHINESE_TRADITIONAL_ONLY);
    }
    return e.valid_values;
}

/** The first engine the header says implements @a e. */
pathime_engine_id_t first_supporting_engine(const Expected &e)
{
    for (size_t i = 0; i < kEngineCount; ++i) {
        if ((e.engines & (1u << i)) != 0) {
            return static_cast<pathime_engine_id_t>(i);
        }
    }
    return PATHIME_ENGINE_HANGUL;  /* unreachable: every option has an engine */
}

/* ---------------------------------------------------------------------------
 * Before pathime_init()
 * ------------------------------------------------------------------------- */

void test_before_init()
{
    pathime_engine engine;
    engine.id = PATHIME_ENGINE_ANTHY;

    /*
     * Every option entry point that has an error channel gates on
     * initialization, and the two that do not are the ones the header names as
     * static table lookups. This runs first, before pathime_init(), because it
     * is the only moment in the process where the uninitialized answers can be
     * observed at all.
     */
    pathime_option_info_t info;
    std::memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    PT_CHECK_STATUS(pathime_engine_option_info(&engine, PATHIME_OPT_LEARNING, &info),
                    PATHIME_ERROR_NOT_INITIALIZED);
    /* Nothing was written: struct_size still holds the caller's own value and
     * every other member is still zero. */
    PT_CHECK_SIZE(info.struct_size, sizeof info);
    PT_CHECK(!info.supported);
    PT_CHECK_I64(info.default_value, 0);

    /*
     * Arguments are validated before state. Both of these are wrong in a way
     * the caller can fix without knowing anything about the library's lifetime,
     * so both report the argument rather than the state — the library-wide
     * order, and the only place it is visible from outside is right here.
     */
    info.struct_size = sizeof info - 1;
    PT_CHECK_STATUS(pathime_engine_option_info(&engine, PATHIME_OPT_LEARNING, &info),
                    PATHIME_ERROR_INVALID_ARGUMENT);
    info.struct_size = sizeof info;
    PT_CHECK_STATUS(pathime_engine_option_info(&engine, (pathime_option_t)9999, &info),
                    PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(pathime_engine_option_info(nullptr, PATHIME_OPT_LEARNING, &info),
                    PATHIME_ERROR_INVALID_ARGUMENT);

    int64_t number = -1;
    PT_CHECK_STATUS(
        pathime_engine_get_option_int(&engine, PATHIME_OPT_MAX_CANDIDATES, &number),
        PATHIME_ERROR_NOT_INITIALIZED);
    PT_CHECK_I64(number, -1);  /* out-parameters untouched on every failure */

    bool flag = true;
    PT_CHECK_STATUS(pathime_engine_get_option_bool(&engine, PATHIME_OPT_LEARNING, &flag),
                    PATHIME_ERROR_NOT_INITIALIZED);
    PT_CHECK(flag);

    pathime_str_t text;
    text.bytes = nullptr;
    text.len = 4242;
    pathime_engine table;
    table.id = PATHIME_ENGINE_TABLE;
    PT_CHECK_STATUS(
        pathime_engine_get_option_string(&table, PATHIME_OPT_TABLE_FILE, &text),
        PATHIME_ERROR_NOT_INITIALIZED);
    PT_CHECK_SIZE(text.len, 4242);

    PT_CHECK_STATUS(pathime_engine_set_option_bool(&engine, PATHIME_OPT_LEARNING, false),
                    PATHIME_ERROR_NOT_INITIALIZED);
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&engine, PATHIME_OPT_MAX_CANDIDATES, 5),
        PATHIME_ERROR_NOT_INITIALIZED);
    PT_CHECK_STATUS(
        pathime_engine_set_option_string(&table, PATHIME_OPT_TABLE_FILE, "x.txt"),
        PATHIME_ERROR_NOT_INITIALIZED);
    PT_CHECK_STATUS(pathime_engine_reset_option(&engine, PATHIME_OPT_LEARNING),
                    PATHIME_ERROR_NOT_INITIALIZED);

    /* A rejected set stored nothing, which the store can be asked directly —
     * is_set has no error channel and answers false before init anyway, so both
     * halves of that sentence are exercised at once. */
    PT_CHECK(!pathime_engine_option_is_set(&engine, PATHIME_OPT_LEARNING));

    /* The three documented exceptions work here and answer the same as they
     * will for the rest of the process. They are the static table lookups, and
     * they are what lets a client build its settings interface before it has
     * decided to start the library at all. */
    PT_CHECK_SIZE(pathime_option_count(), kExpectedCount);
    PT_CHECK_STR(pathime_option_name(PATHIME_OPT_CHINESE_VARIANT), "chinese-variant");
    PT_CHECK_STR(pathime_option_value_name(PATHIME_OPT_CHINESE_VARIANT,
                                           PATHIME_CHINESE_ANY),
                 "any");
}

/* ---------------------------------------------------------------------------
 * The descriptor table against the header
 * ------------------------------------------------------------------------- */

void test_descriptor_table()
{
    pathime_engine engines[kEngineCount];
    for (size_t i = 0; i < kEngineCount; ++i) {
        engines[i].id = static_cast<pathime_engine_id_t>(i);
    }

    PT_CHECK_SIZE(pathime_option_count(), kExpectedCount);

    for (size_t row = 0; row < kExpectedCount; ++row) {
        const Expected &e = kExpected[row];

        /* The table is indexed by option id and is dense, which is what
         * pathime_option_count()'s walk rests on. A row out of order here would
         * shift every descriptor after it. */
        PT_CHECK_MSG(static_cast<size_t>(e.option) == row,
                     "expectations row %zu holds option %d, not its own index",
                     row, static_cast<int>(e.option));
        PT_CHECK_STR(pathime_option_name(e.option), e.name);

        for (size_t i = 0; i < kEngineCount; ++i) {
            const pathime_engine_id_t id = static_cast<pathime_engine_id_t>(i);
            const bool want_supported = (e.engines & (1u << i)) != 0;

            pathime_option_info_t info;
            std::memset(&info, 0xAB, sizeof info);
            info.struct_size = sizeof info;

            /*
             * "Fails with PATHIME_ERROR_INVALID_ARGUMENT only for an
             * unrecognized option or struct_size; an option the engine does not
             * implement is reported through pathime_option_info_t::supported,
             * not as an error." So every one of these 160 queries succeeds.
             */
            const pathime_status_t status =
                pathime_engine_option_info(&engines[i], e.option, &info);
            PT_CHECK_STATUS(status, PATHIME_OK);
            if (status != PATHIME_OK) {
                /* Nothing was written, so the struct still holds the 0xAB fill.
                 * Reading on would report a hundred consequential failures and,
                 * for the string members, walk a pointer made of filler. */
                continue;
            }
            PT_CHECK_SIZE(info.struct_size, sizeof(pathime_option_info_t));

            PT_CHECK_MSG(info.supported == want_supported,
                         "%s on engine %zu: supported=%d, header says %d",
                         e.name, i, static_cast<int>(info.supported),
                         static_cast<int>(want_supported));

            /* "False if this engine does not implement the option, in which
             * case every other member is unspecified." Reading further would
             * test a promise the header declines to make. */
            if (!want_supported) {
                continue;
            }

            PT_CHECK_MSG(info.type == e.type, "%s on engine %zu: type %d, expected %d",
                         e.name, i, static_cast<int>(info.type),
                         static_cast<int>(e.type));

            PT_CHECK_MSG(info.resets_composition == e.resets,
                         "%s on engine %zu: resets_composition=%d, header says %d",
                         e.name, i, static_cast<int>(info.resets_composition),
                         static_cast<int>(e.resets));

            if (e.type == PATHIME_OPTION_STRING) {
                /* "STRING only: the tier-4 default, empty when the option has
                 * none." Never a null pointer, so a client may read it without
                 * a guard. */
                PT_CHECK_MSG(info.default_string.bytes != nullptr,
                             "%s on engine %zu: null default_string.bytes", e.name, i);
                if (info.default_string.bytes != nullptr) {
                    PT_CHECK_STR(as_string(info.default_string), e.default_string);
                }
            } else {
                PT_CHECK_MSG(info.default_value == e.default_value,
                             "%s on engine %zu: default %lld, header says %lld",
                             e.name, i, static_cast<long long>(info.default_value),
                             static_cast<long long>(e.default_value));
            }

            if (e.type == PATHIME_OPTION_INT) {
                PT_CHECK_MSG(info.min_value == e.min_value,
                             "%s: min %lld, header says %lld", e.name,
                             static_cast<long long>(info.min_value),
                             static_cast<long long>(e.min_value));
                PT_CHECK_MSG(info.max_value == e.max_value,
                             "%s: max %lld, header says %lld", e.name,
                             static_cast<long long>(info.max_value),
                             static_cast<long long>(e.max_value));
            }

            if (e.type == PATHIME_OPTION_ENUM || e.type == PATHIME_OPTION_FLAGS) {
                const uint64_t want = expected_valid_values(e, id);
                PT_CHECK_MSG(info.valid_values == want,
                             "%s on engine %zu: valid_values %llx, expected %llx",
                             e.name, i,
                             static_cast<unsigned long long>(info.valid_values),
                             static_cast<unsigned long long>(want));
            }
        }
    }
}

void test_descriptor_spot_checks()
{
    pathime_engine hangul;
    hangul.id = PATHIME_ENGINE_HANGUL;
    pathime_engine pinyin;
    pinyin.id = PATHIME_ENGINE_PINYIN;
    pathime_engine bopomofo;
    bopomofo.id = PATHIME_ENGINE_BOPOMOFO;
    pathime_engine table;
    table.id = PATHIME_ENGINE_TABLE;

    pathime_option_info_t info;

    /*
     * The three claims the header argues for at length, restated here so a
     * reader of this file does not have to reconstruct them from the loop
     * above.
     *
     * One: max-candidates is unsupported on Hangul, and the reason is not an
     * oversight but the shape of libhangul — it composes syllables from jamo
     * and has nothing to choose between, so a cap there would configure a list
     * that is always empty.
     */
    info.struct_size = sizeof info;
    PT_CHECK_STATUS(
        pathime_engine_option_info(&hangul, PATHIME_OPT_MAX_CANDIDATES, &info),
        PATHIME_OK);
    PT_CHECK(!info.supported);

    /* And supported on all four engines that convert by choosing. */
    for (pathime_engine_id_t id : {PATHIME_ENGINE_ANTHY, PATHIME_ENGINE_PINYIN,
                                   PATHIME_ENGINE_BOPOMOFO, PATHIME_ENGINE_TABLE}) {
        pathime_engine engine;
        engine.id = id;
        info.struct_size = sizeof info;
        PT_CHECK_STATUS(
            pathime_engine_option_info(&engine, PATHIME_OPT_MAX_CANDIDATES, &info),
            PATHIME_OK);
        PT_CHECK(info.supported);
        PT_CHECK_I64(info.default_value, PATHIME_DEFAULT_MAX_CANDIDATES);
        PT_CHECK_I64(info.min_value, 1);
    }

    /*
     * Two: chinese-variant narrows to the two exclusive values on Pinyin and
     * Bopomofo and offers all five on Table. Stated as membership rather than
     * as a mask, because what a client actually does with valid_values is ask
     * "may I offer this value?" for each one it knows.
     */
    for (const pathime_engine *engine : {&pinyin, &bopomofo}) {
        info.struct_size = sizeof info;
        PT_CHECK_STATUS(
            pathime_engine_option_info(engine, PATHIME_OPT_CHINESE_VARIANT, &info),
            PATHIME_OK);
        PT_CHECK(info.supported);
        PT_CHECK((info.valid_values & value_bit(PATHIME_CHINESE_SIMPLIFIED_ONLY)) != 0);
        PT_CHECK((info.valid_values & value_bit(PATHIME_CHINESE_TRADITIONAL_ONLY)) != 0);
        PT_CHECK((info.valid_values & value_bit(PATHIME_CHINESE_SIMPLIFIED_FIRST)) == 0);
        PT_CHECK((info.valid_values & value_bit(PATHIME_CHINESE_TRADITIONAL_FIRST)) == 0);
        PT_CHECK((info.valid_values & value_bit(PATHIME_CHINESE_ANY)) == 0);
    }
    info.struct_size = sizeof info;
    PT_CHECK_STATUS(
        pathime_engine_option_info(&table, PATHIME_OPT_CHINESE_VARIANT, &info),
        PATHIME_OK);
    PT_CHECK(info.supported);
    PT_CHECK(info.valid_values == values_upto(PATHIME_CHINESE_ANY));

    /*
     * Three: exactly four options reset the composition, counted from the
     * library rather than from the expectations above so that the two have to
     * agree. The header names why for each: a pending romaji fragment is
     * meaningless once keys are read as kana, pyzy fixes the scheme and the
     * bopomofo arrangement when its context is created, and a new table changes
     * what the accumulated keys even mean.
     */
    size_t resetting = 0;
    for (const Expected &e : kExpected) {
        pathime_engine engine;
        engine.id = first_supporting_engine(e);
        info.struct_size = sizeof info;
        const pathime_status_t status =
            pathime_engine_option_info(&engine, e.option, &info);
        PT_CHECK_STATUS(status, PATHIME_OK);
        if (status != PATHIME_OK) {
            continue;  /* nothing written; see test_descriptor_table() */
        }
        PT_CHECK(info.supported);
        if (info.resets_composition) {
            resetting++;
        }
    }
    PT_CHECK_SIZE(resetting, 4);
}

/* ---------------------------------------------------------------------------
 * The struct_size in-and-out protocol
 * ------------------------------------------------------------------------- */

void test_struct_size_protocol()
{
    pathime_engine engine;
    engine.id = PATHIME_ENGINE_ANTHY;

    constexpr size_t kSize = sizeof(pathime_option_info_t);
    constexpr size_t kSlack = 64;

    /*
     * pathime_option_info_t::struct_size is the only member of this API's
     * out-structs that is both in and out. The caller says how many bytes it
     * allocated; the library writes at most that many and at most as many as it
     * knows how to fill, then stores what it actually wrote.
     */

    /* Exactly the size this build knows: served, and told so. */
    {
        pathime_option_info_t info;
        std::memset(&info, 0xAB, sizeof info);
        info.struct_size = kSize;
        PT_CHECK_STATUS(pathime_engine_option_info(&engine, PATHIME_OPT_LEARNING, &info),
                        PATHIME_OK);
        PT_CHECK_SIZE(info.struct_size, kSize);
        PT_CHECK(info.supported);
        PT_CHECK_I64(info.default_value, 1);
    }

    /*
     * Smaller than any layout that has shipped. Rejected with nothing written —
     * verified byte for byte, because "nothing written" is the whole guarantee:
     * the caller's struct is smaller than the library's idea of it, so a single
     * stray field would be a buffer overrun rather than a stale value.
     */
    for (size_t bad : {static_cast<size_t>(0), static_cast<size_t>(1), kSize - 1,
                       sizeof(size_t)}) {
        unsigned char buf[kSize];
        unsigned char snapshot[kSize];
        std::memset(buf, 0xAB, sizeof buf);
        pathime_option_info_t *info = reinterpret_cast<pathime_option_info_t *>(buf);
        info->struct_size = bad;
        std::memcpy(snapshot, buf, sizeof buf);

        PT_CHECK_STATUS(pathime_engine_option_info(&engine, PATHIME_OPT_LEARNING, info),
                        PATHIME_ERROR_INVALID_ARGUMENT);
        PT_CHECK_MSG(std::memcmp(buf, snapshot, sizeof buf) == 0,
                     "struct_size %zu was rejected but the struct was written",
                     bad);
    }

    /*
     * Larger than this build's layout: a caller compiled against a newer header
     * than the library it loaded. Served — the library writes what it knows —
     * and told the *smaller* number, which is the caller's instruction not to
     * read past it. The bytes beyond must be left exactly as the caller left
     * them, since the library has no idea what they mean.
     */
    {
        alignas(pathime_option_info_t) unsigned char buf[kSize + kSlack];
        std::memset(buf, 0xAB, sizeof buf);
        pathime_option_info_t *info = reinterpret_cast<pathime_option_info_t *>(buf);
        info->struct_size = kSize + kSlack;

        PT_CHECK_STATUS(pathime_engine_option_info(&engine, PATHIME_OPT_MAX_CANDIDATES,
                                                   info),
                        PATHIME_OK);
        PT_CHECK_SIZE(info->struct_size, kSize);
        PT_CHECK(info->supported);
        PT_CHECK_I64(info->default_value, PATHIME_DEFAULT_MAX_CANDIDATES);

        bool tail_intact = true;
        for (size_t i = kSize; i < sizeof buf; ++i) {
            if (buf[i] != 0xAB) {
                tail_intact = false;
            }
        }
        PT_CHECK_MSG(tail_intact,
                     "the library wrote past the %zu bytes it reports knowing", kSize);
    }
}

/* ---------------------------------------------------------------------------
 * Two-level resolution
 * ------------------------------------------------------------------------- */

void test_two_level_resolution()
{
    pathime_engine engine;
    engine.id = PATHIME_ENGINE_ANTHY;
    ClientState state;
    pathime_context ctx;
    wire_context(ctx, engine, state, false);

    int64_t value = 0;

    /*
     * Tier 4, the descriptor default, reported identically at both levels
     * because the context inherits everything. Getters report the resolved
     * effective value — what the engine is actually doing — not which tier
     * supplied it, so there is nothing in the answer to tell them apart.
     */
    PT_CHECK_STATUS(
        pathime_engine_get_option_int(&engine, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, PATHIME_DEFAULT_MAX_CANDIDATES);
    PT_CHECK_STATUS(
        pathime_context_get_option_int(&ctx, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, PATHIME_DEFAULT_MAX_CANDIDATES);

    /* What tells them apart is is_set, which reports only its own level. */
    PT_CHECK(!pathime_engine_option_is_set(&engine, PATHIME_OPT_MAX_CANDIDATES));
    PT_CHECK(!pathime_context_option_is_set(&ctx, PATHIME_OPT_MAX_CANDIDATES));

    /* Tier 2: an engine value the context inherits. */
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&engine, PATHIME_OPT_MAX_CANDIDATES, 10),
        PATHIME_OK);
    PT_CHECK_STATUS(
        pathime_engine_get_option_int(&engine, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, 10);
    PT_CHECK_STATUS(
        pathime_context_get_option_int(&ctx, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, 10);
    PT_CHECK(pathime_engine_option_is_set(&engine, PATHIME_OPT_MAX_CANDIDATES));
    /* The context still overrides nothing: inheriting a value is not setting
     * one, and a settings interface distinguishing "this context overrides the
     * default" from "this context follows it" turns entirely on this. */
    PT_CHECK(!pathime_context_option_is_set(&ctx, PATHIME_OPT_MAX_CANDIDATES));

    /* Tier 1 shadows tier 2, for that context alone. */
    PT_CHECK_STATUS(
        pathime_context_set_option_int(&ctx, PATHIME_OPT_MAX_CANDIDATES, 20),
        PATHIME_OK);
    PT_CHECK_STATUS(
        pathime_context_get_option_int(&ctx, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, 20);
    PT_CHECK_STATUS(
        pathime_engine_get_option_int(&engine, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, 10);
    PT_CHECK(pathime_engine_option_is_set(&engine, PATHIME_OPT_MAX_CANDIDATES));
    PT_CHECK(pathime_context_option_is_set(&ctx, PATHIME_OPT_MAX_CANDIDATES));

    /* Un-shadowing: the engine value reappears, unchanged by having been
     * covered up. A reset that stored the inherited value instead of dropping
     * the override would look identical here and diverge the next time the
     * engine value changed. */
    PT_CHECK_STATUS(
        pathime_context_reset_option(&ctx, PATHIME_OPT_MAX_CANDIDATES), PATHIME_OK);
    PT_CHECK(!pathime_context_option_is_set(&ctx, PATHIME_OPT_MAX_CANDIDATES));
    PT_CHECK_STATUS(
        pathime_context_get_option_int(&ctx, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, 10);

    /* Proof that it really is the engine's value and not a copy: move it. */
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&engine, PATHIME_OPT_MAX_CANDIDATES, 11),
        PATHIME_OK);
    PT_CHECK_STATUS(
        pathime_context_get_option_int(&ctx, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, 11);

    /* "Resetting an option that was never set is a no-op." */
    PT_CHECK_STATUS(
        pathime_context_reset_option(&ctx, PATHIME_OPT_MAX_CANDIDATES), PATHIME_OK);
    PT_CHECK_STATUS(
        pathime_context_reset_option(&ctx, PATHIME_OPT_LEARNING), PATHIME_OK);

    /* Resetting the engine level falls back to tier 4, at both levels at once. */
    PT_CHECK_STATUS(
        pathime_engine_reset_option(&engine, PATHIME_OPT_MAX_CANDIDATES), PATHIME_OK);
    PT_CHECK(!pathime_engine_option_is_set(&engine, PATHIME_OPT_MAX_CANDIDATES));
    PT_CHECK_STATUS(
        pathime_engine_get_option_int(&engine, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, PATHIME_DEFAULT_MAX_CANDIDATES);
    PT_CHECK_STATUS(
        pathime_context_get_option_int(&ctx, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, PATHIME_DEFAULT_MAX_CANDIDATES);
    PT_CHECK_STATUS(
        pathime_engine_reset_option(&engine, PATHIME_OPT_MAX_CANDIDATES), PATHIME_OK);

    /* A context override survives its engine's value being reset, because it
     * never depended on it. */
    PT_CHECK_STATUS(
        pathime_context_set_option_bool(&ctx, PATHIME_OPT_LEARNING, false), PATHIME_OK);
    PT_CHECK_STATUS(
        pathime_engine_set_option_bool(&engine, PATHIME_OPT_LEARNING, true), PATHIME_OK);
    PT_CHECK_STATUS(
        pathime_engine_reset_option(&engine, PATHIME_OPT_LEARNING), PATHIME_OK);
    bool flag = true;
    PT_CHECK_STATUS(
        pathime_context_get_option_bool(&ctx, PATHIME_OPT_LEARNING, &flag), PATHIME_OK);
    PT_CHECK(!flag);
}

/* ---------------------------------------------------------------------------
 * Kind-typed setters and getters
 * ------------------------------------------------------------------------- */

void test_setter_kinds()
{
    pathime_engine anthy;
    anthy.id = PATHIME_ENGINE_ANTHY;
    pathime_engine pinyin;
    pinyin.id = PATHIME_ENGINE_PINYIN;
    pathime_engine table;
    table.id = PATHIME_ENGINE_TABLE;

    /*
     * "Each setter takes the value in its natural form. PATHIME_OPTION_ENUM and
     * PATHIME_OPTION_FLAGS use the int form. Calling the wrong one for an
     * option's type ... is PATHIME_ERROR_INVALID_ARGUMENT and changes nothing."
     *
     * The rejection matters more than it looks: a bool setter reaching an INT
     * option would otherwise store 1, which for max-candidates is a legal value
     * and would silently cap every candidate list at one entry.
     */
    PT_CHECK_STATUS(
        pathime_engine_set_option_bool(&anthy, PATHIME_OPT_MAX_CANDIDATES, true),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_set_option_string(&anthy, PATHIME_OPT_MAX_CANDIDATES, "8"),
        PATHIME_ERROR_INVALID_ARGUMENT);

    PT_CHECK_STATUS(pathime_engine_set_option_int(&anthy, PATHIME_OPT_LEARNING, 1),
                    PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_set_option_string(&anthy, PATHIME_OPT_LEARNING, "true"),
        PATHIME_ERROR_INVALID_ARGUMENT);

    /* ENUM and FLAGS take the int form and only the int form. */
    PT_CHECK_STATUS(
        pathime_engine_set_option_bool(&anthy, PATHIME_OPT_LATIN_WIDTH, true),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_set_option_string(&anthy, PATHIME_OPT_LATIN_WIDTH, "full"),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_set_option_bool(&pinyin, PATHIME_OPT_PINYIN_FUZZY, true),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&anthy, PATHIME_OPT_LATIN_WIDTH,
                                      PATHIME_WIDTH_FULL),
        PATHIME_OK);

    /* STRING takes the string form and only it. */
    PT_CHECK_STATUS(pathime_engine_set_option_int(&table, PATHIME_OPT_TABLE_FILE, 3),
                    PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_set_option_bool(&table, PATHIME_OPT_TABLE_FILE, true),
        PATHIME_ERROR_INVALID_ARGUMENT);

    /* Nothing that was rejected was stored. */
    PT_CHECK(!pathime_engine_option_is_set(&anthy, PATHIME_OPT_MAX_CANDIDATES));
    PT_CHECK(!pathime_engine_option_is_set(&anthy, PATHIME_OPT_LEARNING));
    PT_CHECK(!pathime_engine_option_is_set(&pinyin, PATHIME_OPT_PINYIN_FUZZY));
    PT_CHECK(!pathime_engine_option_is_set(&table, PATHIME_OPT_TABLE_FILE));

    /*
     * The getters apply the same rule from the other side, which they must: a
     * client that reads an ENUM through the bool getter would get "not zero"
     * and take PATHIME_ANTHY_SCRIPT_KATAKANA for true.
     */
    bool flag = false;
    int64_t number = 0;
    pathime_str_t text;

    PT_CHECK_STATUS(
        pathime_engine_get_option_bool(&anthy, PATHIME_OPT_MAX_CANDIDATES, &flag),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_get_option_bool(&anthy, PATHIME_OPT_LATIN_WIDTH, &flag),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(pathime_engine_get_option_int(&anthy, PATHIME_OPT_LEARNING, &number),
                    PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_get_option_string(&anthy, PATHIME_OPT_LEARNING, &text),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_get_option_string(&anthy, PATHIME_OPT_MAX_CANDIDATES, &text),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_get_option_int(&table, PATHIME_OPT_TABLE_FILE, &number),
        PATHIME_ERROR_INVALID_ARGUMENT);

    /* The int getter does serve ENUM and FLAGS, which is the whole reason the
     * rule is stated in terms of the value's natural form. */
    PT_CHECK_STATUS(
        pathime_engine_get_option_int(&anthy, PATHIME_OPT_LATIN_WIDTH, &number),
        PATHIME_OK);
    PT_CHECK_I64(number, PATHIME_WIDTH_FULL);
    PT_CHECK_STATUS(
        pathime_engine_get_option_int(&pinyin, PATHIME_OPT_PINYIN_FUZZY, &number),
        PATHIME_OK);
    PT_CHECK_I64(number, static_cast<int64_t>(bits_upto(PATHIME_PINYIN_FUZZY_ING_IN)));

    /* A NULL out-parameter is rejected before anything else, at both levels. */
    PT_CHECK_STATUS(
        pathime_engine_get_option_bool(&anthy, PATHIME_OPT_LEARNING, nullptr),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_get_option_int(&anthy, PATHIME_OPT_MAX_CANDIDATES, nullptr),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_get_option_string(&table, PATHIME_OPT_TABLE_FILE, nullptr),
        PATHIME_ERROR_INVALID_ARGUMENT);

    /* And a NULL handle, likewise before the initialization state and before
     * the option is even looked up. */
    PT_CHECK_STATUS(pathime_engine_set_option_bool(nullptr, PATHIME_OPT_LEARNING, true),
                    PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(nullptr, PATHIME_OPT_MAX_CANDIDATES, 4),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_set_option_string(nullptr, PATHIME_OPT_TABLE_FILE, "x"),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(pathime_engine_reset_option(nullptr, PATHIME_OPT_LEARNING),
                    PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(pathime_context_set_option_bool(nullptr, PATHIME_OPT_LEARNING, true),
                    PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(pathime_context_reset_option(nullptr, PATHIME_OPT_LEARNING),
                    PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_context_get_option_bool(nullptr, PATHIME_OPT_LEARNING, &flag),
        PATHIME_ERROR_INVALID_ARGUMENT);

    /* A value that is not an option id, at either level. */
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&anthy, (pathime_option_t)9999, 1),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&anthy, (pathime_option_t)-1, 1),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_get_option_int(&anthy, (pathime_option_t)kExpectedCount, &number),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_reset_option(&anthy, (pathime_option_t)kExpectedCount),
        PATHIME_ERROR_INVALID_ARGUMENT);
}

/* ---------------------------------------------------------------------------
 * Value legality
 * ------------------------------------------------------------------------- */

void test_value_validation()
{
    pathime_engine anthy;
    anthy.id = PATHIME_ENGINE_ANTHY;
    pathime_engine pinyin;
    pinyin.id = PATHIME_ENGINE_PINYIN;
    pathime_engine table;
    table.id = PATHIME_ENGINE_TABLE;

    int64_t value = 0;

    /* --- INT bounds ---------------------------------------------------- */

    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&anthy, PATHIME_OPT_MAX_CANDIDATES, 7),
        PATHIME_OK);

    /*
     * "Zero is rejected rather than treated as a way to suppress the list.
     * Engines that convert by selection cannot make progress without a
     * candidate, so a cap of zero would deadlock the composition." The value
     * left behind after each rejection is checked, because "changes nothing" is
     * the half of the contract a client actually relies on when it retries.
     */
    for (int64_t bad : {static_cast<int64_t>(0), static_cast<int64_t>(-1),
                        static_cast<int64_t>(-64), INT64_MIN}) {
        PT_CHECK_STATUS(
            pathime_engine_set_option_int(&anthy, PATHIME_OPT_MAX_CANDIDATES, bad),
            PATHIME_ERROR_INVALID_ARGUMENT);
        PT_CHECK_STATUS(
            pathime_engine_get_option_int(&anthy, PATHIME_OPT_MAX_CANDIDATES, &value),
            PATHIME_OK);
        PT_CHECK_I64(value, 7);
    }

    /* The bounds themselves are inclusive at both ends. The header states a
     * minimum of 1 and no maximum, so INT64_MAX is a legal cap — absurd as a
     * setting, and the point is that the descriptor rather than taste decides. */
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&anthy, PATHIME_OPT_MAX_CANDIDATES, 1),
        PATHIME_OK);
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&anthy, PATHIME_OPT_MAX_CANDIDATES, INT64_MAX),
        PATHIME_OK);
    PT_CHECK_STATUS(
        pathime_engine_get_option_int(&anthy, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, INT64_MAX);
    PT_CHECK_STATUS(
        pathime_engine_reset_option(&anthy, PATHIME_OPT_MAX_CANDIDATES), PATHIME_OK);

    /* --- ENUM membership ------------------------------------------------ */

    for (int64_t good : {static_cast<int64_t>(PATHIME_ANTHY_SCRIPT_HIRAGANA),
                         static_cast<int64_t>(PATHIME_ANTHY_SCRIPT_KATAKANA),
                         static_cast<int64_t>(PATHIME_ANTHY_SCRIPT_HALFWIDTH_KATAKANA)}) {
        PT_CHECK_STATUS(
            pathime_engine_set_option_int(&anthy, PATHIME_OPT_ANTHY_KANA_SCRIPT, good),
            PATHIME_OK);
        PT_CHECK_STATUS(
            pathime_engine_get_option_int(&anthy, PATHIME_OPT_ANTHY_KANA_SCRIPT, &value),
            PATHIME_OK);
        PT_CHECK_I64(value, good);
    }

    /*
     * One past the last enumerator, a negative value, and the two values either
     * side of the ABI ceiling the header states for an ENUM: "an ENUM option can
     * never define a value of 64 or above". 63 is representable in valid_values
     * and still not a member; 64 would shift past the end of the mask, which is
     * the case a naive `1 << value` test gets wrong by invoking undefined
     * behaviour rather than by answering no.
     */
    for (int64_t bad : {static_cast<int64_t>(3), static_cast<int64_t>(-1),
                        static_cast<int64_t>(63), static_cast<int64_t>(64),
                        static_cast<int64_t>(9999), INT64_MAX, INT64_MIN}) {
        PT_CHECK_STATUS(
            pathime_engine_set_option_int(&anthy, PATHIME_OPT_ANTHY_KANA_SCRIPT, bad),
            PATHIME_ERROR_INVALID_ARGUMENT);
        PT_CHECK_STATUS(
            pathime_engine_get_option_int(&anthy, PATHIME_OPT_ANTHY_KANA_SCRIPT, &value),
            PATHIME_OK);
        PT_CHECK_I64(value, PATHIME_ANTHY_SCRIPT_HALFWIDTH_KATAKANA);
    }

    /*
     * The per-engine narrowing is enforced, not merely advertised. A client
     * that ignored valid_values and asked pyzy for a mixed repertoire gets a
     * rejection rather than a silently substituted flag.
     */
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&pinyin, PATHIME_OPT_CHINESE_VARIANT,
                                      PATHIME_CHINESE_TRADITIONAL_ONLY),
        PATHIME_OK);
    for (int64_t bad : {static_cast<int64_t>(PATHIME_CHINESE_SIMPLIFIED_FIRST),
                        static_cast<int64_t>(PATHIME_CHINESE_TRADITIONAL_FIRST),
                        static_cast<int64_t>(PATHIME_CHINESE_ANY)}) {
        PT_CHECK_STATUS(
            pathime_engine_set_option_int(&pinyin, PATHIME_OPT_CHINESE_VARIANT, bad),
            PATHIME_ERROR_INVALID_ARGUMENT);
    }
    PT_CHECK_STATUS(
        pathime_engine_get_option_int(&pinyin, PATHIME_OPT_CHINESE_VARIANT, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, PATHIME_CHINESE_TRADITIONAL_ONLY);

    /* The same three values on the table engine, which supports all five. */
    for (int64_t good : {static_cast<int64_t>(PATHIME_CHINESE_SIMPLIFIED_FIRST),
                         static_cast<int64_t>(PATHIME_CHINESE_TRADITIONAL_FIRST),
                         static_cast<int64_t>(PATHIME_CHINESE_ANY)}) {
        PT_CHECK_STATUS(
            pathime_engine_set_option_int(&table, PATHIME_OPT_CHINESE_VARIANT, good),
            PATHIME_OK);
    }

    /* --- FLAGS subsets -------------------------------------------------- */

    const int64_t all_fuzzy =
        static_cast<int64_t>(bits_upto(PATHIME_PINYIN_FUZZY_ING_IN));

    for (int64_t good : {static_cast<int64_t>(0),
                         static_cast<int64_t>(PATHIME_PINYIN_FUZZY_C_CH),
                         static_cast<int64_t>(PATHIME_PINYIN_FUZZY_C_CH |
                                              PATHIME_PINYIN_FUZZY_ING_IN),
                         all_fuzzy}) {
        PT_CHECK_STATUS(
            pathime_engine_set_option_int(&pinyin, PATHIME_OPT_PINYIN_FUZZY, good),
            PATHIME_OK);
        PT_CHECK_STATUS(
            pathime_engine_get_option_int(&pinyin, PATHIME_OPT_PINYIN_FUZZY, &value),
            PATHIME_OK);
        PT_CHECK_I64(value, good);
    }

    /*
     * "Unknown bits are rejected rather than ignored: silently dropping one
     * would let a client believe a rule is in force that is not." The last bit
     * defined is bit 19, so bit 20 is the first that must be refused — alone,
     * and in company with legal ones, which is the case a mask-and-store
     * implementation would let through.
     */
    for (int64_t bad : {static_cast<int64_t>(1) << 20,
                        (static_cast<int64_t>(1) << 20) | PATHIME_PINYIN_FUZZY_C_CH,
                        all_fuzzy | (static_cast<int64_t>(1) << 20),
                        static_cast<int64_t>(1) << 62,
                        static_cast<int64_t>(-1), INT64_MIN}) {
        PT_CHECK_STATUS(
            pathime_engine_set_option_int(&pinyin, PATHIME_OPT_PINYIN_FUZZY, bad),
            PATHIME_ERROR_INVALID_ARGUMENT);
        PT_CHECK_STATUS(
            pathime_engine_get_option_int(&pinyin, PATHIME_OPT_PINYIN_FUZZY, &value),
            PATHIME_OK);
        PT_CHECK_I64(value, all_fuzzy);
    }

    /* The narrower flags option, whose last bit is bit 7. */
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&pinyin, PATHIME_OPT_PINYIN_CORRECTION,
                                      PATHIME_PINYIN_CORRECT_ON_ONG),
        PATHIME_OK);
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&pinyin, PATHIME_OPT_PINYIN_CORRECTION,
                                      static_cast<int64_t>(1) << 8),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_get_option_int(&pinyin, PATHIME_OPT_PINYIN_CORRECTION, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, PATHIME_PINYIN_CORRECT_ON_ONG);

    /* --- BOOL ----------------------------------------------------------- */

    bool flag = true;
    PT_CHECK_STATUS(pathime_engine_set_option_bool(&anthy, PATHIME_OPT_LEARNING, false),
                    PATHIME_OK);
    PT_CHECK_STATUS(pathime_engine_get_option_bool(&anthy, PATHIME_OPT_LEARNING, &flag),
                    PATHIME_OK);
    PT_CHECK(!flag);
    PT_CHECK_STATUS(pathime_engine_set_option_bool(&anthy, PATHIME_OPT_LEARNING, true),
                    PATHIME_OK);
    PT_CHECK_STATUS(pathime_engine_get_option_bool(&anthy, PATHIME_OPT_LEARNING, &flag),
                    PATHIME_OK);
    PT_CHECK(flag);
}

/* ---------------------------------------------------------------------------
 * Options an engine does not implement
 * ------------------------------------------------------------------------- */

void test_unsupported_options()
{
    pathime_engine hangul;
    hangul.id = PATHIME_ENGINE_HANGUL;
    ClientState state;
    pathime_context ctx;
    wire_context(ctx, hangul, state, true);

    pathime_engine anthy;
    anthy.id = PATHIME_ENGINE_ANTHY;

    int64_t value = 0;
    bool flag = false;
    pathime_str_t text;

    /*
     * "Setting an option the engine does not implement is
     * PATHIME_ERROR_UNSUPPORTED and changes nothing, so crossing engines is
     * diagnosed rather than silently ignored." The canonical case is
     * max-candidates on Hangul.
     */
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&hangul, PATHIME_OPT_MAX_CANDIDATES, 5),
        PATHIME_ERROR_UNSUPPORTED);
    PT_CHECK_STATUS(
        pathime_engine_get_option_int(&hangul, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_ERROR_UNSUPPORTED);
    PT_CHECK_STATUS(
        pathime_engine_reset_option(&hangul, PATHIME_OPT_MAX_CANDIDATES),
        PATHIME_ERROR_UNSUPPORTED);
    PT_CHECK(!pathime_engine_option_is_set(&hangul, PATHIME_OPT_MAX_CANDIDATES));

    /* The context level answers the same, since it resolves through the same
     * engine. A per-context escape hatch would be worse than useless: the
     * backend still has no candidates to cap. */
    PT_CHECK_STATUS(
        pathime_context_set_option_int(&ctx, PATHIME_OPT_MAX_CANDIDATES, 5),
        PATHIME_ERROR_UNSUPPORTED);
    PT_CHECK_STATUS(
        pathime_context_get_option_int(&ctx, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_ERROR_UNSUPPORTED);
    PT_CHECK(!pathime_context_option_is_set(&ctx, PATHIME_OPT_MAX_CANDIDATES));

    /*
     * Support is decided before the value is, and this is where that shows: an
     * illegal value for an option the engine does not implement reports the
     * engine rather than the value. The engine is the thing the client would
     * have to change, and telling it the number was out of range would send it
     * looking in the wrong place entirely.
     */
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&hangul, PATHIME_OPT_MAX_CANDIDATES, 0),
        PATHIME_ERROR_UNSUPPORTED);

    /* Support is also decided before the setter kind, for the same reason. */
    PT_CHECK_STATUS(
        pathime_engine_set_option_bool(&hangul, PATHIME_OPT_MAX_CANDIDATES, true),
        PATHIME_ERROR_UNSUPPORTED);
    PT_CHECK_STATUS(
        pathime_engine_get_option_string(&hangul, PATHIME_OPT_MAX_CANDIDATES, &text),
        PATHIME_ERROR_UNSUPPORTED);

    /* Learning is the other option Hangul does not implement — libhangul has no
     * learning to disable. */
    PT_CHECK_STATUS(pathime_engine_set_option_bool(&hangul, PATHIME_OPT_LEARNING, false),
                    PATHIME_ERROR_UNSUPPORTED);
    PT_CHECK_STATUS(pathime_engine_get_option_bool(&hangul, PATHIME_OPT_LEARNING, &flag),
                    PATHIME_ERROR_UNSUPPORTED);

    /*
     * And Pinyin and Bopomofo refuse it too, which is the less obvious half:
     * they are converting engines that do learn, so a client would reasonably
     * expect the option to be there. It is refused because pyzy offers no way
     * to withhold the learning commit and keeps its learned data
     * process-globally, while this option is per-context. Refusing is the
     * point — silently accepting a value that changed nothing would leave a
     * client believing it had turned learning off.
     */
    pathime_engine pinyin;
    pinyin.id = PATHIME_ENGINE_PINYIN;
    pathime_engine bopomofo;
    bopomofo.id = PATHIME_ENGINE_BOPOMOFO;

    PT_CHECK_STATUS(pathime_engine_set_option_bool(&pinyin, PATHIME_OPT_LEARNING, false),
                    PATHIME_ERROR_UNSUPPORTED);
    PT_CHECK_STATUS(pathime_engine_get_option_bool(&pinyin, PATHIME_OPT_LEARNING, &flag),
                    PATHIME_ERROR_UNSUPPORTED);
    PT_CHECK_STATUS(pathime_engine_set_option_bool(&bopomofo, PATHIME_OPT_LEARNING, false),
                    PATHIME_ERROR_UNSUPPORTED);
    PT_CHECK_STATUS(pathime_engine_get_option_bool(&bopomofo, PATHIME_OPT_LEARNING, &flag),
                    PATHIME_ERROR_UNSUPPORTED);

    /* Anthy still has it — the contrast is what makes the refusal above a
     * statement about pyzy rather than about learning in general. */
    PT_CHECK_STATUS(pathime_engine_set_option_bool(&anthy, PATHIME_OPT_LEARNING, false),
                    PATHIME_OK);
    PT_CHECK_STATUS(pathime_engine_get_option_bool(&anthy, PATHIME_OPT_LEARNING, &flag),
                    PATHIME_OK);
    PT_CHECK(!flag);
    PT_CHECK_STATUS(pathime_engine_reset_option(&anthy, PATHIME_OPT_LEARNING), PATHIME_OK);

    /* And in the other direction: every engine-prefixed option belongs to one
     * engine and is refused by the rest. */
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&anthy, PATHIME_OPT_HANGUL_LAYOUT,
                                      PATHIME_HANGUL_LAYOUT_3SET_390),
        PATHIME_ERROR_UNSUPPORTED);
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&anthy, PATHIME_OPT_PINYIN_FUZZY, 0),
        PATHIME_ERROR_UNSUPPORTED);
    PT_CHECK_STATUS(
        pathime_engine_set_option_string(&anthy, PATHIME_OPT_TABLE_FILE, "wubi.txt"),
        PATHIME_ERROR_UNSUPPORTED);
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&hangul, PATHIME_OPT_ANTHY_KANA_SCRIPT,
                                      PATHIME_ANTHY_SCRIPT_KATAKANA),
        PATHIME_ERROR_UNSUPPORTED);

    /* pyzy supplies two engine ids, and the header scopes special-phrases to
     * both of them and nothing else. (`bopomofo` is declared above, with the
     * learning checks.) */
    PT_CHECK_STATUS(
        pathime_engine_set_option_bool(&bopomofo, PATHIME_OPT_SPECIAL_PHRASES, false),
        PATHIME_OK);
    PT_CHECK_STATUS(
        pathime_engine_set_option_bool(&anthy, PATHIME_OPT_SPECIAL_PHRASES, false),
        PATHIME_ERROR_UNSUPPORTED);

    /* is_set answers false for an unsupported option rather than failing —
     * "the useful reading of all of those is the same". */
    PT_CHECK(!pathime_engine_option_is_set(&anthy, PATHIME_OPT_HANGUL_LAYOUT));
    PT_CHECK(!pathime_engine_option_is_set(&anthy, PATHIME_OPT_TABLE_FILE));
    PT_CHECK(!pathime_context_option_is_set(&ctx, PATHIME_OPT_LEARNING));
}

/* ---------------------------------------------------------------------------
 * String options
 * ------------------------------------------------------------------------- */

/* か — U+304B, one scalar in three bytes. The multi-byte case is the one that
 * matters for "exactly one character": a byte-counting check would pass every
 * ASCII wildcard and reject every CJK one. */
const char kKana1[] = "\xE3\x81\x8B";
/* かな — two scalars, six bytes. */
const char kKana2[] = "\xE3\x81\x8B\xE3\x81\xAA";

void test_string_options()
{
    pathime_engine table;
    table.id = PATHIME_ENGINE_TABLE;

    pathime_str_t text;

    /*
     * Unset. "Reading the option in that state yields an empty string, which is
     * how 'no table' is spelled: there is no tier-4 default to fall back to, and
     * no distinction between unset and empty." A zero-length slice points at ""
     * and never at NULL, so a client may pass it to anything expecting a C
     * string.
     */
    text.bytes = nullptr;
    text.len = 99;
    PT_CHECK_STATUS(
        pathime_engine_get_option_string(&table, PATHIME_OPT_TABLE_FILE, &text),
        PATHIME_OK);
    PT_CHECK(text.bytes != nullptr);
    PT_CHECK_SIZE(text.len, 0);
    PT_CHECK(text.bytes != nullptr && text.bytes[0] == '\0');
    PT_CHECK(!pathime_engine_option_is_set(&table, PATHIME_OPT_TABLE_FILE));

    /* Round-trip. len is authoritative, and the returned bytes are also
     * NUL-terminated, as everything the library produces is. */
    PT_CHECK_STATUS(
        pathime_engine_set_option_string(&table, PATHIME_OPT_TABLE_FILE,
                                         "/usr/share/tables/wubi.txt"),
        PATHIME_OK);
    PT_CHECK_STATUS(
        pathime_engine_get_option_string(&table, PATHIME_OPT_TABLE_FILE, &text),
        PATHIME_OK);
    PT_CHECK_STR(as_string(text), "/usr/share/tables/wubi.txt");
    PT_CHECK_SIZE(text.len, std::strlen("/usr/share/tables/wubi.txt"));
    PT_CHECK(text.bytes != nullptr && text.bytes[text.len] == '\0');
    PT_CHECK(pathime_engine_option_is_set(&table, PATHIME_OPT_TABLE_FILE));

    /* A path is UTF-8 on every platform, so a non-ASCII one round-trips
     * byte-identically rather than being transcoded to anything. */
    {
        const std::string path = std::string("/tmp/") + kKana1 + ".txt";
        PT_CHECK_STATUS(
            pathime_engine_set_option_string(&table, PATHIME_OPT_TABLE_FILE,
                                             path.c_str()),
            PATHIME_OK);
        PT_CHECK_STATUS(
            pathime_engine_get_option_string(&table, PATHIME_OPT_TABLE_FILE, &text),
            PATHIME_OK);
        PT_CHECK_STR(as_string(text), path);
    }

    /* Explicitly empty is legal and distinct from never having been set only in
     * what is_set reports — the resolved value is the same either way. */
    PT_CHECK_STATUS(
        pathime_engine_set_option_string(&table, PATHIME_OPT_TABLE_FILE, ""),
        PATHIME_OK);
    PT_CHECK_STATUS(
        pathime_engine_get_option_string(&table, PATHIME_OPT_TABLE_FILE, &text),
        PATHIME_OK);
    PT_CHECK_SIZE(text.len, 0);
    PT_CHECK(pathime_engine_option_is_set(&table, PATHIME_OPT_TABLE_FILE));

    /*
     * Invalid UTF-8 is refused. This is the boundary rule of the whole API —
     * "all text crossing this boundary is plain UTF-8" — and an option string
     * is the easiest place to smuggle something past it, because unlike preedit
     * text it is never looked at again until a table lookup fails mysteriously.
     */
    PT_CHECK_STATUS(
        pathime_engine_set_option_string(&table, PATHIME_OPT_TABLE_FILE, "/tmp/\xFF"),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_set_option_string(&table, PATHIME_OPT_TABLE_FILE, "\x80"),
        PATHIME_ERROR_INVALID_ARGUMENT);
    /* A truncated three-byte sequence: the terminator arrives mid-scalar. */
    PT_CHECK_STATUS(
        pathime_engine_set_option_string(&table, PATHIME_OPT_TABLE_FILE, "\xE3\x81"),
        PATHIME_ERROR_INVALID_ARGUMENT);
    /* An overlong form, which is how a naive validator gets talked into
     * accepting an ASCII delimiter it thought it had excluded. */
    PT_CHECK_STATUS(
        pathime_engine_set_option_string(&table, PATHIME_OPT_TABLE_FILE, "\xC0\xAF"),
        PATHIME_ERROR_INVALID_ARGUMENT);
    /* A surrogate half, which no conforming decoder can read back. */
    PT_CHECK_STATUS(
        pathime_engine_set_option_string(&table, PATHIME_OPT_TABLE_FILE,
                                         "\xED\xA0\x80"),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK_STATUS(
        pathime_engine_set_option_string(&table, PATHIME_OPT_TABLE_FILE, nullptr),
        PATHIME_ERROR_INVALID_ARGUMENT);

    /* None of that disturbed the stored value. */
    PT_CHECK_STATUS(
        pathime_engine_get_option_string(&table, PATHIME_OPT_TABLE_FILE, &text),
        PATHIME_OK);
    PT_CHECK_SIZE(text.len, 0);

    /* --- The two wildcards: "One character, or empty to disable." ------- */

    for (pathime_option_t option : {PATHIME_OPT_TABLE_SINGLE_WILDCARD,
                                    PATHIME_OPT_TABLE_MULTI_WILDCARD}) {
        /* The default is empty, meaning the table offers no such wildcard. */
        PT_CHECK_STATUS(pathime_engine_get_option_string(&table, option, &text),
                        PATHIME_OK);
        PT_CHECK_SIZE(text.len, 0);

        /* Empty, one ASCII scalar, and one three-byte scalar are all "one
         * character or none". The last is the case that separates a scalar
         * count from a byte count. */
        PT_CHECK_STATUS(pathime_engine_set_option_string(&table, option, ""),
                        PATHIME_OK);
        PT_CHECK_STATUS(pathime_engine_set_option_string(&table, option, "?"),
                        PATHIME_OK);
        PT_CHECK_STATUS(pathime_engine_set_option_string(&table, option, "*"),
                        PATHIME_OK);
        PT_CHECK_STATUS(pathime_engine_set_option_string(&table, option, kKana1),
                        PATHIME_OK);
        PT_CHECK_STATUS(pathime_engine_get_option_string(&table, option, &text),
                        PATHIME_OK);
        PT_CHECK_STR(as_string(text), kKana1);
        PT_CHECK_SIZE(text.len, 3);

        /*
         * Two scalars is PATHIME_ERROR_INVALID_ARGUMENT and changes nothing.
         * Both spellings are checked: two ASCII characters, which a byte-length
         * test also catches, and two kana, which only a scalar count catches —
         * six bytes reading as one character to nothing but a correct decoder.
         */
        PT_CHECK_STATUS(pathime_engine_set_option_string(&table, option, "ab"),
                        PATHIME_ERROR_INVALID_ARGUMENT);
        PT_CHECK_STATUS(pathime_engine_set_option_string(&table, option, kKana2),
                        PATHIME_ERROR_INVALID_ARGUMENT);
        PT_CHECK_STATUS(pathime_engine_set_option_string(&table, option, "*?"),
                        PATHIME_ERROR_INVALID_ARGUMENT);
        PT_CHECK_STATUS(pathime_engine_set_option_string(&table, option, "abcdef"),
                        PATHIME_ERROR_INVALID_ARGUMENT);

        /* Still the one scalar that was legal. */
        PT_CHECK_STATUS(pathime_engine_get_option_string(&table, option, &text),
                        PATHIME_OK);
        PT_CHECK_STR(as_string(text), kKana1);

        /* Invalid UTF-8 is refused before the length rule is even reached, so a
         * single malformed byte is not "one character". */
        PT_CHECK_STATUS(pathime_engine_set_option_string(&table, option, "\xFF"),
                        PATHIME_ERROR_INVALID_ARGUMENT);
        PT_CHECK_STATUS(pathime_engine_get_option_string(&table, option, &text),
                        PATHIME_OK);
        PT_CHECK_STR(as_string(text), kKana1);
    }

    /* The length rule belongs to the two wildcards alone: a table path of any
     * length is fine, which is what makes the rule a per-option one rather than
     * a property of STRING. */
    PT_CHECK_STATUS(
        pathime_engine_set_option_string(&table, PATHIME_OPT_TABLE_FILE, kKana2),
        PATHIME_OK);
}

/* ---------------------------------------------------------------------------
 * The Hangul capping rule
 * ------------------------------------------------------------------------- */

void test_hangul_capping()
{
    pathime_engine engine;
    engine.id = PATHIME_ENGINE_HANGUL;

    ClientState state_no;
    ClientState state_yes;
    pathime_context ctx_no;
    pathime_context ctx_yes;
    wire_context(ctx_no, engine, state_no, /*with_delete=*/false);
    wire_context(ctx_yes, engine, state_yes, /*with_delete=*/true);

    int64_t value = 0;

    /* A default-configured Hangul engine asks nothing of its client. */
    PT_CHECK(pathime_engine_requirements(&engine) == 0);

    /*
     * "Setting it on an engine always succeeds, since an engine has no client."
     * That is not a convenience: an engine-level setter that could fail because
     * of some unrelated context's callback table would make a client's global
     * preference depend on which fields happen to be open.
     */
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&engine, PATHIME_OPT_HANGUL_PREEDIT,
                                      PATHIME_HANGUL_PREEDIT_NONE),
        PATHIME_OK);

    /*
     * Engine-level resolution deliberately does not cap, and this is what that
     * buys: pathime_engine_requirements() reports the true configured value, so
     * pathime_context_create() can reject a client that cannot serve it instead
     * of quietly degrading. A capped engine-level answer would make that
     * rejection impossible to reach.
     */
    PT_CHECK_STATUS(
        pathime_engine_get_option_int(&engine, PATHIME_OPT_HANGUL_PREEDIT, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, PATHIME_HANGUL_PREEDIT_NONE);
    PT_CHECK(pathime_engine_requirements(&engine) ==
             (PATHIME_REQUIRES_SURROUNDING_TEXT | PATHIME_REQUIRES_DELETE_SURROUNDING));

    /*
     * The cap itself, and the one place in the API where a client capability
     * caps a value rather than refusing the call. The context that cannot
     * delete text resolves to SYLLABLE, and its getter reports the capped value
     * — so the client can see what it actually got rather than believing it is
     * in a mode it is not.
     */
    PT_CHECK_STATUS(
        pathime_context_get_option_int(&ctx_no, PATHIME_OPT_HANGUL_PREEDIT, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, PATHIME_HANGUL_PREEDIT_SYLLABLE);

    /* The context that can delete gets what the engine asked for. */
    PT_CHECK_STATUS(
        pathime_context_get_option_int(&ctx_yes, PATHIME_OPT_HANGUL_PREEDIT, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, PATHIME_HANGUL_PREEDIT_NONE);

    /* Neither context set anything: the capping happens during resolution, not
     * by writing a substitute value into the context's store. */
    PT_CHECK(!pathime_context_option_is_set(&ctx_no, PATHIME_OPT_HANGUL_PREEDIT));
    PT_CHECK(!pathime_context_option_is_set(&ctx_yes, PATHIME_OPT_HANGUL_PREEDIT));

    /*
     * A *context-level* set of the same value is refused outright rather than
     * capped. The two cases are decided differently on purpose: here the client
     * is asking for something in so many words and can be told no, whereas an
     * engine-level value arrives at a context that has nothing left to reject.
     */
    PT_CHECK_STATUS(
        pathime_context_set_option_int(&ctx_no, PATHIME_OPT_HANGUL_PREEDIT,
                                       PATHIME_HANGUL_PREEDIT_NONE),
        PATHIME_ERROR_MISSING_CALLBACK);
    PT_CHECK(!pathime_context_option_is_set(&ctx_no, PATHIME_OPT_HANGUL_PREEDIT));

    /* And accepted where the client can serve it. */
    PT_CHECK_STATUS(
        pathime_context_set_option_int(&ctx_yes, PATHIME_OPT_HANGUL_PREEDIT,
                                       PATHIME_HANGUL_PREEDIT_NONE),
        PATHIME_OK);
    PT_CHECK(pathime_context_option_is_set(&ctx_yes, PATHIME_OPT_HANGUL_PREEDIT));

    /* The other two values need nothing of the client, so the context that
     * cannot delete may set them freely — the rejection is about the mode, not
     * about the option. */
    for (int64_t good : {static_cast<int64_t>(PATHIME_HANGUL_PREEDIT_SYLLABLE),
                         static_cast<int64_t>(PATHIME_HANGUL_PREEDIT_WORD)}) {
        PT_CHECK_STATUS(
            pathime_context_set_option_int(&ctx_no, PATHIME_OPT_HANGUL_PREEDIT, good),
            PATHIME_OK);
        PT_CHECK_STATUS(
            pathime_context_get_option_int(&ctx_no, PATHIME_OPT_HANGUL_PREEDIT, &value),
            PATHIME_OK);
        PT_CHECK_I64(value, good);
    }

    /* Dropping the override puts the context back under the engine's value —
     * and therefore back under the cap. */
    PT_CHECK_STATUS(
        pathime_context_reset_option(&ctx_no, PATHIME_OPT_HANGUL_PREEDIT), PATHIME_OK);
    PT_CHECK_STATUS(
        pathime_context_get_option_int(&ctx_no, PATHIME_OPT_HANGUL_PREEDIT, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, PATHIME_HANGUL_PREEDIT_SYLLABLE);

    /* Returning the engine to a mode with no obligations clears the
     * requirements, which is what makes the query worth calling after
     * configuring rather than once at startup. */
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&engine, PATHIME_OPT_HANGUL_PREEDIT,
                                      PATHIME_HANGUL_PREEDIT_WORD),
        PATHIME_OK);
    PT_CHECK(pathime_engine_requirements(&engine) == 0);
    PT_CHECK_STATUS(
        pathime_engine_reset_option(&engine, PATHIME_OPT_HANGUL_PREEDIT), PATHIME_OK);
    PT_CHECK(pathime_engine_requirements(&engine) == 0);

    /*
     * No other engine can produce either bit — PATHIME_HANGUL_PREEDIT_NONE is
     * the only thing in the library that sets one. And a NULL handle answers 0
     * rather than rejecting, because no callback obligation can arise from an
     * engine that does not exist.
     */
    for (pathime_engine_id_t id : {PATHIME_ENGINE_ANTHY, PATHIME_ENGINE_PINYIN,
                                   PATHIME_ENGINE_BOPOMOFO, PATHIME_ENGINE_TABLE}) {
        pathime_engine other;
        other.id = id;
        PT_CHECK(pathime_engine_requirements(&other) == 0);
    }
    PT_CHECK(pathime_engine_requirements(nullptr) == 0);
}

/* ---------------------------------------------------------------------------
 * The engine-level broadcast
 * ------------------------------------------------------------------------- */

void test_engine_broadcast()
{
    pathime_engine engine;
    engine.id = PATHIME_ENGINE_ANTHY;

    ClientState s_inherit;
    ClientState s_override;
    ClientState s_silent;
    pathime_context inheriting;
    pathime_context overriding;
    pathime_context silent;

    wire_context(inheriting, engine, s_inherit, false);
    wire_context(overriding, engine, s_override, false);
    /* A client that supplies no composition_changed at all. The broadcast must
     * skip it rather than call through a null pointer — the header allows the
     * omission, so the loop below walks a context it cannot notify. */
    wire_context(silent, engine, s_silent, false, /*with_changed=*/false);

    /* The override that makes this context immune to the broadcast. A
     * context-level set notifies that context and no other. */
    PT_CHECK_STATUS(
        pathime_context_set_option_int(&overriding, PATHIME_OPT_MAX_CANDIDATES, 5),
        PATHIME_OK);
    PT_CHECK(s_override.changes == 1);
    PT_CHECK(s_inherit.changes == 0);
    PT_CHECK(s_silent.changes == 0);
    PT_CHECK(s_override.last == pathime_context_composition(&overriding));

    s_override.changes = 0;

    /*
     * "Resolution is late: an engine-level set changes the effective value for
     * every context that has not overridden that option, immediately, and
     * dispatches composition_changed to each of them." That is the useful
     * behaviour — one preference changed, every open field follows — and it is
     * also why engine setters are not callback-safe: this call reaches
     * callbacks belonging to contexts it was never passed.
     */
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&engine, PATHIME_OPT_MAX_CANDIDATES, 10),
        PATHIME_OK);
    PT_CHECK(s_inherit.changes == 1);
    /* Skipped, and skipped for a reason rather than by accident: tier 1 already
     * wins for this context, so nothing about it changed and telling its client
     * otherwise would force a redraw that displays the same thing. */
    PT_CHECK(s_override.changes == 0);
    PT_CHECK(s_silent.changes == 0);
    PT_CHECK(s_inherit.last == pathime_context_composition(&inheriting));

    int64_t value = 0;
    PT_CHECK_STATUS(
        pathime_context_get_option_int(&inheriting, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, 10);
    PT_CHECK_STATUS(
        pathime_context_get_option_int(&overriding, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, 5);
    PT_CHECK_STATUS(
        pathime_context_get_option_int(&silent, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, 10);

    /* An engine-level reset is a change like any other and broadcasts the same
     * way — "behaves in every other respect like a setter". */
    s_inherit.changes = 0;
    PT_CHECK_STATUS(
        pathime_engine_reset_option(&engine, PATHIME_OPT_MAX_CANDIDATES), PATHIME_OK);
    PT_CHECK(s_inherit.changes == 1);
    PT_CHECK(s_override.changes == 0);
    PT_CHECK_STATUS(
        pathime_context_get_option_int(&inheriting, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, PATHIME_DEFAULT_MAX_CANDIDATES);

    /*
     * A reset of an option that was never set is a no-op, and that includes
     * dispatching nothing. Read literally in preference to the neighbouring
     * "behaves like a setter": nothing was dropped, so nothing resolved
     * differently, so there is nothing to tell a client about.
     */
    s_inherit.changes = 0;
    PT_CHECK_STATUS(
        pathime_engine_reset_option(&engine, PATHIME_OPT_MAX_CANDIDATES), PATHIME_OK);
    PT_CHECK(s_inherit.changes == 0);

    /* A rejected set dispatches nothing either: a rejection is decided before
     * any work begins, and no callbacks were dispatched is part of what the
     * header means by one. */
    s_inherit.changes = 0;
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&engine, PATHIME_OPT_MAX_CANDIDATES, 0),
        PATHIME_ERROR_INVALID_ARGUMENT);
    PT_CHECK(s_inherit.changes == 0);
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&engine, PATHIME_OPT_HANGUL_LAYOUT, 0),
        PATHIME_ERROR_UNSUPPORTED);
    PT_CHECK(s_inherit.changes == 0);

    /*
     * An option that resets the composition takes the other branch of the
     * broadcast: each affected context is reset as pathime_context_reset()
     * would reset it. These contexts have empty compositions and are not
     * indeterminate, so that reset dispatches nothing — "produces a
     * composition_changed callback unless the composition was already empty".
     * The call still succeeds, and the value still lands.
     */
    s_inherit.changes = 0;
    s_override.changes = 0;
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&engine, PATHIME_OPT_ANTHY_TYPING_METHOD,
                                      PATHIME_ANTHY_TYPING_KANA),
        PATHIME_OK);
    PT_CHECK(s_inherit.changes == 0);
    PT_CHECK(s_override.changes == 0);
    PT_CHECK_STATUS(
        pathime_context_get_option_int(&inheriting, PATHIME_OPT_ANTHY_TYPING_METHOD,
                                       &value),
        PATHIME_OK);
    PT_CHECK_I64(value, PATHIME_ANTHY_TYPING_KANA);

    /* A context that is not registered with the engine — one the library never
     * saw created — is unreachable by the broadcast, which is the property that
     * makes engine.contexts the authority on who gets notified. */
    ClientState s_detached;
    pathime_context detached;
    detached.engine = &engine;
    detached.user_data = &s_detached;
    detached.commit_text = on_commit_text;
    detached.composition_changed = on_composition_changed;
    PT_CHECK_STATUS(
        pathime_engine_set_option_int(&engine, PATHIME_OPT_MAX_CANDIDATES, 12),
        PATHIME_OK);
    PT_CHECK(s_detached.changes == 0);
    /* It still resolves through the engine, because resolution reads the engine
     * rather than the list. */
    PT_CHECK_STATUS(
        pathime_context_get_option_int(&detached, PATHIME_OPT_MAX_CANDIDATES, &value),
        PATHIME_OK);
    PT_CHECK_I64(value, 12);
}

/*
 * Value names, checked against the descriptor rather than against a list
 * repeated here.
 *
 * The promise pathime_option_value_name() makes is what turns the inventory
 * walk into something a client can render: valid_values gives the legal set,
 * and every member of it must have a name. So this drives the exact loop the
 * header documents and demands an answer at each stop — which means a value
 * added to an option without a name added beside it fails here, and a name
 * left behind by a value that was removed fails too.
 */
void test_value_names()
{
    /* Named engine-independently, so the whole inventory is reachable from a
     * single descriptor query per option. */
    pathime_engine any;
    any.id = PATHIME_ENGINE_PINYIN;

    size_t named_options = 0;
    size_t named_values = 0;

    for (size_t row = 0; row < pathime_option_count(); ++row) {
        const pathime_option_t option = static_cast<pathime_option_t>(row);

        pathime_option_info_t info;
        std::memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        PT_CHECK_STATUS(pathime_engine_option_info(&any, option, &info), PATHIME_OK);

        /*
         * valid_values is only populated for the two types that have values,
         * and those are exactly the two types that get names. Everything else
         * is "" — including, deliberately, a BOOL, whose two values are not
         * worth a vocabulary.
         */
        const bool nameable = info.type == PATHIME_OPTION_ENUM ||
                              info.type == PATHIME_OPTION_FLAGS;
        if (!nameable) {
            PT_CHECK_STR(pathime_option_value_name(option, 0), "");
            PT_CHECK_STR(pathime_option_value_name(option, 1), "");
            continue;
        }

        /*
         * The per-engine descriptor is narrowed; the names are not, because a
         * value's name does not depend on who implements it — that is what
         * lets a client label an option it has narrowed for itself. So the set
         * to walk is the union across every engine, which is the unnarrowed
         * set the naming table is keyed to.
         */
        uint64_t valid = 0;
        for (size_t e = 0; e < kEngineCount; ++e) {
            pathime_engine probe;
            probe.id = static_cast<pathime_engine_id_t>(e);
            pathime_option_info_t probe_info;
            std::memset(&probe_info, 0, sizeof probe_info);
            probe_info.struct_size = sizeof probe_info;
            if (pathime_engine_option_info(&probe, option, &probe_info) == PATHIME_OK &&
                probe_info.supported) {
                valid |= probe_info.valid_values;
            }
        }
        PT_CHECK_MSG(valid != 0, "%s: an ENUM or FLAGS option with no legal values",
                     pathime_option_name(option));
        named_options++;

        std::vector<std::string> seen;
        for (int bit = 0; bit < 64; ++bit) {
            if ((valid & (UINT64_C(1) << bit)) == 0) continue;

            /* Exactly the conversion the header's example performs. */
            const int64_t value = info.type == PATHIME_OPTION_FLAGS
                                      ? static_cast<int64_t>(UINT64_C(1) << bit)
                                      : bit;
            const char *name = pathime_option_value_name(option, value);

            PT_CHECK_MSG(name[0] != '\0', "%s: legal value %d has no name",
                         pathime_option_name(option), bit);

            /* Names are a client's storage keys, so two values of one option
             * sharing one would make a stored setting ambiguous. */
            for (const std::string &prior : seen) {
                PT_CHECK_MSG(prior != name, "%s: \"%s\" names two values",
                             pathime_option_name(option), name);
            }
            seen.push_back(name);
            named_values++;
        }

        /*
         * A value no option defines is unnamed. Bit 40 is past the widest row
         * in the library — the twenty Pinyin fuzzy bits — and past the naming
         * table's own capacity, so it exercises both bounds at once and cannot
         * become legal without this line being revisited deliberately.
         */
        {
            const int64_t past = info.type == PATHIME_OPTION_FLAGS
                                     ? static_cast<int64_t>(UINT64_C(1) << 40)
                                     : 40;
            PT_CHECK_STR(pathime_option_value_name(option, past), "");
        }

        /* A FLAGS combination has no single name to give, and neither does
         * zero. Naming the lowest bit set would be worse than saying nothing:
         * a client storing it would round-trip the wrong value. */
        if (info.type == PATHIME_OPTION_FLAGS) {
            PT_CHECK_STR(pathime_option_value_name(option, 0), "");
            PT_CHECK_STR(pathime_option_value_name(option, 3), "");
        } else {
            PT_CHECK_STR(pathime_option_value_name(option, -1), "");
        }
    }

    /* The walk really did reach the inventory, rather than passing because it
     * found nothing to check. Thirteen ENUM options and two FLAGS. */
    PT_CHECK_SIZE(named_options, 15u);
    PT_CHECK(named_values > 60);

    /* A few names pinned literally, since everything above is self-referential
     * — a table and a descriptor that drifted together would satisfy it. */
    PT_CHECK_STR(pathime_option_value_name(PATHIME_OPT_ANTHY_PERIOD_STYLE,
                                           PATHIME_ANTHY_PERIOD_KUTEN),
                 "kuten");
    PT_CHECK_STR(pathime_option_value_name(PATHIME_OPT_CHINESE_VARIANT,
                                           PATHIME_CHINESE_TRADITIONAL_FIRST),
                 "traditional-first");
    PT_CHECK_STR(pathime_option_value_name(PATHIME_OPT_PINYIN_SCHEME,
                                           PATHIME_PINYIN_SCHEME_DOUBLE_MSPY),
                 "double-mspy");
    PT_CHECK_STR(pathime_option_value_name(PATHIME_OPT_HANGUL_LAYOUT,
                                           PATHIME_HANGUL_LAYOUT_3SET_390),
                 "3set-390");
    PT_CHECK_STR(pathime_option_value_name(PATHIME_OPT_PINYIN_CORRECTION,
                                           PATHIME_PINYIN_CORRECT_GN_NG),
                 "gn-ng");
    PT_CHECK_STR(pathime_option_value_name(PATHIME_OPT_PINYIN_FUZZY,
                                           PATHIME_PINYIN_FUZZY_ING_IN),
                 "ing-in");

    /* Not an option id, and an option id with the wrong kind of value. */
    PT_CHECK_STR(pathime_option_value_name(
                     static_cast<pathime_option_t>(pathime_option_count()), 0),
                 "");
    PT_CHECK_STR(pathime_option_value_name(PATHIME_OPT_MAX_CANDIDATES, 1), "");
}

}  // namespace

int main()
{
    test_before_init();

    /* Every option entry point below gates on this. */
    PT_CHECK_STATUS(pathime_init(nullptr), PATHIME_OK);

    test_descriptor_table();
    test_descriptor_spot_checks();
    test_value_names();
    test_struct_size_protocol();
    test_two_level_resolution();
    test_setter_kinds();
    test_value_validation();
    test_unsupported_options();
    test_string_options();
    test_hangul_capping();
    test_engine_broadcast();

    pathime_shutdown();
    return pt_report("core.options");
}
