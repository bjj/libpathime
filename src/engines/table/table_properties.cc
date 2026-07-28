#include "engines/table/table_properties.h"

#include <algorithm>
#include <cstdlib>

#include "utf8.h"

namespace pathime {
namespace table {

namespace {

std::string to_lower(const std::string &s)
{
    std::string out = s;
    for (char &c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

std::string trim(const std::string &s)
{
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t' || s[begin] == '\r')) {
        ++begin;
    }
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) {
        --end;
    }
    return s.substr(begin, end - begin);
}

/*
 * ibus-table writes booleans as TRUE/FALSE and reads them case-insensitively.
 * Anything else is the declared default rather than an error: a table carrying
 * a typo in a field this engine happens to consult should still load, because
 * the alternative is a whole input method unavailable over one word.
 */
bool parse_bool(const std::string &value, bool fallback)
{
    const std::string v = to_lower(trim(value));
    if (v == "true" || v == "1" || v == "yes") {
        return true;
    }
    if (v == "false" || v == "0" || v == "no") {
        return false;
    }
    return fallback;
}

size_t parse_size(const std::string &value, size_t fallback)
{
    const std::string v = trim(value);
    if (v.empty()) {
        return fallback;
    }
    char *end = nullptr;
    const unsigned long long n = std::strtoull(v.c_str(), &end, 10);
    if (end == v.c_str() || *end != '\0') {
        return fallback;
    }
    return static_cast<size_t>(n);
}

/** Every scalar in @a text, as a set. */
std::set<char32_t> scalar_set(const std::string &text)
{
    std::set<char32_t> out;
    size_t offset = 0;
    uint32_t scalar = 0;
    while (utf8_next_scalar(text.data(), text.size(), &offset, &scalar)) {
        out.insert(static_cast<char32_t>(scalar));
    }
    return out;
}

/** The first scalar of @a text as UTF-8, or "" — the wildcards are one scalar. */
std::string first_scalar(const std::string &text)
{
    size_t offset = 0;
    uint32_t scalar = 0;
    if (!utf8_next_scalar(text.data(), text.size(), &offset, &scalar)) {
        return std::string();
    }
    return text.substr(0, offset);
}

std::vector<std::string> split(const std::string &text, char separator)
{
    std::vector<std::string> out;
    size_t begin = 0;
    while (begin <= text.size()) {
        const size_t end = text.find(separator, begin);
        if (end == std::string::npos) {
            out.push_back(trim(text.substr(begin)));
            break;
        }
        out.push_back(trim(text.substr(begin, end - begin)));
        begin = end + 1;
    }
    return out;
}

/**
 * A `pXY` token: character index then position within that character's goucima,
 * with a leading `-` on X counting from the end of the phrase.
 *
 * Y is a single digit in every table shipped with ibus-table-chinese and in the
 * syntax the spec gives, but it is parsed as a full number so that a table with
 * a goucima longer than nine characters is not silently misread.
 */
bool parse_position(const std::string &token, Rules::Position *out)
{
    if (token.size() < 3 || (token[0] != 'p' && token[0] != 'P')) {
        return false;
    }

    size_t i = 1;
    int sign = 1;
    if (token[i] == '-') {
        sign = -1;
        ++i;
    }
    if (i >= token.size() || token[i] < '0' || token[i] > '9') {
        return false;
    }

    /*
     * X is one digit and Y is the rest. The syntax has no separator between
     * them, so this is the only reading available: `p11` is (1,1), `p-11` is
     * (-1,1), and `p112` would be character 1, goucima position 12.
     */
    const int x = sign * (token[i] - '0');
    ++i;
    if (i >= token.size()) {
        return false;
    }

    int y = 0;
    for (; i < token.size(); ++i) {
        if (token[i] < '0' || token[i] > '9') {
            return false;
        }
        y = y * 10 + (token[i] - '0');
    }
    if (x == 0 || y == 0) {
        return false;  /* both are 1-based */
    }

    *out = Rules::Position(x, y);
    return true;
}

}  // namespace

const std::vector<Rules::Position> *Rules::for_length(size_t length) const
{
    const auto exact_it = exact.find(length);
    if (exact_it != exact.end()) {
        return &exact_it->second;
    }
    if (above != 0 && length > above) {
        const auto above_it = exact.find(above);
        if (above_it != exact.end()) {
            return &above_it->second;
        }
    }
    return nullptr;
}

bool parse_rules(const std::string &value, Rules *out)
{
    *out = Rules();
    if (trim(value).empty()) {
        return true;
    }

    for (const std::string &rule : split(value, ';')) {
        if (rule.empty()) {
            continue;
        }

        const size_t colon = rule.find(':');
        if (colon == std::string::npos) {
            return false;
        }

        const std::string condition = to_lower(trim(rule.substr(0, colon)));
        if (condition.size() < 3 || condition[0] != 'c') {
            return false;
        }
        const bool is_catch_all = condition[1] == 'a';
        if (!is_catch_all && condition[1] != 'e') {
            return false;
        }

        const size_t n = parse_size(condition.substr(2), 0);
        if (n < 2) {
            return false;  /* compounds start at two characters */
        }

        std::vector<Rules::Position> positions;
        for (const std::string &token : split(rule.substr(colon + 1), '+')) {
            Rules::Position position;
            if (!parse_position(token, &position)) {
                return false;
            }
            positions.push_back(position);
        }
        if (positions.empty()) {
            return false;
        }

        if (is_catch_all) {
            /*
             * "At most one `ca` rule, and it must carry the largest N" (§3.5).
             * A second one is a malformed rule set rather than a last-one-wins
             * situation, because the two would disagree about every phrase
             * above the smaller threshold.
             */
            if (out->above != 0) {
                return false;
            }
            out->above = n;
        }
        out->exact[n] = positions;
    }

    if (out->above != 0) {
        for (const auto &entry : out->exact) {
            if (entry.first > out->above) {
                return false;  /* the catch-all did not carry the largest N */
            }
        }
    }

    return true;
}

void TableProperties::set(const std::string &key, const std::string &value)
{
    const std::string k = to_lower(trim(key));
    const std::string v = trim(value);
    attrs[k] = v;

    if (k == "name") {
        name = v;
    } else if (k == "uuid") {
        uuid = v;
    } else if (k == "serial_number") {
        serial_number = static_cast<uint64_t>(parse_size(v, 0));
    } else if (k == "languages") {
        languages = split(v, ',');
    } else if (k == "valid_input_chars") {
        valid_input_chars = scalar_set(v);
    } else if (k == "start_chars") {
        start_chars = scalar_set(v);
    } else if (k == "no_check_chars") {
        no_check_chars = scalar_set(v);
    } else if (k == "max_key_length") {
        max_key_length = parse_size(v, max_key_length);
    } else if (k == "least_commit_length") {
        least_commit_length = parse_size(v, 0);
    } else if (k == "single_wildcard_char") {
        single_wildcard = first_scalar(v);
    } else if (k == "multi_wildcard_char") {
        multi_wildcard = first_scalar(v);
    } else if (k == "rules") {
        /*
         * A rule set that does not parse leaves `rules` empty rather than
         * failing the load. The consequence is confined and knowable: no
         * user-phrase derivation (§10.2) and no RULES-derived commit boundaries
         * (§7.5), so the table still types.
         */
        Rules parsed;
        if (parse_rules(v, &parsed)) {
            rules = parsed;
        }
    } else if (k == "auto_wildcard") {
        auto_wildcard = parse_bool(v, auto_wildcard);
    } else if (k == "auto_commit") {
        auto_commit = parse_bool(v, auto_commit);
    } else if (k == "auto_select") {
        auto_select = parse_bool(v, auto_select);
    } else if (k == "user_can_define_phrase") {
        user_can_define_phrase = parse_bool(v, user_can_define_phrase);
    } else if (k == "dynamic_adjust") {
        dynamic_adjust = parse_bool(v, dynamic_adjust);
    } else if (k == "pinyin_mode") {
        pinyin_mode = parse_bool(v, pinyin_mode);
    } else if (k == "suggestion_mode") {
        suggestion_mode = parse_bool(v, suggestion_mode);
    } else if (k == "def_full_width_punct") {
        def_full_width_punct = parse_bool(v, def_full_width_punct);
    } else if (k == "def_full_width_letter") {
        def_full_width_letter = parse_bool(v, def_full_width_letter);
    } else if (k == "language_filter") {
        const std::string mode = to_lower(v);
        if (mode.size() == 3 && mode[0] == 'c' && mode[1] == 'm' &&
            mode[2] >= '0' && mode[2] <= '4') {
            language_filter = mode[2] - '0';
        }
    }
    /* char_prompts is not set here: it arrives as a section, not a scalar. */
}

void TableProperties::finalize()
{
    if (valid_input_chars.empty()) {
        for (char c = 'a'; c <= 'z'; ++c) {
            valid_input_chars.insert(static_cast<char32_t>(c));
        }
    }
    if (max_key_length == 0) {
        max_key_length = 1;
    }

    /*
     * The two language predicates (§3.1). A prefix test rather than equality
     * because the declarations in ibus-table-chinese are locales, not language
     * codes: `zh_CN`, `zh_TW`, `zh` all have to answer the same question.
     */
    is_chinese = false;
    is_cjk = false;
    for (const std::string &language : languages) {
        const std::string l = to_lower(language);
        const bool zh = l.compare(0, 2, "zh") == 0;
        if (zh) {
            is_chinese = true;
        }
        if (zh || l.compare(0, 2, "ja") == 0 || l.compare(0, 2, "ko") == 0) {
            is_cjk = true;
        }
    }
}

std::set<size_t> TableProperties::commit_boundaries() const
{
    std::set<size_t> out;

    if (!rules.empty()) {
        /*
         * "the output length of each ceN rule for N from 2 up to (but not
         * including) the caN threshold" (§7.5). The catch-all is excluded
         * because it describes phrases of unbounded length, so its output
         * length is not a boundary the run can be said to reach.
         */
        for (const auto &entry : rules.exact) {
            if (rules.above != 0 && entry.first >= rules.above) {
                continue;
            }
            out.insert(entry.second.size());
        }
    } else if (least_commit_length > 0) {
        for (size_t n = least_commit_length; n <= max_key_length; ++n) {
            out.insert(n);
        }
    }

    out.insert(max_key_length);
    out.erase(0);
    return out;
}

bool TableProperties::is_input_char(char32_t scalar) const
{
    return valid_input_chars.count(scalar) != 0 || is_wildcard(scalar);
}

bool TableProperties::is_start_char(char32_t scalar) const
{
    if (start_chars.empty()) {
        return is_input_char(scalar);
    }
    return start_chars.count(scalar) != 0 || is_wildcard(scalar);
}

bool TableProperties::is_wildcard(char32_t scalar) const
{
    std::string encoded;
    if (!utf8_append_scalar(encoded, static_cast<uint32_t>(scalar))) {
        return false;
    }
    return (!single_wildcard.empty() && encoded == single_wildcard) ||
           (!multi_wildcard.empty() && encoded == multi_wildcard);
}

bool TableProperties::declared_number(pathime_option_t option, int64_t *out) const
{
    switch (option) {
    case PATHIME_OPT_TABLE_AUTO_COMMIT:
        *out = auto_commit ? 1 : 0;
        return true;
    case PATHIME_OPT_TABLE_AUTO_SELECT:
        *out = auto_select ? 1 : 0;
        return true;
    case PATHIME_OPT_TABLE_PINYIN_FALLBACK:
        /*
         * The declaration is necessary but not sufficient: the compiled
         * database has to hold the rows too. See TableProperties::pinyin_data
         * for why the two come apart for every table this library ships.
         */
        *out = (pinyin_mode && pinyin_data) ? 1 : 0;
        return true;
    case PATHIME_OPT_LEARNING:
        /*
         * A table that declares neither DYNAMIC_ADJUST nor
         * USER_CAN_DEFINE_PHRASE has nothing to learn *into*, so it declares
         * learning off. This is the one place tier 3 overrides a library
         * default that is on rather than off, and it is right: the header's
         * default describes engines that learn, and a table without the
         * declarations has no user-database schema to write.
         */
        *out = (dynamic_adjust || user_can_define_phrase) ? 1 : 0;
        return true;
    case PATHIME_OPT_CHINESE_VARIANT:
        if (language_filter < 0) {
            return false;
        }
        *out = language_filter;
        return true;
    default:
        return false;
    }
}

const char *TableProperties::declared_text(pathime_option_t option) const
{
    switch (option) {
    case PATHIME_OPT_TABLE_SINGLE_WILDCARD:
        return single_wildcard.empty() ? nullptr : single_wildcard.c_str();
    case PATHIME_OPT_TABLE_MULTI_WILDCARD:
        return multi_wildcard.empty() ? nullptr : multi_wildcard.c_str();
    default:
        return nullptr;
    }
}

}  // namespace table
}  // namespace pathime
