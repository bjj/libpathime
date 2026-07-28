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

void apply_frequency_transfer(TableSource *target,
                              const TableSource &source,
                              int64_t threshold)
{
    /* The best frequency each phrase reaches in the source table. */
    std::map<std::string, int64_t> best;
    for (const PhraseRow &row : source.phrases) {
        const auto it = best.find(row.phrase);
        if (it == best.end() || row.freq > it->second) {
            best[row.phrase] = row.freq;
        }
    }

    for (PhraseRow &row : target->phrases) {
        if (row.freq <= threshold) {
            continue;  /* below the cutoff the table's own ordering stands */
        }
        const auto it = best.find(row.phrase);
        if (it != best.end() && it->second > threshold) {
            row.freq = it->second;
        }
    }
}

}  // namespace table
}  // namespace pathime
