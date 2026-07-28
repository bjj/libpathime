/*
 * The source `.txt` table format of docs/ibus-table-mapping.md §3, parsed.
 *
 * This is half the data contract: `ibus-table-chinese` distributes source text
 * only, so every table this library ships passes through here on its way to the
 * compiled database of table_db.h. The other direction — reading a `.db` that
 * ibus-table itself compiled — needs none of this file.
 *
 * Nothing here touches SQLite or the composition model. It reads text and
 * produces the three things a compilation needs: the declaration
 * (table_properties.h), the phrase rows, and the word-formation codes.
 */

#ifndef LIBPATHIME_SRC_ENGINES_TABLE_TABLE_SOURCE_H
#define LIBPATHIME_SRC_ENGINES_TABLE_TABLE_SOURCE_H

#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "engines/table/table_properties.h"

namespace pathime {
namespace table {

/** One `BEGIN_TABLE` row: a key sequence, what it produces, and its rank. */
struct PhraseRow {
    std::string tabkeys;
    std::string phrase;
    int64_t freq = 0;
};

/** A parsed source file. */
struct TableSource {
    TableProperties properties;

    /** TABLE and TABLE_EXTRA merged, in file order (§3.2). */
    std::vector<PhraseRow> phrases;

    /**
     * GOUCI, keyed by the character (§3.3). Empty when the section is absent,
     * in which case derive_goucima() fills it from the phrase rows.
     */
    std::map<std::string, std::string> goucima;

    /**
     * Table rows that were not three tab-separated columns and were skipped.
     *
     * Not zero for the tables this library ships, which is why skipping is the
     * behaviour rather than failing: `stroke5.txt` carries a stray `%chardef
     * end` marker inside its table section, and `cantonese.txt` has one row
     * whose first separator is a space instead of a tab. ibus-table loads both
     * tables, so a parser that refused them would be stricter than the format
     * actually is. Counted rather than ignored so that a table falling apart
     * shows up as a number instead of as missing candidates.
     */
    size_t skipped_rows = 0;
};

/**
 * Parse a source table. Returns false only for input that cannot be read as the
 * format at all — a missing END marker, a table row without its three columns.
 * A declaration key this engine has no meaning for is kept and ignored, never
 * an error, because a table may legitimately carry fields for other consumers.
 *
 * @a error, when non-null, receives a one-line description naming the line
 * number, for the compile tool to print.
 */
bool parse_table_source(std::istream &in, TableSource *out, std::string *error);

/** The same for a file, opened in binary mode so no line ending is rewritten. */
bool parse_table_source_file(const std::string &path, TableSource *out, std::string *error);

/**
 * Fill in goucima for every single-character phrase that has none, using "the
 * longest tabkeys sequence that produces exactly that character" (§3.3).
 *
 * Only meaningful when the table declares USER_CAN_DEFINE_PHRASE; a compilation
 * of a table that does not skips this and writes no goucima table.
 */
void derive_goucima(TableSource *source);

/**
 * Declare @a key as the table's single-character wildcard, if the table declares
 * no wildcard of its own and @a key is never used in a non-initial position.
 * Returns true if the declaration was made.
 *
 * This gives Cangjie the `Z` wildcard that Apple's implementation made familiar:
 * `Z` stands for a part of a decomposition the user cannot remember, so `hz` or
 * `hqz` finds 我 without knowing the whole code. It is a real ergonomic win on a
 * method whose difficulty is precisely recalling decompositions.
 *
 * ---------------------------------------------------------------------------
 * Why derived per table rather than defaulted once
 * ---------------------------------------------------------------------------
 *
 * Because `z` is *not* free everywhere, and one of the tables this library
 * already ships is a case in point. Across ibus-table-chinese, erbi, scj6, yong,
 * easy-big, wu, cantonhk and even cangjie3 and cangjie-big use `z` inside a
 * code, where making it a wildcard would shadow real entries — and so does
 * **wubi-jidian86**, whose 678 `z`-prefixed punctuation codes are spelled `zzbd`
 * and friends, putting a second `z` in position 1. A tier-4 default in
 * options.cc would apply to all of them and break them silently, and would keep
 * breaking them as tables are added.
 *
 * Where it *is* free: cangjie5 (496 `z` rows, every one of them leading),
 * quick5 (494) and zhuyin (744) never put `z` after the first key, so they get
 * the wildcard. stroke5 declares `?` and `*` of its own and keeps them.
 * wubi-jidian86 is declined by the check, which is the check earning its place
 * rather than a hypothetical.
 *
 * The leading occurrences survive too, because a wildcard that is also one of
 * the table's own input characters is only a wildcard in non-initial position —
 * see TableProperties::is_wildcard_at(). Those 496 punctuation codes still work.
 *
 * The declaration is written into the compiled database under the format's own
 * `SINGLE_WILDCARD_CHAR` key rather than a private one, so the resulting `.db`
 * says what it means to any reader, ibus-table included.
 */
bool derive_single_wildcard(TableSource *source, char key);

/**
 * Rewrite @a target's frequencies from a second table's, for every entry
 * @a target itself ranks at or above @a threshold.
 *
 * This is the portable half of the preprocessing carried in the fork this
 * library's tables come from (bjj/ibus-table-chinese, commits d0f9849 and
 * cc4a17f): character frequency data taken from one table — Cantonese, which
 * ranks by usage — is transferred onto tables that sort by structure, so that
 * a partially typed code offers common characters first. Without it, Cangjie
 * and Quick offer their exact matches and then whatever the table's own order
 * happens to be.
 *
 * An entry at or above the threshold takes the frequency the second table gives
 * its phrase, *plus* the threshold; one the second table does not mention
 * contributes zero and so lands on the threshold exactly. Entries below the
 * threshold are left alone, which is what preserves each table's deliberate
 * manual ordering — and the addition is what keeps every rewritten entry above
 * every preserved one.
 *
 * The comparison is `>=`, which matters more than it looks: the tables set
 * their primary entry for each code to exactly the default threshold, so a
 * strict `>` transfers nothing.
 *
 * The fork's other half — dropping entries whose characters a target font cannot
 * display — is deliberately not here, though it does exist: coverage.h, applied
 * by tools/table-compile. It is kept out of this header because a decision about
 * a display target is not a decision about the table, and keeping it out is also
 * what stops the coverage map being linked into the library (coverage.h says
 * why).
 */
void apply_frequency_transfer(TableSource *target,
                              const TableSource &source,
                              int64_t threshold);

/*
 * The coverage filter — the other build-time transformation — is deliberately
 * not here. It lives in coverage.h, because it carries a generated table of
 * 2,000-odd ranges that only tools/table-compile has any use for, and this file
 * is linked into the library itself.
 */

}  // namespace table
}  // namespace pathime

#endif /* LIBPATHIME_SRC_ENGINES_TABLE_TABLE_SOURCE_H */
