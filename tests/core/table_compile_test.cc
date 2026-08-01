/*
 * The table *compiler*: source text in, a SQLite database out, and the database
 * read back through the same reader the engine uses.
 *
 * The peer of core.table, and split from it on purpose. That test asserts on
 * parsed structures and opens nothing; this one writes files. Keeping the two
 * apart is what lets core.table stay honest about the layering it demonstrates
 * — the parser and the ranking know nothing about SQLite — while the compiler,
 * which is nothing but SQLite, still gets tested.
 *
 * Why this suite exists at all: compile_table() is compiled into both the
 * library and tools/table-compile, and the tool runs on every build to produce
 * the shipped tables. So the compiler is *executed* constantly and *asserted on*
 * nowhere — a build cannot fail on a table that compiles to the wrong thing, only
 * on one that crashes. Everything below is a property the build could not have
 * caught.
 *
 * The round trip is the point. compile_table() writing something
 * TableDatabase::open() then reads back identically is worth more than either
 * half checked alone, because the format is an interoperability contract: the
 * schema, the column order and the row order all have to match what ibus-table
 * writes, and a matched pair of bugs on both sides of our own code would be
 * invisible to any test that only went one way.
 */

#include <sqlite3.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "engines/table/table_db.h"
#include "engines/table/table_properties.h"
#include "engines/table/table_source.h"

#include "core_test_util.h"

using namespace pathime;
using namespace pathime::table;

namespace {

/* A directory this build made for the purpose; CMake passes the path in. */
std::string temp_path(const char *name)
{
    return std::string(PATHIME_TABLE_COMPILE_TEST_DIR) + "/" + name;
}

TableSource parse(const std::string &text)
{
    std::istringstream in(text);
    TableSource source;
    std::string error;
    PT_CHECK(parse_table_source(in, &source, &error));
    return source;
}

/* Compile @a text to a fresh file under the temp directory, asserting success. */
std::string compile(const std::string &text, const char *name)
{
    const std::string path = temp_path(name);
    const TableSource source = parse(text);
    std::string error;
    if (!compile_table(source, path, &error)) {
        PT_FAILF("compile_table(%s) failed: %s", name, error.c_str());
    }
    return path;
}

/* Every row of a compiled database's `phrases`, in stored order. */
std::vector<PhraseRow> stored_rows(const std::string &path)
{
    std::vector<PhraseRow> rows;
    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        PT_FAILF("cannot open %s", path.c_str());
        sqlite3_close(db);
        return rows;
    }
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT tabkeys, phrase, freq FROM phrases ORDER BY id;", -1,
                           &statement, nullptr) == SQLITE_OK) {
        while (sqlite3_step(statement) == SQLITE_ROW) {
            PhraseRow row;
            row.tabkeys = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
            row.phrase = reinterpret_cast<const char *>(sqlite3_column_text(statement, 1));
            row.freq = sqlite3_column_int64(statement, 2);
            rows.push_back(row);
        }
        sqlite3_finalize(statement);
    }
    sqlite3_close(db);
    return rows;
}

/* True if @a path holds a table named @a name. */
bool has_sql_table(const std::string &path, const char *name)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }
    sqlite3_stmt *statement = nullptr;
    bool found = false;
    if (sqlite3_prepare_v2(db,
                           "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?;",
                           -1, &statement, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(statement, 1, name, -1, SQLITE_STATIC);
        found = sqlite3_step(statement) == SQLITE_ROW;
        sqlite3_finalize(statement);
    }
    sqlite3_close(db);
    return found;
}

/* A database holding exactly @a sql and nothing else. */
bool make_raw_db(const std::string &path, const char *sql)
{
    std::remove(path.c_str());
    sqlite3 *db = nullptr;
    bool ok = false;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) == SQLITE_OK) {
        ok = sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    }
    sqlite3_close(db);
    return ok;
}

bool file_exists(const std::string &path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    return in.is_open();
}

/* The minimal example of docs/ibus-table-mapping.md, with exact-match lookup. */
const char *const kBasicTable =
    "BEGIN_DEFINITION\n"
    "NAME = Example\n"
    "SERIAL_NUMBER = 20240101\n"
    "LANGUAGES = en\n"
    "VALID_INPUT_CHARS = abc\n"
    "MAX_KEY_LENGTH = 2\n"
    "AUTO_WILDCARD = FALSE\n"
    "AUTO_COMMIT = FALSE\n"
    "END_DEFINITION\n"
    "BEGIN_TABLE\n"
    "a\t\xCE\xB1\t1000\n"
    "b\t\xCE\xB2\t1000\n"
    "c\t\xCE\xB3\t1000\n"
    "ab\t\xCE\xB1\xCE\xB2\t500\n"
    "END_TABLE\n";

/*
 * Compile, reopen, and check that the declaration and the rows survived. The
 * declaration is the interesting half: it goes out as `ime` rows keyed by the
 * lowercased attribute name and comes back through the same TableProperties::set()
 * a source file feeds, so a mismatch between the two spellings would show here
 * and nowhere else.
 */
void test_compile_and_reopen()
{
    const std::string path = compile(kBasicTable, "basic.db");

    std::string error;
    std::unique_ptr<TableDatabase> table = TableDatabase::open(path, "", &error);
    if (table == nullptr) {
        PT_FAILF("open failed: %s", error.c_str());
        return;
    }

    PT_CHECK_STR(table->properties().name, "Example");
    PT_CHECK(table->properties().serial_number == 20240101u);
    PT_CHECK(table->properties().max_key_length == 2);
    PT_CHECK(!table->properties().auto_commit);
    PT_CHECK(!table->properties().auto_wildcard);

    /* No user path was given, so nothing is attached and nothing is learned. */
    PT_CHECK(!table->has_user_db());

    /* AUTO_WILDCARD = FALSE, so a key run matches only itself. */
    std::vector<PhraseMatch> matches;
    PT_CHECK(table->lookup("a", &matches));
    PT_CHECK(matches.size() == 1);
    if (matches.size() == 1) {
        PT_CHECK_STR(matches[0].phrase, "\xCE\xB1");
        PT_CHECK(matches[0].freq == 1000);
        /* Every row of a system database carries user_freq 0 (§4.2). */
        PT_CHECK(matches[0].user_freq == 0);
    }

    PT_CHECK(table->lookup("ab", &matches));
    PT_CHECK(matches.size() == 1);
    if (matches.size() == 1) {
        PT_CHECK_STR(matches[0].phrase, "\xCE\xB1\xCE\xB2");
    }

    /* A key run the table does not carry is an empty result, not a failure. */
    PT_CHECK(table->lookup("zz", &matches));
    PT_CHECK(matches.empty());
}

/*
 * §4.2 fixes the stored row order — tabkeys ASC, phrase ASC, user_freq DESC,
 * freq DESC, id ASC — and it is contract rather than housekeeping: no index is
 * created, so a scan meets the rows in that order and the engine relies on it.
 * user_freq is 0 for every row of a system database, so the sort reduces to
 * tabkeys, phrase, freq DESC.
 *
 * The source rows below are deliberately scrambled, and two of them share a
 * (tabkeys, phrase) pair so that the freq tiebreak is actually exercised.
 */
void test_stored_row_order_is_contract()
{
    const std::string path = compile(
        "BEGIN_DEFINITION\nNAME = Ordered\nEND_DEFINITION\n"
        "BEGIN_TABLE\n"
        "b\tB1\t100\n"
        "a\tA2\t500\n"
        "a\tA1\t100\n"
        "a\tA1\t900\n"
        "b\tB0\t100\n"
        "END_TABLE\n",
        "ordered.db");

    const std::vector<PhraseRow> rows = stored_rows(path);
    PT_CHECK(rows.size() == 5);
    if (rows.size() != 5) {
        return;
    }

    PT_CHECK_STR(rows[0].tabkeys + "/" + rows[0].phrase, "a/A1");
    PT_CHECK(rows[0].freq == 900);   /* freq DESC breaks the tie */
    PT_CHECK_STR(rows[1].tabkeys + "/" + rows[1].phrase, "a/A1");
    PT_CHECK(rows[1].freq == 100);
    PT_CHECK_STR(rows[2].tabkeys + "/" + rows[2].phrase, "a/A2");
    PT_CHECK_STR(rows[3].tabkeys + "/" + rows[3].phrase, "b/B0");
    PT_CHECK_STR(rows[4].tabkeys + "/" + rows[4].phrase, "b/B1");
}

/*
 * The optional tables are created from the *declaration*, and left empty. That
 * split is deliberate and is what TableProperties::pinyin_data exists to draw:
 * every table this library ships declares PINYIN_MODE and none carries pinyin
 * rows, so "declared" and "has data" have to be separate questions. Creating the
 * empty table anyway keeps the schema identical to ibus-table's.
 */
void test_optional_tables_follow_the_declaration()
{
    const std::string plain = compile(
        "BEGIN_DEFINITION\nNAME = Plain\nEND_DEFINITION\n"
        "BEGIN_TABLE\na\tX\t1\nEND_TABLE\n",
        "plain.db");

    PT_CHECK(has_sql_table(plain, "ime"));
    PT_CHECK(has_sql_table(plain, "phrases"));
    PT_CHECK(!has_sql_table(plain, "goucima"));
    PT_CHECK(!has_sql_table(plain, "pinyin"));
    PT_CHECK(!has_sql_table(plain, "suggestion"));

    const std::string declared = compile(
        "BEGIN_DEFINITION\nNAME = Declared\n"
        "USER_CAN_DEFINE_PHRASE = TRUE\n"
        "PINYIN_MODE = TRUE\n"
        "SUGGESTION_MODE = TRUE\n"
        "END_DEFINITION\n"
        "BEGIN_TABLE\na\tX\t1\nEND_TABLE\n",
        "declared.db");

    PT_CHECK(has_sql_table(declared, "goucima"));
    PT_CHECK(has_sql_table(declared, "pinyin"));
    PT_CHECK(has_sql_table(declared, "suggestion"));

    /* Declared but empty: reading it back must not report pinyin data. */
    std::string error;
    std::unique_ptr<TableDatabase> table = TableDatabase::open(declared, "", &error);
    if (table == nullptr) {
        PT_FAILF("open failed: %s", error.c_str());
        return;
    }
    PT_CHECK(table->properties().pinyin_mode);
    PT_CHECK(!table->properties().pinyin_data);
}

/*
 * char_prompts is the one declaration that is neither a scalar nor a section in
 * the compiled form: it becomes a single `ime` row holding a Python dict
 * literal, because that is what ibus-table's own compiler writes. So the writer
 * and the reader are a matched pair of hand-rolled format handlers, and this is
 * the only test that puts them back to back.
 */
void test_char_prompts_round_trip()
{
    const std::string path = compile(
        "BEGIN_DEFINITION\nNAME = Prompted\n"
        "BEGIN_CHAR_PROMPTS_DEFINITION\n"
        "a \xE6\x97\xA5\n"
        "b \xE6\x9C\x88\n"
        "END_CHAR_PROMPTS_DEFINITION\n"
        "END_DEFINITION\n"
        "BEGIN_TABLE\na\tX\t1\nEND_TABLE\n",
        "prompts.db");

    std::string error;
    std::unique_ptr<TableDatabase> table = TableDatabase::open(path, "", &error);
    if (table == nullptr) {
        PT_FAILF("open failed: %s", error.c_str());
        return;
    }

    PT_CHECK(table->properties().char_prompts.size() == 2);
    if (table->properties().char_prompts.size() == 2) {
        PT_CHECK_STR(table->properties().char_prompts.at(U'a'), "\xE6\x97\xA5");
        PT_CHECK_STR(table->properties().char_prompts.at(U'b'), "\xE6\x9C\x88");
    }
}

/*
 * The goucima table, written from the source's derived word-formation codes and
 * read back one character at a time. A table that does not declare
 * USER_CAN_DEFINE_PHRASE answers "" without touching the database, which is why
 * the negative half is checked against a table that has no goucima table at all.
 */
void test_goucima_round_trip()
{
    const std::string path = temp_path("goucima.db");
    TableSource source = parse(
        "BEGIN_DEFINITION\nNAME = Gouci\nUSER_CAN_DEFINE_PHRASE = TRUE\nEND_DEFINITION\n"
        "BEGIN_TABLE\n"
        "a\tX\t1\n"
        "abc\tX\t1\n"
        "de\tY\t1\n"
        "fg\tXY\t1\n"
        "END_TABLE\n");
    derive_goucima(&source);

    std::string error;
    if (!compile_table(source, path, &error)) {
        PT_FAILF("compile_table failed: %s", error.c_str());
        return;
    }

    std::unique_ptr<TableDatabase> table = TableDatabase::open(path, "", &error);
    if (table == nullptr) {
        PT_FAILF("open failed: %s", error.c_str());
        return;
    }

    /* "the longest tabkeys sequence that produces exactly that character" */
    PT_CHECK_STR(table->goucima("X"), "abc");
    PT_CHECK_STR(table->goucima("Y"), "de");

    /* A multi-character phrase has no goucima, and neither has a stranger. */
    PT_CHECK_STR(table->goucima("XY"), "");
    PT_CHECK_STR(table->goucima("Q"), "");

    /* A table that declares nothing answers "" without a query. */
    const std::string plain = compile(
        "BEGIN_DEFINITION\nNAME = Plain2\nEND_DEFINITION\n"
        "BEGIN_TABLE\na\tX\t1\nEND_TABLE\n",
        "plain2.db");
    std::unique_ptr<TableDatabase> no_gouci = TableDatabase::open(plain, "", &error);
    if (no_gouci != nullptr) {
        PT_CHECK_STR(no_gouci->goucima("X"), "");
    }
}

/*
 * The user database: created on open, written by record_selection(), and merged
 * into the lookup by taking the max of each frequency (§8.1).
 *
 * The table declares DYNAMIC_ADJUST because lookup() unions the user rows in
 * only when the table has something to learn — a table with neither declaration
 * would scan an empty user table on every keystroke, so the union is gated, and
 * a test that forgot the declaration would see learning silently do nothing.
 */
void test_user_database_learning()
{
    const std::string system_path = compile(
        "BEGIN_DEFINITION\nNAME = Learner\n"
        "AUTO_WILDCARD = FALSE\n"
        "DYNAMIC_ADJUST = TRUE\n"
        "END_DEFINITION\n"
        "BEGIN_TABLE\n"
        "a\tX\t100\n"
        "a\tY\t200\n"
        "END_TABLE\n",
        "learner.db");

    /* Start from no user database, so the counts below are deterministic. */
    const std::string user_path = temp_path("learner-user.db");
    std::remove(user_path.c_str());

    std::string error;
    std::unique_ptr<TableDatabase> table =
        TableDatabase::open(system_path, user_path, &error);
    if (table == nullptr) {
        PT_FAILF("open failed: %s", error.c_str());
        return;
    }
    PT_CHECK(table->has_user_db());

    std::vector<PhraseMatch> matches;
    PT_CHECK(table->lookup("a", &matches));
    PT_CHECK(matches.size() == 2);

    /* Choosing the lower-ranked phrase records a user row for it. */
    PT_CHECK(table->record_selection("a", "X"));

    PT_CHECK(table->lookup("a", &matches));
    PT_CHECK(matches.size() == 2);  /* merged by (tabkeys, phrase), not doubled */
    for (const PhraseMatch &match : matches) {
        if (match.phrase == "X") {
            /* max of each: the system freq survives, the user count arrives. */
            PT_CHECK(match.freq == 100);
            PT_CHECK(match.user_freq == 1);
        } else {
            PT_CHECK(match.user_freq == 0);
        }
    }

    /* A second selection bumps rather than inserting again. */
    PT_CHECK(table->record_selection("a", "X"));
    PT_CHECK(table->lookup("a", &matches));
    PT_CHECK(matches.size() == 2);
    for (const PhraseMatch &match : matches) {
        if (match.phrase == "X") {
            PT_CHECK(match.user_freq == 2);
        }
    }

    /* Nothing to record is false rather than an error. */
    PT_CHECK(!table->record_selection("", "X"));
    PT_CHECK(!table->record_selection("a", ""));

    /*
     * Without a user database there is nothing to write to, and saying so is
     * how a build with no writable data directory reports itself.
     */
    std::unique_ptr<TableDatabase> read_only =
        TableDatabase::open(system_path, "", &error);
    if (read_only != nullptr) {
        PT_CHECK(!read_only->has_user_db());
        PT_CHECK(!read_only->record_selection("a", "X"));
    }
}

/*
 * A failed compilation must leave nothing behind. A half-written database that
 * survived would be worse than no file: the build stages whatever is at the
 * path, and a truncated table opens successfully and types wrongly.
 */
void test_failed_compilation_leaves_no_file()
{
    const std::string path = temp_path("no-such-directory/impossible.db");
    const TableSource source = parse(
        "BEGIN_DEFINITION\nNAME = Doomed\nEND_DEFINITION\n"
        "BEGIN_TABLE\na\tX\t1\nEND_TABLE\n");

    std::string error;
    PT_CHECK(!compile_table(source, path, &error));
    PT_CHECK(!error.empty());
    PT_CHECK(!file_exists(path));
}

/*
 * open() is the library's only defence against being pointed at the wrong file,
 * because PATHIME_OPT_TABLE_FILE lets a client name any path it likes. Each
 * rejection carries its own message, since "the table did not load" is the one
 * diagnostic a user cannot act on.
 */
void test_open_rejects_non_tables()
{
    std::string error;

    /* A path with nothing at it. */
    error.clear();
    PT_CHECK(TableDatabase::open(temp_path("absent.db"), "", &error) == nullptr);
    PT_CHECK(!error.empty());

    /* A file that is not a database at all. */
    const std::string text_path = temp_path("not-a-database.db");
    {
        std::ofstream out(text_path.c_str(), std::ios::binary);
        out << "this is not a SQLite database\n";
    }
    error.clear();
    PT_CHECK(TableDatabase::open(text_path, "", &error) == nullptr);
    PT_CHECK(!error.empty());

    /* A real database that is not an ibus-table one: no `ime` table. */
    const std::string stranger_path = temp_path("stranger.db");
    std::remove(stranger_path.c_str());
    {
        sqlite3 *db = nullptr;
        if (sqlite3_open_v2(stranger_path.c_str(), &db,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK) {
            sqlite3_exec(db, "CREATE TABLE something (a, b);", nullptr, nullptr, nullptr);
        }
        sqlite3_close(db);
    }
    error.clear();
    PT_CHECK(TableDatabase::open(stranger_path, "", &error) == nullptr);
    PT_CHECK(error.find("ibus-table") != std::string::npos);

    /* An `ime` table that exists but is empty is equally not a table. */
    const std::string empty_path = temp_path("empty-ime.db");
    std::remove(empty_path.c_str());
    {
        sqlite3 *db = nullptr;
        if (sqlite3_open_v2(empty_path.c_str(), &db,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK) {
            sqlite3_exec(db, "CREATE TABLE ime (attr TEXT, val TEXT);", nullptr, nullptr,
                         nullptr);
        }
        sqlite3_close(db);
    }
    error.clear();
    PT_CHECK(TableDatabase::open(empty_path, "", &error) == nullptr);
    PT_CHECK(error.find("ibus-table") != std::string::npos);
}

/*
 * A table whose path carries characters that mean something to the machinery
 * open() builds its ATTACH out of.
 *
 * This is not a hypothetical. PATHIME_OPT_TABLE_FILE lets a client name any
 * path, and open() gets there through two layers of quoting: the file becomes a
 * `file:` URI, where `#` and `%` are syntax, and the URI is then interpolated
 * into a SQL string literal, where an apostrophe is. A home directory called
 * `Ben's tables` or a versioned directory called `tables%2` is enough to reach
 * both, and the failure would be a table that silently will not load.
 *
 * `?` and `\` are escaped by the same function and are deliberately not tested:
 * neither is legal in a Windows filename, so a case using them would have to be
 * skipped on the platform where the backslash branch is the one that matters.
 */
void test_path_needing_escapes()
{
    const std::string path = compile(kBasicTable, "odd#name%20it's.db");

    std::string error;
    std::unique_ptr<TableDatabase> table = TableDatabase::open(path, "", &error);
    if (table == nullptr) {
        PT_FAILF("open(%s) failed: %s", path.c_str(), error.c_str());
        return;
    }

    PT_CHECK_STR(table->properties().name, "Example");

    std::vector<PhraseMatch> matches;
    PT_CHECK(table->lookup("a", &matches));
    PT_CHECK(matches.size() == 1);

    /* The user database goes through the same two layers, and is created rather
     * than merely opened, so it exercises the `rwc` mode as well. */
    const std::string user_path = temp_path("odd#user%20it's.db");
    std::remove(user_path.c_str());
    std::unique_ptr<TableDatabase> learner =
        TableDatabase::open(path, user_path, &error);
    if (learner != nullptr) {
        PT_CHECK(learner->has_user_db());
    }
}

/*
 * Databases that open but are damaged, which is a different problem from
 * databases that are not databases.
 *
 * `PATHIME_OPT_TABLE_FILE` takes any path a client cares to name, so a table
 * that is a real ibus-table database with a piece missing is a live input: a
 * truncated download, a half-finished copy, a table from a newer ibus-table
 * that dropped a table this reader still queries. The library's answer must be
 * an empty result or a false, never a crash and never invented data — every
 * query below prepares SQL against a schema that is not there.
 *
 * These are also, incidentally, the only way to reach those failure branches
 * without injecting faults into SQLite itself.
 */
void test_damaged_databases()
{
    std::string error;

    /*
     * `ime` is present, so this is recognizably an ibus-table database and
     * opens — but `phrases` is gone, so every lookup prepares against nothing.
     */
    {
        const std::string path = temp_path("no-phrases.db");
        PT_CHECK(make_raw_db(path,
                             "CREATE TABLE ime (attr TEXT, val TEXT);"
                             "INSERT INTO ime VALUES ('name', 'NoPhrases');"));

        error.clear();
        std::unique_ptr<TableDatabase> table = TableDatabase::open(path, "", &error);
        if (table == nullptr) {
            PT_FAILF("open(no-phrases) failed: %s", error.c_str());
        } else {
            PT_CHECK_STR(table->properties().name, "NoPhrases");
            std::vector<PhraseMatch> matches;
            /* False, not a crash and not a silent empty success. */
            PT_CHECK(!table->lookup("a", &matches));
        }
    }

    /*
     * The declaration says the table carries word-formation codes; the goucima
     * table it would read them from is absent. goucima() answers "" — the same
     * answer as a character the table simply does not list, which is right:
     * both mean "no code available", and a caller has nothing different to do.
     */
    {
        const std::string path = temp_path("no-goucima.db");
        PT_CHECK(make_raw_db(path,
                             "CREATE TABLE ime (attr TEXT, val TEXT);"
                             "INSERT INTO ime VALUES ('name', 'NoGouci');"
                             "INSERT INTO ime VALUES ('user_can_define_phrase', 'TRUE');"
                             "CREATE TABLE phrases (id INTEGER PRIMARY KEY, tabkeys TEXT,"
                             " phrase TEXT, freq INTEGER, user_freq INTEGER);"));

        error.clear();
        std::unique_ptr<TableDatabase> table = TableDatabase::open(path, "", &error);
        if (table == nullptr) {
            PT_FAILF("open(no-goucima) failed: %s", error.c_str());
        } else {
            PT_CHECK(table->properties().user_can_define_phrase);
            PT_CHECK_STR(table->goucima("X"), "");
        }
    }

    /*
     * A NULL in the `ime` value column. SQL NULL is not the empty string, and
     * the reader has to turn it into one rather than into a null pointer it
     * then constructs a std::string from.
     */
    {
        const std::string path = temp_path("null-attr.db");
        PT_CHECK(make_raw_db(path,
                             "CREATE TABLE ime (attr TEXT, val TEXT);"
                             "INSERT INTO ime VALUES ('name', 'NullVal');"
                             "INSERT INTO ime VALUES ('max_key_length', NULL);"
                             "INSERT INTO ime VALUES (NULL, 'orphan');"
                             "CREATE TABLE phrases (id INTEGER PRIMARY KEY, tabkeys TEXT,"
                             " phrase TEXT, freq INTEGER, user_freq INTEGER);"));

        error.clear();
        std::unique_ptr<TableDatabase> table = TableDatabase::open(path, "", &error);
        if (table == nullptr) {
            PT_FAILF("open(null-attr) failed: %s", error.c_str());
        } else {
            PT_CHECK_STR(table->properties().name, "NullVal");
            /* An unparseable value leaves the documented default standing. */
            PT_CHECK(table->properties().max_key_length > 0);
        }
    }

    /*
     * A `phrases` table whose columns hold the wrong types. SQLite is happy to
     * store them; the reader must not produce a phrase that is a number or a
     * frequency that is a sentence.
     */
    {
        const std::string path = temp_path("wrong-types.db");
        PT_CHECK(make_raw_db(path,
                             "CREATE TABLE ime (attr TEXT, val TEXT);"
                             "INSERT INTO ime VALUES ('name', 'WrongTypes');"
                             "INSERT INTO ime VALUES ('auto_wildcard', 'FALSE');"
                             "CREATE TABLE phrases (id INTEGER PRIMARY KEY, tabkeys TEXT,"
                             " phrase TEXT, freq INTEGER, user_freq INTEGER);"
                             "INSERT INTO phrases (tabkeys, phrase, freq, user_freq)"
                             " VALUES ('a', 42, 'not-a-number', NULL);"));

        error.clear();
        std::unique_ptr<TableDatabase> table = TableDatabase::open(path, "", &error);
        if (table == nullptr) {
            PT_FAILF("open(wrong-types) failed: %s", error.c_str());
        } else {
            std::vector<PhraseMatch> matches;
            PT_CHECK(table->lookup("a", &matches));
            PT_CHECK(matches.size() == 1);
            if (matches.size() == 1) {
                /* Coerced, deterministically, rather than read as raw bytes. */
                PT_CHECK_STR(matches[0].phrase, "42");
                PT_CHECK(matches[0].freq == 0);
                PT_CHECK(matches[0].user_freq == 0);
            }
        }
    }

    /*
     * A file with a plausible SQLite header and nothing behind it. This is the
     * truncated-copy case, and it must be refused rather than half-read.
     */
    {
        const std::string source = compile(kBasicTable, "to-truncate.db");
        std::string head(200, '\0');
        {
            std::ifstream in(source.c_str(), std::ios::binary);
            in.read(&head[0], static_cast<std::streamsize>(head.size()));
            head.resize(static_cast<size_t>(in.gcount()));
        }
        const std::string path = temp_path("truncated.db");
        {
            std::ofstream out(path.c_str(), std::ios::binary);
            out.write(head.data(), static_cast<std::streamsize>(head.size()));
        }

        error.clear();
        /* Either answer is defensible — what matters is that it is an answer. */
        std::unique_ptr<TableDatabase> table = TableDatabase::open(path, "", &error);
        if (table == nullptr) {
            PT_CHECK(!error.empty());
        } else {
            std::vector<PhraseMatch> matches;
            table->lookup("a", &matches);  /* must not crash */
        }
    }
}

/*
 * A user database that cannot be created is not fatal: the table still types
 * and only learning is lost. Reporting the whole input method unavailable
 * because a home directory is read-only would be the wrong trade, so open()
 * swallows the failure and has_user_db() is how a caller finds out.
 *
 * The unopenable path here is one *under a regular file*, which no filesystem
 * will turn into a directory.
 */
void test_unusable_user_database_is_not_fatal()
{
    const std::string system_path = compile(kBasicTable, "learner-ro.db");
    const std::string user_path = system_path + "/not-a-directory/user.db";

    std::string error;
    std::unique_ptr<TableDatabase> table =
        TableDatabase::open(system_path, user_path, &error);
    if (table == nullptr) {
        PT_FAILF("open with an unusable user path failed: %s", error.c_str());
        return;
    }

    PT_CHECK(!table->has_user_db());
    PT_CHECK(!table->record_selection("a", "\xCE\xB1"));

    /* The table itself is entirely usable. */
    std::vector<PhraseMatch> matches;
    PT_CHECK(table->lookup("a", &matches));
    PT_CHECK(matches.size() == 1);
}

/*
 * The parser's diagnostics. These exist for one caller — tools/table-compile,
 * printing to whoever is adding a table to the build — so the thing worth
 * asserting is that the message names the line, which is the only part that
 * turns "it failed" into somewhere to look.
 */
void test_source_diagnostics()
{
    struct Case {
        const char *text;
        const char *needle;
        const char *line;
    };

    /* Each malformation the format actually forbids, and where it is reported. */
    const Case cases[] = {
        {"BEGIN_DEFINITION\nNAME = A\nBEGIN_TABLE\n", "BEGIN_TABLE inside DEFINITION",
         "line 3"},
        {"BEGIN_DEFINITION\nNAME = A\nEND_TABLE\n", "END_TABLE does not close DEFINITION",
         "line 3"},
        {"END_DEFINITION\n", "END_DEFINITION does not close anything", "line 1"},
        {"BEGIN_DEFINITION\nNAME = A\nthis line has no equals sign\nEND_DEFINITION\n",
         "definition line has no '='", "line 3"},
    };

    for (const Case &c : cases) {
        std::istringstream in(c.text);
        TableSource source;
        std::string error;
        PT_CHECK(!parse_table_source(in, &source, &error));
        if (error.find(c.needle) == std::string::npos) {
            PT_FAILF("expected \"%s\" in \"%s\"", c.needle, error.c_str());
        }
        if (error.find(c.line) == std::string::npos) {
            PT_FAILF("expected \"%s\" in \"%s\"", c.line, error.c_str());
        }
    }

    /*
     * A section left open at end of file. Reported without a line number on
     * purpose: the offending line is the missing one, so naming where the file
     * ran out would point at the wrong place.
     */
    {
        std::istringstream in("BEGIN_TABLE\na\tX\t1\n");
        TableSource source;
        std::string error;
        PT_CHECK(!parse_table_source(in, &source, &error));
        PT_CHECK(error.find("unexpected end of file inside TABLE") != std::string::npos);
    }

    /* A null error pointer is accepted; the caller may not want the message. */
    {
        std::istringstream in("END_DEFINITION\n");
        TableSource source;
        PT_CHECK(!parse_table_source(in, &source, nullptr));
    }
}

/* The file-reading wrapper: the missing-file message, and a real round trip. */
void test_parse_table_source_file()
{
    std::string error;
    TableSource source;

    const std::string absent = temp_path("no-such-table.txt");
    std::remove(absent.c_str());
    PT_CHECK(!parse_table_source_file(absent, &source, &error));
    PT_CHECK(error.find("cannot open") != std::string::npos);

    const std::string path = temp_path("source.txt");
    {
        std::ofstream out(path.c_str(), std::ios::binary);
        out << kBasicTable;
    }
    error.clear();
    PT_CHECK(parse_table_source_file(path, &source, &error));
    PT_CHECK(error.empty());
    PT_CHECK_STR(source.properties.name, "Example");
    PT_CHECK(source.phrases.size() == 4);
}

/*
 * An explicit GOUCI section, which overrides derivation rather than adding to
 * it. Tables that ship one are declaring codes the phrase rows do not imply, so
 * a parser that merged the two would silently invent entries.
 */
void test_gouci_section()
{
    const TableSource source = parse(
        "BEGIN_DEFINITION\nNAME = G\nUSER_CAN_DEFINE_PHRASE = TRUE\nEND_DEFINITION\n"
        "BEGIN_TABLE\na\tX\t1\nEND_TABLE\n"
        "BEGIN_GOUCI\n"
        "X\tqrst\n"
        "Y uvwx\n"
        "lonely\n"
        "END_GOUCI\n");

    PT_CHECK(source.goucima.size() == 2);
    PT_CHECK_STR(source.goucima.at("X"), "qrst");
    /* Space-separated is accepted alongside tab-separated. */
    PT_CHECK_STR(source.goucima.at("Y"), "uvwx");
    /* A lone token declares nothing and is not an error. */
    PT_CHECK(source.goucima.find("lonely") == source.goucima.end());
}

/*
 * The derived `Z` wildcard. The check that makes it safe is positional: a key
 * used anywhere but first in some code cannot become a wildcard, because it
 * would shadow that code. wubi-jidian86 is the real table this declines, and the
 * three cases below are that situation in miniature.
 */
void test_derive_single_wildcard()
{
    /* `z` appears only as a leading key, so it is free to become the wildcard. */
    {
        TableSource source = parse(
            "BEGIN_DEFINITION\nNAME = W\nEND_DEFINITION\n"
            "BEGIN_TABLE\n"
            "z\tX\t1\n"
            "za\tY\t1\n"
            "ab\tZ\t1\n"
            "END_TABLE\n");
        PT_CHECK(derive_single_wildcard(&source, 'z'));
        PT_CHECK_STR(source.properties.single_wildcard, "z");
        /* Written under the format's own key, so the .db says what it means. */
        PT_CHECK_STR(source.properties.attrs["single_wildcard_char"], "z");
    }

    /* `z` inside a code: declaring it would shadow that row. Declined. */
    {
        TableSource source = parse(
            "BEGIN_DEFINITION\nNAME = W2\nEND_DEFINITION\n"
            "BEGIN_TABLE\n"
            "z\tX\t1\n"
            "zzbd\tY\t1\n"
            "END_TABLE\n");
        PT_CHECK(!derive_single_wildcard(&source, 'z'));
        PT_CHECK(source.properties.single_wildcard.empty());
    }

    /* A table that declares its own wildcards keeps them. */
    {
        TableSource source = parse(
            "BEGIN_DEFINITION\nNAME = W3\n"
            "SINGLE_WILDCARD_CHAR = ?\n"
            "MULTI_WILDCARD_CHAR = *\n"
            "END_DEFINITION\n"
            "BEGIN_TABLE\na\tX\t1\nEND_TABLE\n");
        PT_CHECK(!derive_single_wildcard(&source, 'z'));
        PT_CHECK_STR(source.properties.single_wildcard, "?");
    }
}

/*
 * The punctuation strip, which keeps the punctuation keys with the shared
 * layer (src/punctuation.*) when a table carries punctuation entries of its
 * own. The cases below are the real tables in miniature: cangjie5 at
 * ibus-table-chinese 1.8.9+, whose punctuation is single-key convenience rows
 * bolted onto an alphabetic table, and stroke5, whose punctuation characters
 * *are* the alphabet, spelled into multi-key codes.
 */
void test_strip_punctuation_keys()
{
    /* cangjie-shaped: stripped, and both forms of the declaration follow —
     * the typed sets and the attr strings the compiler writes back. */
    {
        TableSource source = parse(
            "BEGIN_DEFINITION\nNAME = P1\nLANGUAGES = zh_TW\n"
            "VALID_INPUT_CHARS = ab,.\n"
            "BEGIN_CHAR_PROMPTS_DEFINITION\n"
            "a \xE6\x97\xA5\n"
            ", \xE9\x80\x97\n"
            "END_CHAR_PROMPTS_DEFINITION\n"
            "END_DEFINITION\n"
            "BEGIN_TABLE\n"
            "a\tX\t1000\n"
            "ab\tY\t500\n"
            ",\t\xEF\xBC\x8C\t950\n"
            ",\t\xE3\x80\x81\t949\n"
            ".\t\xE3\x80\x82\t950\n"
            "END_TABLE\n");
        PT_CHECK(strip_punctuation_keys(&source) == 3);
        PT_CHECK(source.phrases.size() == 2);
        PT_CHECK(source.properties.valid_input_chars.count(U',') == 0);
        PT_CHECK(source.properties.valid_input_chars.count(U'a') == 1);
        PT_CHECK_STR(source.properties.attrs["valid_input_chars"], "ab");
        PT_CHECK(source.properties.char_prompts.count(U',') == 0);
        PT_CHECK(source.properties.char_prompts.count(U'a') == 1);
    }

    /* stroke5-shaped: multi-key codes spell with `,./`, so nothing moves —
     * including the single-key rows those same characters key. */
    {
        TableSource source = parse(
            "BEGIN_DEFINITION\nNAME = P2\nLANGUAGES = zh\n"
            "VALID_INPUT_CHARS = nm,./\n"
            "END_DEFINITION\n"
            "BEGIN_TABLE\n"
            ",\t\xE4\xB9\x99\t1000\n"
            ",,\t\xE5\x8F\x88\t900\n"
            "./\t\xE4\xB9\x8B\t800\n"
            "nm,./\t\xE6\xB0\xB8\t700\n"
            "END_TABLE\n");
        PT_CHECK(strip_punctuation_keys(&source) == 0);
        PT_CHECK(source.phrases.size() == 4);
        PT_CHECK(source.properties.valid_input_chars.count(U',') == 1);
    }

    /* Non-CJK: the punctuation layer never runs, so nothing is stripped. */
    {
        TableSource source = parse(
            "BEGIN_DEFINITION\nNAME = P3\nLANGUAGES = en\n"
            "VALID_INPUT_CHARS = ab;\n"
            "END_DEFINITION\n"
            "BEGIN_TABLE\n"
            "a\tX\t1000\n"
            ";\tY\t900\n"
            "END_TABLE\n");
        PT_CHECK(strip_punctuation_keys(&source) == 0);
        PT_CHECK(source.phrases.size() == 2);
    }

    /* A declared wildcard is input machinery, never stripped. */
    {
        TableSource source = parse(
            "BEGIN_DEFINITION\nNAME = P4\nLANGUAGES = zh\n"
            "VALID_INPUT_CHARS = ab?\n"
            "SINGLE_WILDCARD_CHAR = ?\n"
            "END_DEFINITION\n"
            "BEGIN_TABLE\n"
            "a\tX\t1000\n"
            "END_TABLE\n");
        PT_CHECK(strip_punctuation_keys(&source) == 0);
        PT_CHECK(source.properties.valid_input_chars.count(U'?') == 1);
    }

    /* A table that is nothing but punctuation would be stripped to nothing,
     * so it is left as declared. */
    {
        TableSource source = parse(
            "BEGIN_DEFINITION\nNAME = P5\nLANGUAGES = zh\n"
            "VALID_INPUT_CHARS = ,.\n"
            "END_DEFINITION\n"
            "BEGIN_TABLE\n"
            ",\t\xEF\xBC\x8C\t1000\n"
            "END_TABLE\n");
        PT_CHECK(strip_punctuation_keys(&source) == 0);
        PT_CHECK(source.phrases.size() == 1);
    }
}

}  // namespace

int main(void)
{
    test_compile_and_reopen();
    test_stored_row_order_is_contract();
    test_optional_tables_follow_the_declaration();
    test_char_prompts_round_trip();
    test_goucima_round_trip();
    test_user_database_learning();
    test_failed_compilation_leaves_no_file();
    test_open_rejects_non_tables();
    test_damaged_databases();
    test_unusable_user_database_is_not_fatal();
    test_path_needing_escapes();
    test_source_diagnostics();
    test_parse_table_source_file();
    test_gouci_section();
    test_derive_single_wildcard();
    test_strip_punctuation_keys();
    return pt_report("core.table_compile");
}
