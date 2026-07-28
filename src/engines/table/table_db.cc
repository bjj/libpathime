#include "engines/table/table_db.h"

#include <sqlite3.h>

#include <algorithm>
#include <cstdio>
#include <sstream>

#include "paths.h"
#include "utf8.h"

namespace pathime {
namespace table {

namespace {

/**
 * The pragmas of §13.3, applied to every connection this file opens.
 *
 * `case_sensitive_like` is the load-bearing one and the reason it cannot be
 * left to SQLite's default: tabkeys are case-sensitive (§3.2), and several
 * tables use upper and lower case as different key strokes. Under the default
 * LIKE, typing `a` would match a row keyed `A`.
 */
const char *const kPragmas =
    "PRAGMA encoding = \"UTF-8\";"
    "PRAGMA case_sensitive_like = true;"
    "PRAGMA page_size = 4096;"
    "PRAGMA cache_size = 20000;"
    "PRAGMA temp_store = MEMORY;"
    "PRAGMA synchronous = NORMAL;"
    "PRAGMA busy_timeout = 5000;";

bool exec(sqlite3 *db, const char *sql, std::string *error)
{
    char *message = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &message) == SQLITE_OK) {
        return true;
    }
    if (error != nullptr) {
        *error = (message != nullptr) ? message : sqlite3_errmsg(db);
    }
    sqlite3_free(message);
    return false;
}

/**
 * A `file:` URI for @a path with the given mode, which is how each attached
 * database states its own access rights independently of the connection's.
 *
 * The characters SQLite gives meaning to inside a URI are percent-encoded:
 * `?` would begin the query part, `#` a fragment, and `%` an escape. Everything
 * else — spaces, non-ASCII, the rest — is carried through, which the
 * resource-directory arrangement requires. Backslashes become forward slashes
 * because a URI path separator is `/` on every platform, including Windows,
 * where SQLite accepts a drive letter after it.
 */
std::string uri_for(const std::string &path, const char *mode)
{
    std::string out = "file:";

    /* An absolute Windows path (C:\...) needs a leading slash in a URI. */
    if (path.size() > 1 && path[1] == ':') {
        out += '/';
    }

    for (const char c : path) {
        switch (c) {
        case '?':
            out += "%3f";
            break;
        case '#':
            out += "%23";
            break;
        case '%':
            out += "%25";
            break;
        case '\\':
            out += '/';
            break;
        default:
            out.push_back(c);
            break;
        }
    }

    out += "?mode=";
    out += mode;
    return out;
}

/** Everything before the last separator, or "" when there is none. */
std::string parent_directory(const std::string &path)
{
    const size_t at = path.find_last_of("/\\");
    return (at == std::string::npos) ? std::string() : path.substr(0, at);
}

/** A SQL string literal, single quotes doubled. */
std::string quote(const std::string &value)
{
    std::string out = "'";
    for (const char c : value) {
        if (c == '\'') {
            out += "''";
        }
        out.push_back(c);
    }
    out += "'";
    return out;
}

std::string to_text(sqlite3_stmt *statement, int column)
{
    const unsigned char *bytes = sqlite3_column_text(statement, column);
    if (bytes == nullptr) {
        return std::string();
    }
    return std::string(reinterpret_cast<const char *>(bytes),
                       static_cast<size_t>(sqlite3_column_bytes(statement, column)));
}

/**
 * Whether @a sql yields at least one row.
 *
 * A statement that will not even prepare answers false rather than raising,
 * because the only caller is asking about tables that are optional in the
 * schema (§4.4): a database that never declared pinyin mode has no `pinyin`
 * table at all, and "no such table" is one of the answers meaning "no data".
 */
bool table_has_rows(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
        return false;
    }
    const bool any = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return any;
}

/**
 * The char_prompts map as ibus-table stores it: a Python dict literal in the
 * `ime` table (§3.4). Parsed rather than ignored because the prompts are
 * preedit *content* for Cangjie and Stroke5 — a user typing `a` sees 日 — so a
 * database compiled by ibus-table would otherwise show raw latin keys.
 *
 * Deliberately a small hand-rolled reader for the one shape that occurs,
 * `{'a': '日', 'b': '月'}`, rather than anything general: the value is written
 * by repr() over a dict of single-character strings, so quotes are the only
 * structure and a backslash escape has never appeared in a shipped table.
 */
void parse_char_prompts(const std::string &literal, TableProperties *properties)
{
    std::vector<std::string> strings;
    size_t i = 0;
    while (i < literal.size()) {
        const char c = literal[i];
        if (c != '\'' && c != '"') {
            ++i;
            continue;
        }
        const size_t end = literal.find(c, i + 1);
        if (end == std::string::npos) {
            break;
        }
        strings.push_back(literal.substr(i + 1, end - i - 1));
        i = end + 1;
    }

    for (size_t pair = 0; pair + 1 < strings.size(); pair += 2) {
        const std::string &key = strings[pair];
        const std::string &prompt = strings[pair + 1];
        size_t offset = 0;
        uint32_t scalar = 0;
        if (prompt.empty() ||
            !utf8_next_scalar(key.data(), key.size(), &offset, &scalar)) {
            continue;
        }
        properties->char_prompts[static_cast<char32_t>(scalar)] = prompt;
    }
}

std::string char_prompts_literal(const TableProperties &properties)
{
    std::ostringstream out;
    out << "{";
    bool first = true;
    for (const auto &entry : properties.char_prompts) {
        std::string key;
        if (!utf8_append_scalar(key, static_cast<uint32_t>(entry.first))) {
            continue;
        }
        if (!first) {
            out << ", ";
        }
        first = false;
        out << "'" << key << "': '" << entry.second << "'";
    }
    out << "}";
    return out.str();
}

}  // namespace

LikePattern build_like_pattern(const TableProperties &properties, const std::string &keys)
{
    LikePattern out;

    /*
     * Step 0 of §8.1, implied by the rest: the escape character must not be one
     * the user can type as a wildcard, or escaping would be indistinguishable
     * from matching.
     */
    const char *const candidates = "!@#";
    for (const char *c = candidates; *c != '\0'; ++c) {
        const std::string encoded(1, *c);
        if (encoded != properties.single_wildcard && encoded != properties.multi_wildcard) {
            out.escape = *c;
            break;
        }
    }

    size_t offset = 0;
    uint32_t scalar = 0;
    while (utf8_next_scalar(keys.data(), keys.size(), &offset, &scalar)) {
        std::string encoded;
        utf8_append_scalar(encoded, scalar);

        if (!properties.single_wildcard.empty() && encoded == properties.single_wildcard) {
            out.pattern += '_';
            continue;
        }
        if (!properties.multi_wildcard.empty() && encoded == properties.multi_wildcard) {
            out.pattern += '%';
            continue;
        }

        /*
         * Everything else is literal, so the three characters SQL LIKE gives
         * meaning to are escaped: the escape character itself (doubled), and
         * `%` and `_` where the table has not claimed them as wildcards.
         */
        if (encoded.size() == 1 &&
            (encoded[0] == out.escape || encoded[0] == '%' || encoded[0] == '_')) {
            out.pattern += out.escape;
        }
        out.pattern += encoded;
    }

    if (properties.auto_wildcard) {
        out.pattern += '%';
    }

    return out;
}

bool compile_table(const TableSource &source, const std::string &path, std::string *error)
{
    std::remove(path.c_str());

    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) != SQLITE_OK) {
        if (error != nullptr) {
            *error = (db != nullptr) ? sqlite3_errmsg(db) : "cannot create " + path;
        }
        sqlite3_close(db);
        return false;
    }

    bool ok = exec(db, kPragmas, error);

    if (ok) {
        ok = exec(db,
                  "CREATE TABLE IF NOT EXISTS main.ime (attr TEXT, val TEXT);"
                  "CREATE TABLE IF NOT EXISTS main.phrases "
                  "(id INTEGER PRIMARY KEY, tabkeys TEXT, phrase TEXT, "
                  " freq INTEGER, user_freq INTEGER);",
                  error);
    }
    if (ok && source.properties.user_can_define_phrase) {
        ok = exec(db,
                  "CREATE TABLE IF NOT EXISTS main.goucima "
                  "(zi TEXT PRIMARY KEY, goucima TEXT);",
                  error);
    }
    if (ok && source.properties.pinyin_mode) {
        ok = exec(db,
                  "CREATE TABLE IF NOT EXISTS main.pinyin "
                  "(pinyin TEXT, zi TEXT, freq INTEGER);",
                  error);
    }
    if (ok && source.properties.suggestion_mode) {
        ok = exec(db,
                  "CREATE TABLE IF NOT EXISTS main.suggestion "
                  "(phrase TEXT, freq INTEGER);",
                  error);
    }

    if (ok) {
        ok = exec(db, "BEGIN TRANSACTION;", error);
    }

    /* ---- ime ---- */
    if (ok) {
        sqlite3_stmt *statement = nullptr;
        if (sqlite3_prepare_v2(db, "INSERT INTO main.ime (attr, val) VALUES (?, ?);", -1,
                               &statement, nullptr) != SQLITE_OK) {
            if (error != nullptr) {
                *error = sqlite3_errmsg(db);
            }
            ok = false;
        } else {
            std::map<std::string, std::string> attrs = source.properties.attrs;
            if (!source.properties.char_prompts.empty()) {
                attrs["char_prompts"] = char_prompts_literal(source.properties);
            }
            for (const auto &entry : attrs) {
                sqlite3_reset(statement);
                sqlite3_bind_text(statement, 1, entry.first.c_str(),
                                  static_cast<int>(entry.first.size()), SQLITE_STATIC);
                sqlite3_bind_text(statement, 2, entry.second.c_str(),
                                  static_cast<int>(entry.second.size()), SQLITE_STATIC);
                if (sqlite3_step(statement) != SQLITE_DONE) {
                    if (error != nullptr) {
                        *error = sqlite3_errmsg(db);
                    }
                    ok = false;
                    break;
                }
            }
            sqlite3_finalize(statement);
        }
    }

    /* ---- phrases ---- */
    if (ok) {
        /*
         * "Rows are stored sorted by tabkeys ASC, phrase ASC, user_freq DESC,
         * freq DESC, id ASC" (§4.2). The row *order* is part of the contract
         * because no index is created — ibus-table found indexes doubled the
         * file size without helping — so a scan meets the rows in a useful
         * order. user_freq is 0 for every row in a system database, so the sort
         * reduces to tabkeys, phrase, then freq descending.
         */
        std::vector<const PhraseRow *> ordered;
        ordered.reserve(source.phrases.size());
        for (const PhraseRow &row : source.phrases) {
            ordered.push_back(&row);
        }
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const PhraseRow *a, const PhraseRow *b) {
                             if (a->tabkeys != b->tabkeys) {
                                 return a->tabkeys < b->tabkeys;
                             }
                             if (a->phrase != b->phrase) {
                                 return a->phrase < b->phrase;
                             }
                             return a->freq > b->freq;
                         });

        sqlite3_stmt *statement = nullptr;
        if (sqlite3_prepare_v2(db,
                               "INSERT INTO main.phrases (tabkeys, phrase, freq, user_freq) "
                               "VALUES (?, ?, ?, 0);",
                               -1, &statement, nullptr) != SQLITE_OK) {
            if (error != nullptr) {
                *error = sqlite3_errmsg(db);
            }
            ok = false;
        } else {
            for (const PhraseRow *row : ordered) {
                sqlite3_reset(statement);
                sqlite3_bind_text(statement, 1, row->tabkeys.c_str(),
                                  static_cast<int>(row->tabkeys.size()), SQLITE_STATIC);
                sqlite3_bind_text(statement, 2, row->phrase.c_str(),
                                  static_cast<int>(row->phrase.size()), SQLITE_STATIC);
                sqlite3_bind_int64(statement, 3, row->freq);
                if (sqlite3_step(statement) != SQLITE_DONE) {
                    if (error != nullptr) {
                        *error = sqlite3_errmsg(db);
                    }
                    ok = false;
                    break;
                }
            }
            sqlite3_finalize(statement);
        }
    }

    /* ---- goucima ---- */
    if (ok && source.properties.user_can_define_phrase) {
        sqlite3_stmt *statement = nullptr;
        if (sqlite3_prepare_v2(db,
                               "INSERT OR REPLACE INTO main.goucima (zi, goucima) VALUES (?, ?);",
                               -1, &statement, nullptr) != SQLITE_OK) {
            if (error != nullptr) {
                *error = sqlite3_errmsg(db);
            }
            ok = false;
        } else {
            for (const auto &entry : source.goucima) {
                sqlite3_reset(statement);
                sqlite3_bind_text(statement, 1, entry.first.c_str(),
                                  static_cast<int>(entry.first.size()), SQLITE_STATIC);
                sqlite3_bind_text(statement, 2, entry.second.c_str(),
                                  static_cast<int>(entry.second.size()), SQLITE_STATIC);
                if (sqlite3_step(statement) != SQLITE_DONE) {
                    if (error != nullptr) {
                        *error = sqlite3_errmsg(db);
                    }
                    ok = false;
                    break;
                }
            }
            sqlite3_finalize(statement);
        }
    }

    if (ok) {
        ok = exec(db, "COMMIT;", error);
    } else {
        exec(db, "ROLLBACK;", nullptr);
    }

    sqlite3_close(db);
    if (!ok) {
        std::remove(path.c_str());
    }
    return ok;
}

std::unique_ptr<TableDatabase> TableDatabase::open(const std::string &system_path,
                                                   const std::string &user_path,
                                                   std::string *error)
{
    std::unique_ptr<TableDatabase> table(new TableDatabase());

    /*
     * The connection's own main database is `:memory:` and holds nothing. Both
     * real databases are attached beside it, which is the only arrangement that
     * gets the access rights right in every case.
     *
     * The obvious alternative — open the system table as main and ATTACH the
     * user database — does not work, and the failure is silent rather than
     * loud. SQLite opens an attached file with the *main* connection's flags,
     * so a system table opened READONLY attaches a read-only user database, and
     * one that does not exist yet cannot be created at all. Opening the system
     * table read-write instead is worse: an installed table under a system
     * prefix is not writable, so that fails outright for the ordinary case.
     *
     * With an empty main, each file carries its own access mode in a URI, and
     * the read-only system table and the read-write user database coexist on
     * one connection — which is what the merged lookup of §8.1 needs, since a
     * UNION cannot span two connections.
     */
    if (sqlite3_open_v2(":memory:", &table->db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI,
                        nullptr) != SQLITE_OK) {
        if (error != nullptr) {
            *error = (table->db_ != nullptr) ? sqlite3_errmsg(table->db_)
                                             : "cannot open a database connection";
        }
        return nullptr;
    }

    if (!exec(table->db_, kPragmas, error)) {
        return nullptr;
    }

    const std::string attach_system =
        "ATTACH DATABASE " + quote(uri_for(system_path, "ro")) + " AS system;";
    if (!exec(table->db_, attach_system.c_str(), error)) {
        return nullptr;
    }

    if (!table->read_properties(error)) {
        return nullptr;
    }

    if (!user_path.empty()) {
        /*
         * A user database that cannot be created or attached is not fatal. The
         * table still types; only learning is lost, and reporting the whole
         * input method unavailable because a home directory is read-only would
         * be the wrong trade.
         */
        make_directories(parent_directory(user_path));

        const std::string attach_user =
            "ATTACH DATABASE " + quote(uri_for(user_path, "rwc")) + " AS user_db;";
        if (exec(table->db_, attach_user.c_str(), nullptr) &&
            exec(table->db_,
                 "PRAGMA user_db.journal_mode = WAL;"
                 "CREATE TABLE IF NOT EXISTS user_db.phrases "
                 "(id INTEGER PRIMARY KEY, tabkeys TEXT, phrase TEXT, "
                 " freq INTEGER, user_freq INTEGER);"
                 "CREATE TABLE IF NOT EXISTS user_db.desc (name PRIMARY KEY, value);"
                 "INSERT OR IGNORE INTO user_db.desc (name, value) VALUES ('version', '1.00');",
                 nullptr)) {
            table->has_user_db_ = true;
        }
    }

    return table;
}

TableDatabase::~TableDatabase()
{
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
}

bool TableDatabase::read_properties(std::string *error)
{
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT attr, val FROM system.ime;", -1, &statement, nullptr) !=
        SQLITE_OK) {
        if (error != nullptr) {
            *error = "not an ibus-table database: no ime table";
        }
        return false;
    }

    std::string char_prompts;
    bool any = false;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const std::string attr = to_text(statement, 0);
        const std::string value = to_text(statement, 1);
        any = true;
        if (attr == "char_prompts") {
            char_prompts = value;
        } else {
            properties_.set(attr, value);
        }
    }
    sqlite3_finalize(statement);

    if (!any) {
        if (error != nullptr) {
            *error = "not an ibus-table database: empty ime table";
        }
        return false;
    }

    if (!char_prompts.empty()) {
        parse_char_prompts(char_prompts, &properties_);
    }
    properties_.finalize();

    /*
     * Whether the optional tables were populated, as opposed to merely declared.
     * TableProperties::pinyin_data carries the reasoning; the short version is
     * that every table this library ships declares PINYIN_MODE and none of them
     * has any pinyin rows, so the declaration on its own would make the option
     * read as enabled while doing nothing.
     *
     * `LIMIT 1` rather than a count: the question is emptiness, and the pinyin
     * table of a real ibus-table database has tens of thousands of rows.
     */
    properties_.pinyin_data =
        properties_.pinyin_mode && table_has_rows(db_, "SELECT 1 FROM system.pinyin LIMIT 1;");
    properties_.suggestion_data =
        properties_.suggestion_mode &&
        table_has_rows(db_, "SELECT 1 FROM system.suggestion LIMIT 1;");
    return true;
}

bool TableDatabase::lookup(const std::string &keys, std::vector<PhraseMatch> *out) const
{
    out->clear();

    const LikePattern pattern = build_like_pattern(properties_, keys);

    /*
     * The user rows are unioned in only when the table has something to learn
     * (§8.1). A table with neither declaration never writes user rows, so the
     * union would scan an empty table on every keystroke.
     */
    const bool merge_user = has_user_db_ && (properties_.user_can_define_phrase ||
                                             properties_.dynamic_adjust);

    std::string sql =
        "SELECT tabkeys, phrase, freq, user_freq FROM system.phrases "
        "WHERE tabkeys LIKE ?1 ESCAPE ?2";
    if (merge_user) {
        sql +=
            " UNION ALL "
            "SELECT tabkeys, phrase, freq, user_freq FROM user_db.phrases "
            "WHERE tabkeys LIKE ?1 ESCAPE ?2";
    }
    sql += ";";

    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
        return false;
    }

    const std::string escape(1, pattern.escape);
    sqlite3_bind_text(statement, 1, pattern.pattern.c_str(),
                      static_cast<int>(pattern.pattern.size()), SQLITE_STATIC);
    sqlite3_bind_text(statement, 2, escape.c_str(), 1, SQLITE_STATIC);

    /*
     * Merge duplicates as they arrive rather than afterwards: a (tabkeys,
     * phrase) in both databases becomes one row taking the max of each
     * frequency (§8.1). The index map is keyed on the pair, which is what makes
     * "the same phrase" mean the same thing here as it does to the user.
     */
    std::map<std::pair<std::string, std::string>, size_t> seen;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        PhraseMatch match;
        match.tabkeys = to_text(statement, 0);
        match.phrase = to_text(statement, 1);
        match.freq = sqlite3_column_int64(statement, 2);
        match.user_freq = sqlite3_column_int64(statement, 3);

        const auto key = std::make_pair(match.tabkeys, match.phrase);
        const auto existing = seen.find(key);
        if (existing == seen.end()) {
            seen[key] = out->size();
            out->push_back(match);
        } else {
            PhraseMatch &kept = (*out)[existing->second];
            kept.freq = std::max(kept.freq, match.freq);
            kept.user_freq = std::max(kept.user_freq, match.user_freq);
        }
    }

    sqlite3_finalize(statement);
    return true;
}

bool TableDatabase::record_selection(const std::string &tabkeys, const std::string &phrase)
{
    if (!has_user_db_ || tabkeys.empty() || phrase.empty()) {
        return false;
    }

    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "UPDATE user_db.phrases SET user_freq = user_freq + 1 "
                           "WHERE tabkeys = ?1 AND phrase = ?2;",
                           -1, &statement, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(statement, 1, tabkeys.c_str(), static_cast<int>(tabkeys.size()),
                      SQLITE_STATIC);
    sqlite3_bind_text(statement, 2, phrase.c_str(), static_cast<int>(phrase.size()),
                      SQLITE_STATIC);
    const bool updated = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);

    if (updated && sqlite3_changes(db_) > 0) {
        return true;
    }

    if (sqlite3_prepare_v2(db_,
                           "INSERT INTO user_db.phrases (tabkeys, phrase, freq, user_freq) "
                           "VALUES (?1, ?2, 0, 1);",
                           -1, &statement, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(statement, 1, tabkeys.c_str(), static_cast<int>(tabkeys.size()),
                      SQLITE_STATIC);
    sqlite3_bind_text(statement, 2, phrase.c_str(), static_cast<int>(phrase.size()),
                      SQLITE_STATIC);
    const bool inserted = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return inserted;
}

std::string TableDatabase::goucima(const std::string &character) const
{
    if (!properties_.user_can_define_phrase) {
        return std::string();
    }

    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT goucima FROM system.goucima WHERE zi = ?;", -1,
                           &statement, nullptr) != SQLITE_OK) {
        return std::string();
    }
    sqlite3_bind_text(statement, 1, character.c_str(), static_cast<int>(character.size()),
                      SQLITE_STATIC);

    std::string out;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        out = to_text(statement, 0);
    }
    sqlite3_finalize(statement);
    return out;
}

}  // namespace table
}  // namespace pathime
