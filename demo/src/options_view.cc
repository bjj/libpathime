#include "options_view.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace demo {
namespace {

/** The legal values of an ENUM option, lowest first. */
std::vector<std::int64_t> legal_values(const pathime_option_info_t &info)
{
    std::vector<std::int64_t> values;
    for (int bit = 0; bit < 64; bit++)
        if (info.valid_values & (UINT64_C(1) << bit)) values.push_back(bit);
    return values;
}

/** The honoured bits of a FLAGS option, as masks, lowest first. */
std::vector<std::uint64_t> legal_bits(const pathime_option_info_t &info)
{
    std::vector<std::uint64_t> bits;
    for (int bit = 0; bit < 64; bit++)
        if (info.valid_values & (UINT64_C(1) << bit)) bits.push_back(UINT64_C(1) << bit);
    return bits;
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
        if (value.len == 0) {
            /* Say what the arrow keys would do, rather than only that nothing
             * is set: "(empty)" alone reads as "and there is nothing here". */
            if (row.info.valid_value_count != 0) {
                std::snprintf(buf, sizeof(buf), "(none)   %zu available",
                              row.info.valid_value_count);
                return buf;
            }
            return "(empty)";
        }
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
        /* The library's own name for the value. Not display text — it is a
         * machine-readable key a real client would map to its own localized
         * strings — but it is what turns the inventory walk into something
         * renderable, and printing it directly is the honest demonstration of
         * how far the walk gets on its own. */
        const char *name = pathime_option_value_name(row.option, value);
        if (name[0] != '\0') {
            std::snprintf(buf, sizeof(buf), "%s", name);
        } else {
            std::snprintf(buf, sizeof(buf), "value %" PRId64, value);
        }
        return buf;
    }
    case PATHIME_OPTION_FLAGS: {
        /* The bit under the edit cursor is named beside the count, which is
         * what makes bit-at-a-time editing legible: "12 of 20 bits" alone says
         * nothing about which one Left/Right is about to flip. */
        const std::uint64_t bits = static_cast<std::uint64_t>(value);
        const std::vector<std::uint64_t> all = legal_bits(row.info);
        const std::size_t at = row.flags_bit < all.size() ? row.flags_bit : 0;
        const char *name = all.empty()
                               ? ""
                               : pathime_option_value_name(
                                     row.option, static_cast<std::int64_t>(all[at]));
        std::snprintf(buf, sizeof(buf), "%d of %d bits   %s%s",
                      popcount64(bits), popcount64(row.info.valid_values),
                      (!all.empty() && (bits & all[at]) != 0) ? "[x] " : "[ ] ",
                      name);
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
    if (row.info.type == PATHIME_OPTION_STRING) {
        /*
         * A string option is edited by stepping through the values the library
         * enumerates, exactly as an enum is — the library reports how many
         * there are and names each by index, so this loop needs no idea that
         * the option happens to be about tables.
         *
         * An option with nothing to enumerate is genuinely not editable here:
         * this panel steps through values and has no text entry, so there is
         * no honest thing to do with a free-form string.
         */
        if (row.info.valid_value_count == 0) return PATHIME_ERROR_UNSUPPORTED;

        pathime_str_t current{nullptr, 0};
        const pathime_status_t got =
            engine_level ? pathime_engine_get_option_string(engine, row.option, &current)
                         : pathime_context_get_option_string(ctx, row.option, &current);
        if (got != PATHIME_OK) return got;

        const std::size_t count = row.info.valid_value_count;
        const std::string now(current.bytes ? current.bytes : "", current.len);

        /*
         * Where the current value is not one of the enumerated ones — unset, or
         * a path the client typed — the first step lands on entry 0 rather than
         * moving relative to something that is not in the list.
         */
        std::size_t at = count;
        for (std::size_t i = 0; i < count; i++)
            if (now == pathime_option_value_name(row.option, static_cast<std::int64_t>(i)))
                at = i;

        std::size_t next;
        if (at == count) {
            next = (step >= 0) ? 0 : count - 1;
        } else {
            const int direction = step >= 0 ? 1 : -1;
            next = (at + count + static_cast<std::size_t>(direction)) % count;
        }

        const char *chosen =
            pathime_option_value_name(row.option, static_cast<std::int64_t>(next));
        return engine_level ? pathime_engine_set_option_string(engine, row.option, chosen)
                            : pathime_context_set_option_string(ctx, row.option, chosen);
    }

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
        /*
         * One bit at a time, which is only worth offering because
         * pathime_option_value_name() can say which bit it is. This used to be
         * all-or-nothing: twenty anonymous fuzzy-pinyin bits taught a user
         * nothing individually, so the panel flipped the whole set. Now each
         * one has a name, so each one is worth reaching.
         *
         * step 0 — the toggle key — flips the bit under the edit cursor; the
         * cursor itself moves with the same Left/Right that walks an enum's
         * values, which is handled by the caller in app.cc.
         */
        const std::vector<std::uint64_t> all = legal_bits(row.info);
        if (all.empty()) return PATHIME_ERROR_UNSUPPORTED;
        const std::size_t at = row.flags_bit < all.size() ? row.flags_bit : 0;
        value = static_cast<std::int64_t>(static_cast<std::uint64_t>(value) ^ all[at]);
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
