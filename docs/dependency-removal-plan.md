# Shedding the external shared-library dependencies

This is the working plan for the `shed-shared-deps` branch. It exists so the
work can be picked up mid-stream; when the branch lands, the substance moves to
`THIRD-PARTY.md`, `docs/windows-port.md` and `docs/design-history.md`, and this
file is deleted.

## What the investigation established

The Windows artifact ships five external DLLs beside `pathime.dll`:
`glib-2.0-0.dll`, `iconv-2.dll`, `intl-8.dll`, `pcre2-8.dll`, `sqlite3.dll`.

- **glib is pyzy's alone.** It is linked PRIVATE into `pyzy-1.0.dll` only;
  `pathime.dll` never links it and pyzy's four public headers are glib-free.
  The usage is shallow: ~28 functions, 5 macros, 3 types across 12 files —
  asserts/logging, printf helpers, UTF-8 helpers, file/path operations, one
  `GTimer`, and a `g_timeout` that never fires (no `GMainLoop` runs; see
  `docs/design-history.md` on the pyzy autosave). No GObject, no GIO, no glib
  containers, no GRegex anywhere in the built sources. The complete token
  inventory outside `src/tests/` (not built): `g_assert`, `g_assert_not_reached`,
  `g_warning`, `g_debug`, `g_error`, `G_LIKELY`/`G_UNLIKELY`, `G_N_ELEMENTS`,
  `G_DIR_SEPARATOR_S`, `G_FILE_TEST_IS_REGULAR`, `gboolean`, `GError`/
  `g_error_free`, `g_free`, `g_snprintf`, `g_vsnprintf`, `g_strdup_vprintf`,
  `g_strlcpy`, `g_strlcat`, `g_utf8_validate`, `g_utf8_strlen`,
  `g_utf8_offset_to_pointer`, `g_utf8_prev_char`, `g_unichar_to_utf8`,
  `g_ucs4_to_utf8`, `g_file_test`, `g_mkdir_with_parents`, `g_unlink`,
  `g_rename`, `g_build_filename`, `g_get_user_cache_dir`,
  `g_get_user_config_dir`, `GTimer` + `g_timer_new/destroy/start/elapsed`,
  `g_timeout_add_seconds`, `g_source_remove`.
- **iconv, intl and pcre2 are used by nothing in this repository.** They are
  vcpkg glib's own DLL closure; removing glib from pyzy removes all three.
  libhangul under CMake compiles gettext out and its only iconv use is the
  optional vendored test; anthy-unicode converts EUC-JP with its own in-tree
  tables and uses neither.
- **sqlite3 has two independent consumers**: pyzy's `Database.cc` (system
  dictionary + user phrase learning; `ATTACH ":memory:"`, the online-backup
  API, legacy `sqlite3_prepare`, double-quoted SQL string literals) and the
  table engine's `src/engines/table/table_db.cc` (URI opens, a WAL user
  database, `LIKE … ESCAPE`). The compiled table format *is* a SQLite database
  by contract (`docs/ibus-table-mapping.md` §4). Statically linking the
  public-domain amalgamation removes the DLL without touching either format.
- **Upstreaming the glib removal is not realistic.** openSUSE/pyzy is alive but
  near-dormant (~6 trivial commits since 2022) and pyzy serves the glib-native
  ibus ecosystem; a patch replacing glib with hand-rolled code offers upstream
  nothing. Carrying it is nearly free: the design diffs only ~10 include lines
  of upstream-owned text, so rebases stay conflict-free.

## Work stream A — remove glib from pyzy (removes iconv, intl, pcre2 with it)

### A1. Commits on `engines/pyzy` branch `libpathime` (local until review)

1. **Add `src/GlibLess.h` + `src/GlibLess.cc`** — glib-free implementations of
   exactly the names pyzy uses, following the `DataDir.cc` precedent. Functions
   are real symbols named `pyzy_g_*`; the header maps the glib names with
   `#define`, so the library can coexist with a real glib in the same process
   and never collides at link time. Contents:
   - Macros/types: `G_LIKELY`/`G_UNLIKELY` (`__builtin_expect` where available),
     always-on `g_assert`/`g_assert_not_reached`, `g_warning`/`g_debug` to
     stderr, `g_error` expanding to a noreturn fatal (matters: `Database.h`'s
     use sits in a reference-returning function), `G_N_ELEMENTS`,
     `G_DIR_SEPARATOR_S`, `G_FILE_TEST_IS_REGULAR`, `gboolean`, a minimal
     `GError` (one use, in `String.h`).
   - UTF-8: `g_utf8_validate` (called with len −1 only), `g_utf8_strlen`,
     `g_utf8_offset_to_pointer`, `g_utf8_prev_char`, `g_unichar_to_utf8`,
     `g_ucs4_to_utf8`. Semantics transcribed from `src/utf8.cc`, but the code
     is duplicated, not linked: pyzy is its own library and links no
     libpathime objects.
   - printf/str: `g_strdup_vprintf` (size-then-malloc over `vsnprintf`),
     `g_snprintf`/`g_vsnprintf` → `snprintf`/`vsnprintf`, BSD `g_strlcpy`/
     `g_strlcat`, `g_free` → `free`.
   - Filesystem, UTF-8-in / wide-API-underneath on Windows (pattern from
     `src/paths.cc`): `g_file_test(IS_REGULAR)`, `g_mkdir_with_parents`,
     `g_unlink`, `g_rename`, `g_build_filename` (two-component joins only),
     `g_get_user_cache_dir`/`g_get_user_config_dir` (reached only by the
     no-argument `InputContext::init()` overload, which libpathime never
     calls, but they must work). **`g_rename` must be
     `MoveFileExW(…, MOVEFILE_REPLACE_EXISTING)` on Windows** — `Database.cc`
     renames the tmp file over an existing `user-1.0.db` on every save after
     the first, and `_wrename` fails on an existing target, silently losing
     user learning at shutdown.
   - Timer: `GTimer` over `std::chrono::steady_clock`;
     `g_timeout_add_seconds` returns a constant nonzero id and registers
     nothing (the callback never fires today either; the nonzero
     `m_timeout_id` is what makes `~Database` save the user db);
     `g_source_remove` is a no-op. Behaviour is identical to the glib build.
   Also adds `GlibLess.cc` to `src/Makefile.am` for branch self-consistency.
2. **Swap the includes** (~10 lines): `String.h`, `Phrase.h`,
   `SpecialPhraseTable.h`, `InputContext.cc`, `DynamicSpecialPhrase.cc` take
   `"GlibLess.h"` instead of `<glib.h>`; `Database.cc` also drops
   `<glib/gstdio.h>`; `Util.h` gains the include its BSD-only `g_strlcpy`
   currently borrows transitively.

### A2. libpathime changes (after bumping the submodule pointer)

- `cmake/ports/pyzy/CMakeLists.txt`: add `GlibLess.cc` to the source list;
  delete the glib probe/link block. `find_package(PkgConfig)` stays for the
  POSIX uuid branch.
- `tests/pyzy/CMakeLists.txt`: drop the `PkgConfig::LIBPATHIME_GLIB` link.
- `cmake/LibpathimeDependencies.cmake`: remove glib from the pyzy gate and its
  missing-dependency message.
- `cmake/LibpathimeInstall.cmake`: delete the vcpkg copyright loop (glib,
  libiconv, gettext, pcre2 are all glib's closure); drop `glib-2.0 >= 2.24.0`
  from the pkg-config `Requires.private`.
- `cmake/pathime-config.cmake.in`: delete the glib block (uuid stays).
- Docs: `THIRD-PARTY.md` (the four DLL rows and the closure rationale),
  `BUILD.md` (dependency table, apt/brew/vcpkg lines), `README.md`,
  `CMakePresets.json` description, `docs/windows-port.md` (runtime-closure
  paragraph), a superseding entry in `docs/design-history.md`.
- CI: remove glib from every install line in `ci.yml`, `release.yml`,
  `coverage.yml`, `codeql.yml`. Add `libiconv` explicitly to the Windows vcpkg
  lines so `hangul.vendored.hangul` keeps running there (today iconv arrives
  as glib by-catch).

## Work stream B — sqlite3 becomes a vendored static library on Windows

Findings that shaped this:

- The API surface is 28 functions, but the engine features are broad (ATTACH,
  WAL, the backup API, `LIKE … ESCAPE`, `datetime()`, URI filenames), so
  `SQLITE_OMIT_*` minimisation buys ~100–150 KB at best. Use the stock
  amalgamation with the safe flags. **Must not set** `SQLITE_DQS=0` (pyzy
  emits double-quoted SQL string literals) or `SQLITE_OMIT_AUTOINIT`
  (`table_db.cc` relies on auto-init). Safe: `SQLITE_OMIT_LOAD_EXTENSION`,
  `SQLITE_OMIT_SHARED_CACHE`, `SQLITE_OMIT_DEPRECATED` (legacy
  `sqlite3_prepare` survives it), `SQLITE_OMIT_PROGRESS_CALLBACK`,
  `SQLITE_OMIT_DECLTYPE`, `SQLITE_OMIT_UTF16`, `SQLITE_DEFAULT_MEMSTATUS=0`,
  `SQLITE_MAX_EXPR_DEPTH=0`, `SQLITE_LIKE_DOESNT_MATCH_BLOBS`.
- **Two embedded copies** result (one in `pathime.dll`, one in
  `pyzy-1.0.dll`), ~+1 MB net against shipping the DLL. Benign on Windows:
  separate symbol spaces, no interposition, and the two copies never open the
  same file (pyzy's databases live under `…/pyzy/`, the table engine's under
  `…/table/`). On Linux the same move would be hazardous — default-visibility
  ELF exports would unify `sqlite3_*` across the two shared objects and could
  hijack a consumer's own sqlite — so **POSIX stays on the system sqlite**,
  which also matches the artifact policy (POSIX ships no external libraries).
- Dropping the sqlite *format* later (if the ibus-table-chinese data source is
  replaced and only source-`.txt` compatibility matters) is a contained
  `table_db.cc` rewrite but removes sqlite only from `pathime.dll`, not from
  pyzy — not worth doing for dependency reasons alone. Filed as a possible
  rider on the data-source replacement.

### B1. Trial first (no repo changes)

```
vcpkg install sqlite3:x64-windows-static-md
cmake -S . -B build/trial-static-sqlite -G Ninja
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
  -DCMAKE_BUILD_TYPE=Release
  -DSQLite3_INCLUDE_DIR=<vcpkg>/installed/x64-windows-static-md/include
  -DSQLite3_LIBRARY=<vcpkg>/installed/x64-windows-static-md/lib/sqlite3.lib
```

Build, `ctest`, `dumpbin /DEPENDENTS` on `pathime.dll` and `pyzy-1.0.dll`
(no `sqlite3.dll` expected), record the size deltas. Both cache variables must
be set or `FindSQLite3` falls back to the dynamic triplet; a Debug configure
needs the `debug/lib` variant.

### B2. Permanent shape

- New `cmake/ports/sqlite3/`: pinned `sqlite3.c`/`sqlite3.h` with the version
  recorded, built as a STATIC lib with the flag set above,
  `POSITION_INDEPENDENT_CODE ON`, `C_VISIBILITY_PRESET hidden` (free now, and
  it makes a later POSIX flip safe), then `add_library(SQLite::SQLite3 ALIAS …)`
  so all four existing consumers (`src/`, `cmake/ports/pyzy/`, `tools/`,
  `tests/core/`) link unchanged.
- Option `LIBPATHIME_VENDORED_SQLITE`, default ON when `WIN32`, OFF elsewhere.
- Top-level `CMakeLists.txt` adds the port before pyzy, gated on the option
  and `(PYZY OR TABLE)`.
- `cmake/LibpathimeDependencies.cmake`: skip `find_package(SQLite3)` when the
  target is vendored. `cmake/ports/pyzy/CMakeLists.txt`: guard its
  `find_package(SQLite3 REQUIRED)` with `if(NOT TARGET SQLite::SQLite3)`.
- `cmake/LibpathimeInstall.cmake`: install the archive for static layouts via
  the existing vendored-install path; the `.pc` `Requires.private` sqlite3
  entry becomes conditional on not vendoring.
- `cmake/pathime-config.cmake.in`: gate `find_dependency(SQLite3)` the same
  way.
- CI drops `sqlite3:x64-windows`; `THIRD-PARTY.md` reworks the sqlite rows
  (public domain — no copyright-file install obligation); `BUILD.md`,
  `docs/windows-port.md`, `docs/design-history.md` updated.

## Sequencing and verification

1. Work stream A, then B1, then B2. After B2 the Windows install ships zero
   external DLLs.
2. Full `ctest` after each stream. Targeted checks for A: a second run against
   the same `PYZY_TEST_HOME` (exercises rename-over-existing on the user db);
   a non-ASCII data-directory path (wide-API handling); learning persisting
   across `Database::finalize()`. For B: `dumpbin /DEPENDENTS`, an install
   whose `bin/` contains no external DLLs, and `examples/install-check`
   against that install. Linux legs run in CI once the branch is pushed.
