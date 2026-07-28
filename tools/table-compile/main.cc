/*
 * pathime-table-compile — turn an ibus-table source `.txt` into the compiled
 * SQLite `.db` of docs/ibus-table-spec.md §4.
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
                 "\n"
                 "Frequency transfer takes usage-ranked data from one table and\n"
                 "applies it to a table that sorts structurally, so a partially\n"
                 "typed code offers common characters first. Entries at or below\n"
                 "the threshold are left alone, which preserves each table's own\n"
                 "deliberate ordering of its top choices.\n"
                 "\n"
                 "Glyph filtering is the other half of the same purpose and is on\n"
                 "by default: entries whose characters fall outside the generated\n"
                 "coverage map are dropped, so a mistyped code cannot fill the\n"
                 "candidate list with tofu. The map comes from a deliberately\n"
                 "inclusive CJK font and is checked in rather than read at build\n"
                 "time, which is what keeps a compiled .db reproducible; see\n"
                 "tools/generate-coverage.py. --no-glyph-filter turns it off for\n"
                 "anyone who would rather have the entries than the guarantee.\n"
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
    if (uncovered != 0) {
        std::printf(", %zu unrenderable dropped", uncovered);
    }
    if (derived_wildcard) {
        std::printf(", '%c' wildcard", wildcard_key);
    }
    if (source.skipped_rows != 0) {
        std::printf(", %zu malformed rows skipped", source.skipped_rows);
    }
    std::printf("\n");
    return 0;
}
