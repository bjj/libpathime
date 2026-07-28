#include "options_view.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace demo {
namespace {

/*
 * Display names for the enum values, keyed by option. The library gives a
 * client the *set* of legal values through pathime_option_info_t::valid_values
 * and nothing else, on the ground that naming them is presentation — so this
 * table is the demo's, and an option missing from it still works and shows its
 * bare number.
 */
struct EnumLabels {
    pathime_option_t option;
    const char *labels[9];  /**< Indexed by value; a null ends the list. */
};

const EnumLabels kEnumLabels[] = {
    {PATHIME_OPT_LATIN_WIDTH,        {"half", "full", nullptr}},
    {PATHIME_OPT_PUNCTUATION_WIDTH,  {"half", "full", nullptr}},
    {PATHIME_OPT_CHINESE_VARIANT,    {"simplified", "traditional",
                                      "simplified first", "traditional first",
                                      "any", nullptr}},
    {PATHIME_OPT_HANGUL_LAYOUT,      {"2-set", "2-set yetgeul", "3-set/2",
                                      "3-set 390", "3-set final",
                                      "3-set noshift", "3-set yetgeul",
                                      "romaja", "ahnmatae"}},
    {PATHIME_OPT_HANGUL_PREEDIT,     {"syllable", "word", "none", nullptr}},
    {PATHIME_OPT_ANTHY_TYPING_METHOD,{"romaji", "kana", nullptr}},
    {PATHIME_OPT_ANTHY_KANA_SCRIPT,  {"hiragana", "katakana",
                                      "halfwidth katakana", nullptr}},
    {PATHIME_OPT_ANTHY_PERIOD_STYLE, {"kuten", "full-width", nullptr}},
    {PATHIME_OPT_ANTHY_SYMBOL_STYLE, {"corner + slash", "corner + middot",
                                      "bracket + slash", "bracket + middot",
                                      nullptr}},
    {PATHIME_OPT_ANTHY_ON_PERIOD,    {"nothing", "convert", "commit", nullptr}},
    {PATHIME_OPT_PINYIN_SCHEME,      {"full", "double MSPY", "double ZRM",
                                      "double ABC", "double ZGPY",
                                      "double PYJJ", "double XHE", nullptr}},
    {PATHIME_OPT_BOPOMOFO_LAYOUT,    {"standard", "ching-yeah", "eten", "ibm",
                                      nullptr}},
    {PATHIME_OPT_TABLE_INVALID_INPUT,{"commit candidate", "commit raw", nullptr}},
};

const char *enum_label(pathime_option_t option, std::int64_t value)
{
    if (value < 0 || value >= 9) return nullptr;
    for (const EnumLabels &entry : kEnumLabels) {
        if (entry.option != option) continue;
        return entry.labels[value];
    }
    return nullptr;
}

/** The legal values of an ENUM option, lowest first. */
std::vector<std::int64_t> legal_values(const pathime_option_info_t &info)
{
    std::vector<std::int64_t> values;
    for (int bit = 0; bit < 64; bit++)
        if (info.valid_values & (UINT64_C(1) << bit)) values.push_back(bit);
    return values;
}

pathime_status_t get_number(const OptionRow &row,
                            const pathime_engine_t *engine,
                            const pathime_context_t *ctx,
                            bool engine_level,
                            std::int64_t *out)
{
    if (row.info.type == PATHIME_OPTION_BOOL) {
        bool value = false;
        const pathime_status_t st =
            engine_level ? pathime_engine_get_option_bool(engine, row.option, &value)
                         : pathime_context_get_option_bool(ctx, row.option, &value);
        *out = value ? 1 : 0;
        return st;
    }
    return engine_level ? pathime_engine_get_option_int(engine, row.option, out)
                        : pathime_context_get_option_int(ctx, row.option, out);
}

pathime_status_t set_number(const OptionRow &row,
                            pathime_engine_t *engine,
                            pathime_context_t *ctx,
                            bool engine_level,
                            std::int64_t value)
{
    if (row.info.type == PATHIME_OPTION_BOOL) {
        return engine_level
            ? pathime_engine_set_option_bool(engine, row.option, value != 0)
            : pathime_context_set_option_bool(ctx, row.option, value != 0);
    }
    return engine_level ? pathime_engine_set_option_int(engine, row.option, value)
                        : pathime_context_set_option_int(ctx, row.option, value);
}

int popcount64(std::uint64_t v)
{
    int n = 0;
    for (; v != 0; v &= v - 1) n++;
    return n;
}

}  // namespace

std::vector<OptionRow> collect_options(const pathime_engine_t *engine)
{
    std::vector<OptionRow> rows;
    /* The inventory walk the header describes: ids are dense and append-only,
     * so every option is a value in [0, pathime_option_count()). */
    for (std::size_t i = 0; i < pathime_option_count(); i++) {
        OptionRow row;
        row.option = static_cast<pathime_option_t>(i);
        std::memset(&row.info, 0, sizeof(row.info));
        row.info.struct_size = sizeof(row.info);
        if (pathime_engine_option_info(engine, row.option, &row.info) != PATHIME_OK)
            continue;
        /* An option the engine does not implement is reported through
         * `supported`, not as an error, and every other member of the
         * descriptor is then unspecified — so this is the only safe place to
         * drop it. */
        if (!row.info.supported) continue;
        rows.push_back(row);
    }
    return rows;
}

std::string value_text(const OptionRow &row,
                       const pathime_engine_t *engine,
                       const pathime_context_t *ctx,
                       bool engine_level)
{
    char buf[96];

    if (row.info.type == PATHIME_OPTION_STRING) {
        pathime_str_t value{nullptr, 0};
        const pathime_status_t st =
            engine_level ? pathime_engine_get_option_string(engine, row.option, &value)
                         : pathime_context_get_option_string(ctx, row.option, &value);
        if (st != PATHIME_OK) return std::string("<") + pathime_status_string(st) + ">";
        if (value.len == 0) return "(empty)";
        return std::string(value.bytes, value.len);
    }

    std::int64_t value = 0;
    const pathime_status_t st = get_number(row, engine, ctx, engine_level, &value);
    if (st != PATHIME_OK) return std::string("<") + pathime_status_string(st) + ">";

    switch (row.info.type) {
    case PATHIME_OPTION_BOOL:
        return value != 0 ? "true" : "false";
    case PATHIME_OPTION_INT:
        std::snprintf(buf, sizeof(buf), "%" PRId64, value);
        return buf;
    case PATHIME_OPTION_ENUM: {
        const char *label = enum_label(row.option, value);
        if (label != nullptr) {
            std::snprintf(buf, sizeof(buf), "%s", label);
        } else {
            std::snprintf(buf, sizeof(buf), "value %" PRId64, value);
        }
        return buf;
    }
    case PATHIME_OPTION_FLAGS: {
        const std::uint64_t bits = static_cast<std::uint64_t>(value);
        std::snprintf(buf, sizeof(buf), "0x%08" PRIx64 "  (%d of %d bits)", bits,
                      popcount64(bits), popcount64(row.info.valid_values));
        return buf;
    }
    default:
        break;
    }
    return "?";
}

pathime_status_t adjust_option(const OptionRow &row,
                               pathime_engine_t *engine,
                               pathime_context_t *ctx,
                               bool engine_level,
                               int step)
{
    if (row.info.type == PATHIME_OPTION_STRING) return PATHIME_ERROR_UNSUPPORTED;

    std::int64_t value = 0;
    const pathime_status_t st = get_number(row, engine, ctx, engine_level, &value);
    if (st != PATHIME_OK) return st;

    switch (row.info.type) {
    case PATHIME_OPTION_BOOL:
        value = value != 0 ? 0 : 1;
        break;

    case PATHIME_OPTION_INT: {
        const std::int64_t next = value + step;
        if (next < row.info.min_value) value = row.info.min_value;
        else if (next > row.info.max_value) value = row.info.max_value;
        else value = next;
        break;
    }

    case PATHIME_OPTION_ENUM: {
        const std::vector<std::int64_t> values = legal_values(row.info);
        if (values.empty()) return PATHIME_ERROR_UNSUPPORTED;
        std::size_t at = 0;
        for (std::size_t i = 0; i < values.size(); i++)
            if (values[i] == value) at = i;
        const int direction = step >= 0 ? 1 : -1;
        at = (at + values.size() + static_cast<std::size_t>(direction)) % values.size();
        value = values[at];
        break;
    }

    case PATHIME_OPTION_FLAGS: {
        /* All or nothing. Turning PATHIME_OPT_PINYIN_FUZZY off wholesale shows
         * what the option does; picking individual mergers out of twenty
         * unnamed bits would not, and the library gives no names for them —
         * see this file's header comment. */
        const std::uint64_t all = row.info.valid_values;
        value = (static_cast<std::uint64_t>(value) == all)
                    ? 0 : static_cast<std::int64_t>(all);
        break;
    }

    default:
        return PATHIME_ERROR_UNSUPPORTED;
    }

    return set_number(row, engine, ctx, engine_level, value);
}

pathime_status_t reset_option(const OptionRow &row,
                              pathime_engine_t *engine,
                              pathime_context_t *ctx,
                              bool engine_level)
{
    return engine_level ? pathime_engine_reset_option(engine, row.option)
                        : pathime_context_reset_option(ctx, row.option);
}

bool is_set_here(const OptionRow &row,
                 const pathime_engine_t *engine,
                 const pathime_context_t *ctx,
                 bool engine_level)
{
    return engine_level ? pathime_engine_option_is_set(engine, row.option)
                        : pathime_context_option_is_set(ctx, row.option);
}

}  // namespace demo
