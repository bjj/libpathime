/*
 * The table-driven backend: the engine and context halves of backend.h, over
 * the data layer in this directory.
 *
 * This is the only file under engines/table/ that includes backend.h, and that
 * is the boundary the directory is organized around. table_source.h,
 * table_db.h, table_properties.h, ranking.h and variants.h know nothing about
 * compositions, key events, options or outputs — they answer questions about
 * table data, and are testable and reusable (by the compile tool) without any
 * of the engine machinery. Everything that turns those answers into libpathime
 * behaviour is here.
 *
 * The engine is ours rather than a vendored library, so the usual adapter
 * caveats do not apply: nothing here is working around an upstream API. What it
 * is working around instead is the distance between two documents — the
 * key-event behaviour of docs/ibus-table-spec.md §7 and the fixed meanings the
 * public header gives Space and Return — and where they disagree the header
 * wins, because it is the contract every other engine already keeps.
 */

#ifndef LIBPATHIME_SRC_ENGINES_TABLE_TABLE_BACKEND_H
#define LIBPATHIME_SRC_ENGINES_TABLE_TABLE_BACKEND_H

#include <map>
#include <memory>
#include <string>

#include "backend.h"
#include "engines/table/table_db.h"

namespace pathime {
namespace table {

/**
 * Resolve what PATHIME_OPT_TABLE_FILE names to a filesystem path.
 *
 * A value with no path separator is a shipped table: `cangjie5` resolves to
 * `<resource_dir>/table/cangjie5.db`. Anything else — absolute or relative — is
 * used verbatim. That split exists because the resource directory defaults to a
 * location the client never names and cannot compute (`pathime-data` beside the
 * loaded library), so without it a client could not reach the tables this
 * library ships without also being told where they are.
 *
 * Exposed for the tests, which need to know which file a name should reach.
 */
std::string resolve_table_path(const std::string &value);

/** The per-user database for @a table_path, or "" when there is no data dir. */
std::string user_db_path(const std::string &table_path);

}  // namespace table

/* The three hooks backend.h declares for this engine are defined in
 * table_backend.cc: table_global_init, table_global_shutdown,
 * table_create_engine. */

}  // namespace pathime

#endif /* LIBPATHIME_SRC_ENGINES_TABLE_TABLE_BACKEND_H */
