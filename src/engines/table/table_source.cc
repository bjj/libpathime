#include "engines/table/table_source.h"

#include <cstdlib>
#include <fstream>
#include <istream>
#include <sstream>

#include "utf8.h"

namespace pathime {
namespace table {

namespace {

std::string trim(const std::string &s)
{
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t' || s[begin] == '\r' ||
                           s[begin] == '\n')) {
        ++begin;
    }
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' ||
                           s[end - 1] == '\n')) {
        --end;
    }
    return s.substr(begin, end - begin);
}

std::string to_upper(const std::string &s)
{
    std::string out = s;
    for (char &c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return out;
}

/**
 * Strip a `###` comment. ibus-table's own parser treats `###` as beginning a
 * comment anywhere on the line, so a phrase containing it could not be written
 * — which is why no shipped table contains one, and why matching that behaviour
 * exactly is what keeps the tables readable.
 */
std::string strip_comment(const std::string &line)
{
    const size_t hash = line.find("###");
    if (hash == std::string::npos) {
        return line;
    }
    return line.substr(0, hash);
}

/** The section a `BEGIN_X` / `END_X` marker names, or "" if the line is not one. */
std::string marker(const std::string &line, const char *prefix)
{
    const size_t prefix_len = std::char_traits<char>::length(prefix);
    const std::string upper = to_upper(trim(line));
    if (upper.compare(0, prefix_len, prefix) != 0) {
        return std::string();
    }
    return upper.substr(prefix_len);
}

/** Split on the first run of the given separator characters. */
bool split_first(const std::string &line, const char *separators,
                 std::string *first, std::string *rest)
{
    const size_t at = line.find_first_of(separators);
    if (at == std::string::npos) {
        return false;
    }
    *first = line.substr(0, at);
    *rest = line.substr(at + 1);
    return true;
}

int64_t parse_freq(const std::string &text, bool *ok)
{
    const std::string v = trim(text);
    if (v.empty()) {
        *ok = false;
        return 0;
    }
    char *end = nullptr;
    const long long n = std::strtoll(v.c_str(), &end, 10);
    *ok = (end != v.c_str() && *end == '\0');
    return static_cast<int64_t>(n);
}

std::string line_error(size_t line_number, const std::string &what)
{
    std::ostringstream out;
    out << "line " << line_number << ": " << what;
    return out.str();
}

}  // namespace

bool parse_table_source(std::istream &in, TableSource *out, std::string *error)
{
    *out = TableSource();

    std::string section;   /* "" outside any BEGIN/END pair */
    std::string line;
    size_t line_number = 0;
    bool seen_content = false;

    while (std::getline(in, line)) {
        ++line_number;
        const std::string raw = trim(strip_comment(line));
        if (raw.empty()) {
            continue;
        }

        /*
         * The two legacy SCIM header lines, skipped wherever they appear before
         * any real content rather than only on lines 1 and 2 — the tables in
         * ibus-table-chinese put a comment block above them.
         */
        if (!seen_content &&
            (raw == "SCIM_Generic_Table_Phrase_Library_TEXT" || raw == "VERSION_1_0")) {
            continue;
        }

        const std::string begins = marker(raw, "BEGIN_");
        if (!begins.empty()) {
            if (!section.empty() && !(section == "DEFINITION" &&
                                      begins == "CHAR_PROMPTS_DEFINITION")) {
                if (error != nullptr) {
                    *error = line_error(line_number,
                                        "BEGIN_" + begins + " inside " + section);
                }
                return false;
            }
            seen_content = true;
            section = begins;
            continue;
        }

        const std::string ends = marker(raw, "END_");
        if (!ends.empty()) {
            if (section != ends) {
                if (error != nullptr) {
                    *error = line_error(line_number, "END_" + ends + " does not close " +
                                                         (section.empty() ? "anything"
                                                                          : section));
                }
                return false;
            }
            /*
             * CHAR_PROMPTS_DEFINITION nests inside DEFINITION (§3.4), so
             * closing it returns to the definition section rather than to the
             * top level.
             */
            section = (ends == "CHAR_PROMPTS_DEFINITION") ? std::string("DEFINITION")
                                                          : std::string();
            continue;
        }

        seen_content = true;

        if (section == "DEFINITION") {
            std::string key;
            std::string value;
            if (!split_first(raw, "=", &key, &value)) {
                if (error != nullptr) {
                    *error = line_error(line_number, "definition line has no '='");
                }
                return false;
            }
            /* `KEY == VALUE` is accepted alongside `KEY = VALUE` (§3.1). */
            if (!value.empty() && value[0] == '=') {
                value.erase(0, 1);
            }
            out->properties.set(trim(key), trim(value));
        } else if (section == "TABLE" || section == "TABLE_EXTRA") {
            /*
             * Three tab-separated columns plus an optional fourth this engine
             * ignores. Split on tabs only: a phrase may contain spaces, and
             * several tables' entries do.
             */
            const size_t first_tab = raw.find('\t');
            const size_t second_tab = (first_tab == std::string::npos)
                                          ? std::string::npos
                                          : raw.find('\t', first_tab + 1);
            if (second_tab == std::string::npos) {
                ++out->skipped_rows;
                continue;
            }

            PhraseRow row;
            row.tabkeys = raw.substr(0, first_tab);
            row.phrase = raw.substr(first_tab + 1, second_tab - first_tab - 1);

            const size_t third_tab = raw.find('\t', second_tab + 1);
            const std::string freq_text =
                (third_tab == std::string::npos)
                    ? raw.substr(second_tab + 1)
                    : raw.substr(second_tab + 1, third_tab - second_tab - 1);

            bool ok = false;
            row.freq = parse_freq(freq_text, &ok);
            if (!ok) {
                ++out->skipped_rows;
                continue;
            }

            /* The literal NOSYMBOL means the empty string (§3.2). */
            if (row.phrase == "NOSYMBOL") {
                row.phrase.clear();
            }

            out->phrases.push_back(row);
        } else if (section == "GOUCI") {
            std::string zi;
            std::string code;
            if (!split_first(raw, " \t", &zi, &code)) {
                continue;  /* a lone token declares nothing */
            }
            zi = trim(zi);
            code = trim(code);
            if (!zi.empty() && !code.empty()) {
                out->goucima[zi] = code;
            }
        } else if (section == "CHAR_PROMPTS_DEFINITION") {
            std::string key;
            std::string prompt;
            if (!split_first(raw, " \t", &key, &prompt)) {
                continue;
            }
            key = trim(key);
            prompt = trim(prompt);

            size_t offset = 0;
            uint32_t scalar = 0;
            if (!prompt.empty() &&
                utf8_next_scalar(key.data(), key.size(), &offset, &scalar)) {
                out->properties.char_prompts[static_cast<char32_t>(scalar)] = prompt;
            }
        }
        /* Any other section is data for a consumer that is not this engine. */
    }

    if (!section.empty()) {
        if (error != nullptr) {
            *error = "unexpected end of file inside " + section;
        }
        return false;
    }

    out->properties.finalize();
    return true;
}

bool parse_table_source_file(const std::string &path, TableSource *out, std::string *error)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in.is_open()) {
        if (error != nullptr) {
            *error = "cannot open " + path;
        }
        return false;
    }
    return parse_table_source(in, out, error);
}

void derive_goucima(TableSource *source)
{
    for (const PhraseRow &row : source->phrases) {
        /*
         * Single-character phrases only: goucima describe what one character
         * contributes to a compound's key, so a multi-character phrase has
         * none of its own.
         */
        size_t offset = 0;
        uint32_t scalar = 0;
        if (!utf8_next_scalar(row.phrase.data(), row.phrase.size(), &offset, &scalar) ||
            offset != row.phrase.size()) {
            continue;
        }

        const auto existing = source->goucima.find(row.phrase);
        if (existing == source->goucima.end()) {
            source->goucima[row.phrase] = row.tabkeys;
        } else if (row.tabkeys.size() > existing->second.size()) {
            /* "the longest tabkeys sequence that produces exactly that character" */
            existing->second = row.tabkeys;
        }
    }
}

bool derive_single_wildcard(TableSource *source, char key)
{
    /* A table that chose its own wildcards keeps them: this is a default, not
     * an override. */
    if (!source->properties.single_wildcard.empty() ||
        !source->properties.multi_wildcard.empty()) {
        return false;
    }

    for (const PhraseRow &row : source->phrases) {
        /*
         * Position 0 is exempt. A leading occurrence stays literal at lookup
         * time (TableProperties::is_wildcard_at), so it is not a conflict — and
         * exempting it is what lets Cangjie keep its `z`-prefixed punctuation
         * codes while gaining the wildcard everywhere else.
         */
        if (row.tabkeys.find(key, 1) != std::string::npos) {
            return false;
        }
    }

    source->properties.set("SINGLE_WILDCARD_CHAR", std::string(1, key));
    source->properties.attrs["single_wildcard_char"] = std::string(1, key);
    return true;
}

namespace {

/** The printable ASCII characters that are neither letters nor digits — the
 *  keys src/punctuation.h maps. */
bool is_ascii_punctuation(char32_t scalar)
{
    if (scalar <= U' ' || scalar >= 0x7F) {
        return false;
    }
    return !((scalar >= U'0' && scalar <= U'9') || (scalar >= U'A' && scalar <= U'Z') ||
             (scalar >= U'a' && scalar <= U'z'));
}

std::vector<char32_t> scalars(const std::string &text)
{
    std::vector<char32_t> out;
    size_t offset = 0;
    uint32_t scalar = 0;
    while (utf8_next_scalar(text.data(), text.size(), &offset, &scalar)) {
        out.push_back(static_cast<char32_t>(scalar));
    }
    return out;
}

/** @a text with every scalar in @a drop removed, other scalars kept in order. */
std::string remove_scalars(const std::string &text, const std::set<char32_t> &drop)
{
    std::string out;
    out.reserve(text.size());
    size_t offset = 0;
    while (offset < text.size()) {
        const size_t begin = offset;
        uint32_t scalar = 0;
        if (!utf8_next_scalar(text.data(), text.size(), &offset, &scalar)) {
            break;
        }
        if (drop.count(static_cast<char32_t>(scalar)) == 0) {
            out.append(text, begin, offset - begin);
        }
    }
    return out;
}

}  // namespace

size_t strip_punctuation_keys(TableSource *source)
{
    TableProperties &properties = source->properties;

    /* Only a CJK table's keys are claimed by the punctuation layer; for any
     * other table there is no ownership question and nothing to strip. */
    if (!properties.is_cjk) {
        return 0;
    }

    std::set<char32_t> stripped;
    for (const char32_t scalar : properties.valid_input_chars) {
        if (is_ascii_punctuation(scalar) && !properties.is_wildcard(scalar)) {
            stripped.insert(scalar);
        }
    }

    /* A character any multi-key code spells with is alphabet, not punctuation. */
    for (const PhraseRow &row : source->phrases) {
        if (stripped.empty()) {
            return 0;
        }
        const std::vector<char32_t> keys = scalars(row.tabkeys);
        if (keys.size() < 2) {
            continue;
        }
        for (const char32_t key : keys) {
            stripped.erase(key);
        }
    }
    if (stripped.empty() || stripped.size() == properties.valid_input_chars.size()) {
        /* The second case is a table that is nothing but punctuation, which
         * stripping would turn into no table at all. Left as declared. */
        return 0;
    }

    /* Every row a stripped character keyed is single-key, by construction. */
    size_t dropped = 0;
    std::vector<PhraseRow> kept;
    kept.reserve(source->phrases.size());
    for (PhraseRow &row : source->phrases) {
        const std::vector<char32_t> keys = scalars(row.tabkeys);
        if (keys.size() == 1 && stripped.count(keys[0]) != 0) {
            ++dropped;
        } else {
            kept.push_back(std::move(row));
        }
    }
    source->phrases.swap(kept);

    /*
     * Both forms of the declaration: the typed sets the engine consults, and
     * the raw attribute strings compilation writes back into the `ime` table.
     * A strip that changed one but not the other would compile a database
     * whose declaration re-grants the keys the rows no longer serve.
     */
    for (const char32_t scalar : stripped) {
        properties.valid_input_chars.erase(scalar);
        properties.start_chars.erase(scalar);
        properties.char_prompts.erase(scalar);
    }
    const auto valid = properties.attrs.find("valid_input_chars");
    if (valid != properties.attrs.end()) {
        valid->second = remove_scalars(valid->second, stripped);
    }
    const auto start = properties.attrs.find("start_chars");
    if (start != properties.attrs.end()) {
        start->second = remove_scalars(start->second, stripped);
    }

    return dropped;
}

void apply_frequency_transfer(TableSource *target,
                              const TableSource &source,
                              int64_t threshold)
{
    /*
     * Last occurrence wins, which is the reference's behaviour rather than a
     * choice: it fills a dict keyed by phrase as it scans, so a phrase reachable
     * by several codes ends up with the frequency of its last row. Taking the
     * maximum instead would be defensible and would not match.
     */
    std::map<std::string, int64_t> ranked;
    for (const PhraseRow &row : source.phrases) {
        ranked[row.phrase] = row.freq;
    }

    for (PhraseRow &row : target->phrases) {
        /*
         * Strictly below the threshold, the table's own ordering stands
         * untouched — that is what "retains all manual sorting" means, and the
         * tables use runs like 999, 998, 997 to express a deliberate order
         * among near-equals.
         *
         * At or above it, the frequency is *replaced* rather than raised:
         * usage frequency plus the threshold. The addition is what keeps every
         * rewritten row above every manually ordered one, and a phrase the
         * frequency table has never heard of contributes 0, so it lands exactly
         * on the threshold and is left where it was relative to its peers.
         *
         * `>=` and not `>`: the tables set their top choice to exactly the
         * default threshold — every primary entry in cangjie5 is 1000 — so a
         * strict comparison transfers nothing at all.
         */
        if (row.freq < threshold) {
            continue;
        }
        const auto it = ranked.find(row.phrase);
        row.freq = (it == ranked.end() ? 0 : it->second) + threshold;
    }
}

}  // namespace table
}  // namespace pathime
