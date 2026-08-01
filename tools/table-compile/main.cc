/*
 * pathime-table-compile — turn an ibus-table source `.txt` into the compiled
 * SQLite `.db` of docs/ibus-table-mapping.md §4.
 *
 * This exists because `ibus-table-chinese` distributes source text only, while
 * the engine reads compiled databases. Upstream's own build calls
 * `ibus-table-createdb`, which is part of ibus-table and therefore Python; this
 * is the replacement, and being a C++ program built from the same sources as
 * the engine it needs no interpreter and no shell utilities. That is what makes
 * the table data build identically on Windows, where upstream's pipeline of
 * sed, iconv and awk does not run at all.
 *
 * It links no part of the engine. Everything it uses — the source parser, the
 * declaration, the database writer — sits below backend.h on purpose, so the
 * tool and the library share the data layer without the tool pulling in
 * compositions, options or key events.
 */

#include <cstdio>
#include <cstring>
#include <string>

#include "engines/table/coverage.h"
#include "engines/table/table_db.h"
#include "engines/table/table_source.h"

namespace {

int usage(const char *program)
{
    std::fprintf(stderr,
                 "usage: %s [options] <source.txt> <output.db>\n"
                 "\n"
                 "  --freq-from <table.txt>   raise frequencies from a second table\n"
                 "  --freq-threshold <n>      entries at or below <n> keep their own\n"
                 "                            frequency (default 1000)\n"
                 "  --no-glyph-filter         keep entries no font can render\n"
                 "  --no-derive-wildcard      do not declare `z` as a wildcard\n"
                 "  --keep-punctuation        keep single-key punctuation entries\n"
                 "\n"
                 "Frequency transfer takes usage-ranked data from one table and\n"
                 "applies it to a table that sorts structurally, so a partially\n"
                 "typed code offers common characters first. Entries at or below\n"
                 "the threshold are left alone, which preserves each table's own\n"
                 "deliberate ordering of its top choices.\n"
                 "\n"
                 "Glyph filtering is the other half of the same purpose and is on\n"
                 "by default: entries whose characters fall outside the compiled-in\n"
                 "coverage map are dropped, so a mistyped code cannot fill the\n"
                 "candidate list with tofu. Which map that is was fixed when this\n"
                 "tool was built (LIBPATHIME_TABLE_COVERAGE) and is named in the\n"
                 "line printed on success; the maps are checked in rather than read\n"
                 "from a font at build time, which is what keeps a compiled .db\n"
                 "reproducible. See tools/generate-coverage.py. --no-glyph-filter\n"
                 "turns filtering off for anyone whose target can render Extension\n"
                 "B and would rather have the entries than the guarantee.\n"
                 "\n"
                 "Punctuation stripping removes ASCII punctuation from a CJK\n"
                 "table's VALID_INPUT_CHARS when no multi-key code uses it, and\n"
                 "drops the single-key rows that mapped it. Full-width\n"
                 "punctuation belongs to the library's own layer, shared across\n"
                 "the Chinese engines; a table carrying punctuation entries of\n"
                 "its own — the cangjie and quick tables of ibus-table-chinese\n"
                 "1.8.9 and later do — would otherwise capture those keys as\n"
                 "composition input. Characters a table genuinely spells codes\n"
                 "with, like stroke5's `,./`, are detected and kept.\n"
                 "\n"
                 "Wildcard derivation declares `z` as the single-character\n"
                 "wildcard — Apple's Cangjie convention, for a decomposition the\n"
                 "user cannot fully recall — but only for a table that declares no\n"
                 "wildcard of its own and never uses `z` after the first key. It is\n"
                 "checked against the rows being compiled rather than assumed,\n"
                 "because several ibus-table-chinese tables do use `z` inside a\n"
                 "code and would be broken by it.\n",
                 program);
    return 2;
}

}  // namespace

int main(int argc, char **argv)
{
    std::string source_path;
    std::string output_path;
    std::string freq_path;
    long long threshold = 1000;
    bool glyph_filter = true;
    bool strip_punctuation = true;
    char wildcard_key = 'z';

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (std::strcmp(arg, "--freq-from") == 0 && i + 1 < argc) {
            freq_path = argv[++i];
        } else if (std::strcmp(arg, "--freq-threshold") == 0 && i + 1 < argc) {
            threshold = std::strtoll(argv[++i], nullptr, 10);
        } else if (std::strcmp(arg, "--no-glyph-filter") == 0) {
            glyph_filter = false;
        } else if (std::strcmp(arg, "--no-derive-wildcard") == 0) {
            wildcard_key = '\0';
        } else if (std::strcmp(arg, "--keep-punctuation") == 0) {
            strip_punctuation = false;
        } else if (arg[0] == '-') {
            return usage(argv[0]);
        } else if (source_path.empty()) {
            source_path = arg;
        } else if (output_path.empty()) {
            output_path = arg;
        } else {
            return usage(argv[0]);
        }
    }

    if (source_path.empty() || output_path.empty()) {
        return usage(argv[0]);
    }

    pathime::table::TableSource source;
    std::string error;
    if (!pathime::table::parse_table_source_file(source_path, &source, &error)) {
        std::fprintf(stderr, "%s: %s\n", source_path.c_str(), error.c_str());
        return 1;
    }

    /*
     * First, before any other transformation reads the rows: a punctuation row
     * that will not ship should not receive a transferred frequency or hold a
     * character the wildcard derivation trips over.
     */
    size_t punctuation_rows = 0;
    if (strip_punctuation) {
        punctuation_rows = pathime::table::strip_punctuation_keys(&source);
    }

    if (!freq_path.empty()) {
        pathime::table::TableSource frequencies;
        if (!pathime::table::parse_table_source_file(freq_path, &frequencies, &error)) {
            std::fprintf(stderr, "%s: %s\n", freq_path.c_str(), error.c_str());
            return 1;
        }
        pathime::table::apply_frequency_transfer(&source, frequencies,
                                                 static_cast<int64_t>(threshold));
    }

    /*
     * Before goucima derivation, which reads the phrase rows: a dropped phrase
     * must not leave a word-formation code behind pointing at a character the
     * table no longer offers.
     */
    size_t uncovered = 0;
    if (glyph_filter) {
        uncovered = pathime::table::apply_coverage_filter(&source);
    }

    /*
     * The `Z` wildcard, where the table leaves room for it. Derived from the
     * rows rather than defaulted, because `z` is only free in some tables;
     * derive_single_wildcard() has the survey and the reasoning.
     */
    bool derived_wildcard = false;
    if (wildcard_key != '\0') {
        derived_wildcard = pathime::table::derive_single_wildcard(&source, wildcard_key);
    }

    /*
     * Goucima are derived only for a table that declares USER_CAN_DEFINE_PHRASE,
     * because that declaration is also what decides whether the compiled
     * database carries a goucima table at all (§4.3).
     */
    if (source.properties.user_can_define_phrase) {
        pathime::table::derive_goucima(&source);
    }

    if (!pathime::table::compile_table(source, output_path, &error)) {
        std::fprintf(stderr, "%s: %s\n", output_path.c_str(), error.c_str());
        return 1;
    }

    std::printf("%s: %zu phrases, %zu goucima", output_path.c_str(),
                source.phrases.size(), source.goucima.size());
    if (glyph_filter) {
        /*
         * The map is named, not just the count. A table trimmed by 55% and one
         * trimmed by 0% are both correct answers depending on which map ran, and
         * a build log that reported only the number would leave the reader
         * unable to tell a policy change from a data problem.
         */
        std::printf(", %zu unrenderable dropped (%s)", uncovered,
                    pathime::table::coverage_map_name());
    } else {
        std::printf(", unfiltered");
    }
    if (derived_wildcard) {
        std::printf(", '%c' wildcard", wildcard_key);
    }
    if (punctuation_rows != 0) {
        std::printf(", %zu punctuation rows stripped", punctuation_rows);
    }
    if (source.skipped_rows != 0) {
        std::printf(", %zu malformed rows skipped", source.skipped_rows);
    }
    std::printf("\n");
    return 0;
}
