/*
 * The source `.txt` table format of docs/ibus-table-spec.md §3, parsed.
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
 * Raise @a target's frequencies from a second table's, for the entries @a source
 * ranks above @a threshold.
 *
 * This is the portable half of the preprocessing carried in the fork this
 * library's tables come from (bjj/ibus-table-chinese, commits d0f9849 and
 * cc4a17f): character frequency data taken from one table — Cantonese, which
 * ranks by usage — is transferred onto tables that sort by structure, so that
 * a partially typed code offers common characters first. Without it, Cangjie
 * and Quick offer their exact matches and then whatever the table's own order
 * happens to be.
 *
 * Entries at or below @a threshold keep the frequency they were given, which is
 * what preserves each table's deliberate manual ordering of its top choices.
 *
 * The fork's other half — commenting out entries whose characters a target font
 * cannot display — is deliberately not here. It needs fontconfig and a specific
 * font, and it is a decision about a display target rather than about the table,
 * so it belongs to whoever is packaging for that target.
 */
void apply_frequency_transfer(TableSource *target,
                              const TableSource &source,
                              int64_t threshold);

}  // namespace table
}  // namespace pathime

#endif /* LIBPATHIME_SRC_ENGINES_TABLE_TABLE_SOURCE_H */
