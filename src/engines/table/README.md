# Table engine (not started)

The fourth engine is written here from scratch, to `docs/ibus-table-spec.md` —
source `.txt` file format, compiled SQLite schema, key-event state machine,
candidate sorting. `docs/ibus-table-options.md` is its option inventory, and
`refs/ibus-table-chinese/` supplies real tables (Wubi, Cangjie, Stroke5,
Zhuyin, …) to test against.

It is a peer of the vendored backends, not a wrapper: ibus-table is Python, so
there is nothing to link against — it supplies the proven feature set, and the
spec was derived from it clean-room. One engine id (`PATHIME_ENGINE_TABLE`)
covers every table-driven method, because they differ only in the table
loaded; tables are selected per context with `PATHIME_OPT_TABLE_FILE`, so one
engine can hold several compiled tables and hand a different one to each
context.

Until code lands here, `LIBPATHIME_WITH_TABLE` stays OFF and is forced off
with a warning in `cmake/LibpathimeDependencies.cmake` naming the missing
piece as the implementation itself, and
`pathime_has_engine(PATHIME_ENGINE_TABLE)` is false.

When implementation starts: sources go in this directory following the
sibling adapters' pattern (`table_backend.h/.cc` implementing
`src/backend.h`), the sources get added to the gated block in
`src/CMakeLists.txt`, and the option gate in LibpathimeDependencies is
lifted. Write it after the three real adapters — `backend.h` should be shaped
by the libraries we cannot change, then implemented by the one we can.
