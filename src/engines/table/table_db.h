/*
 * The compiled SQLite database of docs/ibus-table-mapping.md §4 and §5: creating
 * one from a parsed source, opening one ibus-table itself compiled, and running
 * the lookup of §8.1 against it.
 *
 * This is the interoperability half of the engine. The schema, the column
 * order, the row order and the pragmas are all contract rather than choice — a
 * `.db` this library writes has to be one ibus-table can read, and vice versa —
 * so everything here that looks arbitrary is transcribed from the spec and
 * should be changed only with it.
 *
 * Nothing in this file knows what a composition is. It answers questions about
 * phrases; the ordering that turns an answer into a candidate list is
 * ranking.h, and the state machine that decides what to ask is table_backend.cc.
 */

#ifndef LIBPATHIME_SRC_ENGINES_TABLE_TABLE_DB_H
#define LIBPATHIME_SRC_ENGINES_TABLE_TABLE_DB_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engines/table/table_properties.h"
#include "engines/table/table_source.h"

struct sqlite3;

namespace pathime {
namespace table {

/** One row of a lookup result, before ordering. */
struct PhraseMatch {
    std::string tabkeys;
    std::string phrase;
    int64_t freq = 0;
    int64_t user_freq = 0;
};

/**
 * The LIKE pattern a key run becomes (§8.1), together with the escape character
 * the query must use.
 *
 * The escape character is `!` unless the table configured `!` as one of its
 * wildcards, in which case `@` and then `#`. That fallback is the spec's, and it
 * matters for real tables: erbi uses punctuation as input characters.
 */
struct LikePattern {
    std::string pattern;
    char escape = '!';
};

/** Build the pattern for @a keys under @a properties. */
LikePattern build_like_pattern(const TableProperties &properties, const std::string &keys);

/**
 * Compile a parsed source into a new database at @a path, replacing any file
 * already there.
 *
 * Writes `ime` from the declaration, `phrases` from the rows in the order §4.2
 * fixes, and `goucima` when the table declares USER_CAN_DEFINE_PHRASE. The
 * `pinyin` and `suggestion` tables are created only when the table declares the
 * matching mode, and are left empty: their source data (`pinyin_table.txt`,
 * `phrase.txt`) ships with ibus-table rather than with the tables, and this
 * library does not carry it, so neither mode is implemented. Creating the
 * tables anyway keeps the schema identical to the one ibus-table writes, and
 * keeps the declaration and the data separate questions — which is the
 * distinction TableProperties::pinyin_data exists to draw.
 */
bool compile_table(const TableSource &source, const std::string &path, std::string *error);

/**
 * One loaded table: the system database, optionally with a user database
 * attached as schema `user_db`.
 *
 * Shared by every context naming the same table, which is what makes
 * PATHIME_OPT_TABLE_FILE a per-context option rather than an engine identity:
 * loading is expensive, so the engine caches these, and a context switching
 * tables pays only for a map lookup.
 */
class TableDatabase {
public:
    /**
     * Open @a system_path read-only and attach @a user_path, creating the user
     * database if it does not exist. An empty @a user_path attaches nothing,
     * which is what a build with learning off wants.
     *
     * Returns null on failure with @a error set. A system database that opens
     * but carries no `ime` table is a failure: it is not an ibus-table database.
     */
    static std::unique_ptr<TableDatabase> open(const std::string &system_path,
                                               const std::string &user_path,
                                               std::string *error);

    ~TableDatabase();

    TableDatabase(const TableDatabase &) = delete;
    TableDatabase &operator=(const TableDatabase &) = delete;

    const TableProperties &properties() const { return properties_; }

    /** True if a user database is attached and writable. */
    bool has_user_db() const { return has_user_db_; }

    /**
     * Every phrase matching @a keys, unordered.
     *
     * The union with `user_db.phrases` happens here when a user database is
     * attached, merging a `(tabkeys, phrase)` present in both by taking the max
     * of each frequency, exactly as §8.1 specifies. What comes back is the raw
     * result set; ranking.h puts it in candidate order.
     */
    bool lookup(const std::string &keys, std::vector<PhraseMatch> *out) const;

    /** The goucima for @a character, or "" when the table declares none. */
    std::string goucima(const std::string &character) const;

    /**
     * Record that the user chose @a phrase for @a tabkeys, bumping its
     * `user_freq` in the user database (§5.1).
     *
     * A pair that has no user row yet gets one with `user_freq = 1` and
     * `freq = 0` — zero rather than the system table's value, because §5.1 is
     * explicit that a user row shadowing a system row carries no frequency of
     * its own and the lookup merges by taking the max of each.
     *
     * Two statements rather than an UPSERT, because `user_db.phrases` has no
     * unique index on `(tabkeys, phrase)` to conflict on — the schema is fixed
     * by the data contract (§5.1) and adding an index to it would be a change
     * ibus-table did not make to a file both programs write.
     *
     * False when there is no user database, which is not an error: it is what a
     * build with no writable data directory looks like.
     */
    bool record_selection(const std::string &tabkeys, const std::string &phrase);

private:
    TableDatabase() = default;

    bool read_properties(std::string *error);

    sqlite3 *db_ = nullptr;
    bool has_user_db_ = false;
    TableProperties properties_;
};

}  // namespace table
}  // namespace pathime

#endif /* LIBPATHIME_SRC_ENGINES_TABLE_TABLE_DB_H */
