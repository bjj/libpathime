# Building libpathime

One CMake build produces the `libpathime` library and the three vendored
input-method libraries it wraps (libhangul, anthy-unicode, pyzy), on Linux and
Windows.

- Running the tests: `docs/testing.md`
- How the Windows port works, and its known limitations: `docs/windows-port.md`

## Prerequisites

- CMake ≥ 3.21 and a C11/C++17 toolchain (GCC, Clang, MSVC, or clang-cl).
- Ninja (recommended) or any CMake generator.
- Submodules checked out: `git submodule update --init --recursive`.

Per-backend dependencies:

| Backend | Needs |
|---------|-------|
| Korean (libhangul) | nothing beyond a C compiler |
| Japanese (anthy-unicode) | nothing external; its dictionary is built by host tools at build time, so cross-compiling is not supported |
| Chinese (pyzy) | `glib-2.0 ≥ 2.24`, `sqlite3`, a UUID provider (`libuuid` on Unix; the bundled Rpcrt4 shim on Windows), plus Python 3 for the optional `android.db` |
| Table-driven | `sqlite3` — the compiled table format *is* a SQLite database, so reading one ibus-table wrote needs it. Its tables come from the `engines/ibus-table-chinese` submodule. |

The table-driven backend (`PATHIME_ENGINE_TABLE`: Wubi, Cangjie, Stroke5,
Zhuyin, …) wraps no library. `ibus-table`, the reference implementation, is
Python and cannot be linked against, so this engine is written in `libpathime`
itself against `docs/ibus-table-mapping.md`. What it *does* need is data:
`engines/ibus-table-chinese` is a submodule of table sources, and the build
compiles a selected set of them with `pathime-table-compile`, a host tool built
from the engine's own sources. Upstream's `tables/CMakeLists.txt` is not used —
it calls `ibus-table-createdb` (the Python being replaced) plus `sed`, `iconv`
and `awk`, none of which run on Windows.

`LIBPATHIME_WITH_TABLE` defaults `ON`, like the other three. Missing SQLite
turns it off with a warning, as with any other backend. Without the submodule
the engine builds and opens a table a client names by absolute path; it ships
none.

Linux (Debian/Ubuntu):

```bash
sudo apt-get install build-essential cmake ninja-build pkg-config \
                     libglib2.0-dev libsqlite3-dev uuid-dev
```

Windows: Visual Studio 2022 (or the Build Tools), plus vcpkg for pyzy's
dependencies.

```bat
git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat
C:\dev\vcpkg\vcpkg install glib sqlite3
set VCPKG_ROOT=C:\dev\vcpkg
```

UUID, `mmap`, and the two dozen other POSIX facilities the submodules expect
come from an in-tree compat layer, so there is nothing else to install. Python 3
is optional — it is only used for pyzy's `android.db` — and the build finds it
through the `py` launcher if `FindPython3` cannot.

Two things reliably trip people up on Windows:

- **A developer command prompt overwrites `VCPKG_ROOT`.** `vcvars64.bat` points
  it at the vcpkg bundled with Visual Studio, which has no packages installed.
  Set your own `VCPKG_ROOT` *after* running vcvars. (The `windows-msvc` preset
  uses the Visual Studio generator and needs no vcvars at all; only the Ninja
  preset does.)
- **Check the tree out with LF line endings.** `git config --global
  core.autocrlf input` (or `false`) before cloning. The codegen tools run with
  the CRT in binary mode, so a dictionary source converted to CRLF would leave
  stray CRs in parsed tokens.

## Configure & build

Using presets (see `CMakePresets.json`):

```bash
cmake --preset linux-release
cmake --build --preset linux-release
```

Or directly:

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

> Build in a normal filesystem that supports symlinks and is case-sensitive.
> Versioned `.so` symlinks cannot be created on some network- or VM-mounted
> paths.

## Windows

```bat
cmake --preset windows-msvc
cmake --build --preset windows-msvc
```

There is also a `windows-ninja` preset (clang-cl + Ninja); it must be run from a
developer command prompt, and remember to re-set `VCPKG_ROOT` afterwards.

## Options

| Option | Default | Meaning |
|--------|---------|---------|
| `LIBPATHIME_WITH_HANGUL` / `_ANTHY` / `_PYZY` | `ON` | Enable each backend, both the vendored library and its adapter. A backend whose dependencies are missing is warned about and skipped. |
| `LIBPATHIME_WITH_TABLE` | `ON` | The table-driven backend. Needs `sqlite3`; skipped with a warning (or a hard error under `LIBPATHIME_REQUIRE_BACKENDS`) without it. |
| `LIBPATHIME_TABLES` | five tables | Which tables to compile into `pathime-data/table/`, as `<name>\|<source>\|<freq source>` entries. Default: `cangjie5`, `quick5`, `wubi-jidian86`, `stroke5`, `zhuyin` — about 9 MB compiled. All thirteen families in the submodule are available; adding one is a line. |
| `LIBPATHIME_TABLE_COVERAGE` | `windows` on Windows, `noto` elsewhere | Which glyph-coverage map trims the compiled tables, or `none` to trim nothing. See "Glyph coverage" below; on Windows `none` is a reasonable choice. |
| `LIBPATHIME_TABLE_REGENERATE_COVERAGE` | `OFF` | Offer a `pathime-table-coverage` target that rewrites the map named by `LIBPATHIME_TABLE_COVERAGE` from a font. Needs Python 3 and nothing else. See "Glyph coverage" below — the ordinary build never reads a font. |
| `LIBPATHIME_TABLE_COVERAGE_FONT` | per platform | The fonts that target reads, as a list — the Windows map is the union of several. Only consulted when the option above is `ON`. |
| `LIBPATHIME_REQUIRE_BACKENDS` | `OFF` | Turn "missing dependency ⇒ skip" into a hard error (for CI). |
| `LIBPATHIME_BUILD_TESTS` | `OFF` | Build the test suites — see `docs/testing.md`. |
| `LIBPATHIME_TEST_COVERAGE` | `OFF` | Instrument the build for **test** coverage and offer the `pathime-test-coverage` target. Needs the option above, `gcovr`, and a gcov-style toolchain. Nothing to do with `LIBPATHIME_TABLE_COVERAGE` — see "Test coverage" below. |
| `LIBPATHIME_BUILD_DEMO` | `OFF` | Build the interactive terminal demo — see `demo/README.md`. Needs the `demo/cpp-terminal` submodule. |
| `PYZY_BUILD_DB_ANDROID` | `ON` | Build pyzy's bundled Android pinyin database (needs Python 3). |
| `LIBPATHIME_INSTALL_VENDORED` | `OFF` | Install the vendored backend libraries and their headers as ordinary system libraries, rather than into the private directory beside `libpathime` that the default uses. For a distribution package that intends to replace the system's `libhangul`, `libanthy-unicode` and `libpyzy` with ours; see "What gets produced". |
| `BUILD_SHARED_LIBS` | `ON` | Shared vs. static libraries. One exception ignores it: on Windows cpp-terminal — which only the demo links — is always static, because its published globals carry no `dllimport` and a DLL build of it therefore cannot be linked against on Windows at all. |

Which backends survived the gating is recorded in the generated
`include/pathime/config.h` as `PATHIME_WITH_*`, so a client can compile out
unavailable paths. `pathime_has_engine()` is the matching runtime query, and
answers false for a backend whose runtime data is missing as well.

## What gets produced

- **The library**: `libpathime` — `libpathime.so` / `libpathime.a` on Linux,
  `pathime.dll` + import library on Windows.
- **Public headers**: `include/pathime/pathime.h` and the generated
  `include/pathime/config.h`.
- **Vendored libraries**: `libhangul`, `libanthy-unicode` (+
  `libanthydic-unicode`, `libanthyinput-unicode`), `libpyzy-1.0`.
- **Consumer metadata**: `lib/cmake/pathime/` for `find_package(pathime)` and
  `lib/pkgconfig/pathime.pc`. See "Consuming the library".
- **Data**: `pathime-data/`, holding `anthy/anthy.dic` (built by anthy's
  four-stage codegen, ~20 MB), `pyzy/main.db` (~3.4 MB / 16 tables) plus
  `pyzy/phrases.txt`, and `table/*.db` (5–9 MB for the five default tables,
  depending on the glyph-coverage map; ~15 MB with none). See below.
- **Build-time tools, not installed**: `bin/pathime-table-compile`.
- **With `LIBPATHIME_BUILD_DEMO=ON`**: `bin/pathime-demo`, plus the
  `cpp-terminal` library it draws with. Neither is installed.

Pass `-DCMAKE_INSTALL_PREFIX=...` at **configure** time if you plan to install.
The generated `pathime.pc` records absolute paths, so `cmake --install --prefix`
cannot relocate the result afterwards.

`cmake --install` lays out headers under `include/pathime/`, `libpathime` under
`lib/` (the DLL under `bin/`), and the vendored backend libraries under
`lib/pathime/` — a **private directory that is on no library search path**.
libpathime reaches them through an RPATH of `$ORIGIN/pathime`; on Windows,
which has no RPATH, they sit beside `pathime.dll` in `bin/` instead.

That is deliberate, and it is the layout an engine shipping libpathime wants.
Our libhangul, anthy-unicode and pyzy are not the system's: two of the three
carry portability commits on their own `libpathime` branch (`THIRD-PARTY.md`),
and all three are built with this project's options and compat layer. Installed
as system libraries they would collide with a distribution's packages of the
same names, and an engine that resolved one of those instead would be running a
library this project has never tested. No vendored header is installed at all,
because nothing outside this build compiles against one.

`LIBPATHIME_INSTALL_VENDORED=ON` puts the libraries and their headers in the
ordinary system places instead — for a packager who intends exactly that and
has decided to deal with the consequences. `cmake/LibpathimeInstall.cmake` is
the whole of the layout, in one file.

## Shipping the data

The engines' read-only data lives in a directory named `pathime-data`, **beside
the libpathime binary** — next to `libpathime.so`, next to `pathime.dll`, or
next to the program itself when the library is linked statically. That is where
`pathime_init()` looks unless a client says otherwise, and it is resolved from
the loaded module's own path at runtime, so it holds wherever the pair is
installed and whatever the process's working directory is. `cmake --install`
puts it there, and the build stages the same layout beside the built library so
that the demo and the tests find it identically.

Shipping libpathime with an application therefore means copying `pathime-data/`
along with the library and keeping the two together.

A client whose layout separates code from data sets
`pathime_init_params_t::resource_dir` instead. Either way an engine whose data
is absent is reported unavailable by `pathime_has_engine()` and costs the other
engines nothing.

### Glyph coverage

Compiled tables are trimmed to the characters the target can render. A stock
Cangjie table holds roughly twice as many characters as an ordinary CJK font, so
without the trim a slightly mistyped code fills the candidate list with tofu.

What the trim actually removes is **CJK Extension B and beyond**: of the 40,686
distinct characters the Noto map drops from the five shipped tables, 40,603 are
supplementary-plane and the remaining 83 are private-use. So the right answer
depends on whether the target has a supplementary-plane font, and that differs
by platform enough that one map cannot serve both — which is why two ship:
`noto` describes Noto Sans CJK, the ordinary Linux target, and `windows` the
in-box faces a bare Windows install has. `LIBPATHIME_TABLE_COVERAGE` picks,
measured against those tables' 305,150 rows:

| value | map | rows dropped | `cangjie5` | table data |
|-------|-----|--------------|------------|------------|
| `noto` | Noto Sans CJK, 44,810 points | 36.6% | 52.4% | ~9 MB |
| `windows` | Windows in-box CJK, 43,509 points | 38.3% | 54.9% | ~5 MB |
| `none` | — | none | none | ~15 MB |

`none` is a real choice on Windows rather than a footgun. A system with the
Chinese language feature installed carries **SimSun-ExtB**, which covers 60,349
supplementary code points on its own and makes every row of every shipped table
renderable — measured, the whole Windows CJK font set drops exactly one character
out of the tables' 70,948. An embedder who knows their target has it should take
`none` and get the characters; the `windows` default assumes only the in-box
faces, which a bare en-US install has.

Neither map is a superset of the other, so neither is a default with an override.
The default follows the platform because a default describing the wrong font
landscape is a worse failure than the cross-platform difference — and the
difference stays visible: the configure summary prints the map, and so does every
line `pathime-table-compile` emits.

**Only the table engine is trimmed.** hangul, anthy and pyzy commit whatever
their own data holds, at build time and at runtime alike; no coverage map is
applied to them. That is a measurement rather than an oversight — the same two
maps, run over the other three backends' data, drop almost nothing:

| backend | data | distinct characters | dropped by `noto` | by `windows` |
|---------|------|---------------------|-------------------|--------------|
| hangul | precomposed syllables and compat jamo | 11,172 + jamo | none | none |
| anthy | 245,374 word entries, the dictionaries `mkworddic/dict.args` reads | 10,986 | 4, in 11 entries | none |
| pyzy | 65,105 phrase rows, `pyzy/main.db` | 16,463 | none | none |

pyzy's phrases stop at U+9FA5, inside the original CJK block every CJK font
carries. anthy's reach further — 301 of its characters are supplementary-plane —
but those are the JIS X 0213 additions, which a font aimed at Japanese carries by
definition, so both maps hold every one of them; what the Noto map drops is four
symbols (`≒`, `⅓`, `⅔`, `⅕`) that Noto Sans CJK JP genuinely lacks. hangul is
narrower still: it emits precomposed syllables and compat jamo only, and hanja
conversion is out of scope for the adapter, so libhangul's hanja tables — which
do carry 54 Ext-B characters the Windows map would drop — are never read.

The table engine is the outlier because of what its data *is*. A Cangjie or Wubi
table is a mapping over a character repertoire, extended as far as Unicode goes;
a conversion dictionary is a list of words people write. Trimming the repertoire
is worth 36–38% of its rows, and trimming the dictionaries would be worth four
characters — so an embedder wanting a guarantee that nothing unrenderable reaches
their candidate list does not have one, and is within four characters of it.

**The build never reads a font.** Each map is generated data checked in at
`src/engines/table/coverage_data_<map>.h` — the same arrangement
`variants_data.h` has with Unicode data, and for the same reason: reading an
installed font at build time would make a compiled `.db` a function of the build
machine, so two builds of the same commit would ship different tables. Two builds
of the same commit with the same map still produce byte-identical tables, which
is checked across MSVC and clang-cl.

Regenerating is a deliberate act, not a build step:

```bash
cmake -S . -B build -DLIBPATHIME_TABLE_REGENERATE_COVERAGE=ON \
      -DLIBPATHIME_TABLE_COVERAGE=noto \
      -DLIBPATHIME_TABLE_COVERAGE_FONT=/path/to/font.ttc
cmake --build build --target pathime-table-coverage
```

It rewrites the header for whichever map `LIBPATHIME_TABLE_COVERAGE` names, in
the source tree, because the output is a checked-in file meant to be reviewed in
a diff. The generator parses the font's own `cmap` table and needs only Python 3
— no fontconfig, and nothing platform-specific. `LIBPATHIME_TABLE_COVERAGE_FONT`
is a list, because the Windows map is the union of several in-box faces; an entry
may be `PATH#FACE` to pick out of a TrueType collection.
`tools/generate-coverage.py --help` and its module docstring carry the rest.
Per-table opt-out is `pathime-table-compile --no-glyph-filter`.

## Test coverage

A different subject from the section above, sharing an unfortunate word. *Glyph*
coverage is which characters a font can render. **Test coverage is which lines of
`src/` the suites under `tests/` execute**, and it is what
`LIBPATHIME_TEST_COVERAGE` and the `pathime-test-coverage` target are about.
Nothing connects the two.

```bash
cmake -S . -B build/coverage -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DLIBPATHIME_BUILD_TESTS=ON -DLIBPATHIME_TEST_COVERAGE=ON
cmake --build build/coverage
cmake --build build/coverage --target pathime-test-coverage
```

The two build lines are in that order because the target runs the suites and
does not build them. It writes `build/coverage/coverage/`: `index.html`
(annotated source, per file), `coverage.txt`, and `coverage.xml` (Cobertura, for
CI). Needs `gcovr` — `pip install gcovr` or `apt-get install gcovr`.

Requires GCC or Clang with the GNU driver, and `LIBPATHIME_BUILD_TESTS=ON`;
either missing is a configure error naming the reason rather than a silently
inert option. The whole tree is instrumented, as it is under the sanitizers, and
`-O0 -g` is forced over the build type — gcov's line counts describe optimised
code as the optimiser left it, not as it was written. The report's filter is what
narrows the result back to `src/`, so the vendored libraries do not appear.

**The target clears the counters before it runs, and that matters more than it
sounds.** gcov accumulates into `.gcda` files, and `pathime-table-compile`
compiles several of the table engine's own sources and then runs during the
build to produce the shipped tables. Those runs are real executions of
`src/engines/table/`, but they are the build's, not the suites' — counted, they
credit the tests with a chunk of `table_db.cc` that no test touches — worth
about three points overall, and twenty-five on that one file.

### Test coverage on Windows

Not wired into the build, because neither Windows toolchain uses gcov. Both
recipes are manual:

- **MSVC (`windows-msvc` preset)** — [OpenCppCoverage][occ]. It reads PDBs
  rather than instrumenting, so there are no extra flags and no separate build;
  configure `Debug` or `RelWithDebInfo` and wrap the test run:

  ```bat
  OpenCppCoverage --sources src --export_type html:coverage -- ctest --test-dir build\windows-msvc -C Debug
  ```

- **clang-cl (`windows-ninja` preset)** — LLVM's source-based coverage, which is
  the more precise of the two and gives real branch data. Build with
  `-fprofile-instr-generate -fcoverage-mapping` on the compile flags and
  `-fprofile-instr-generate` on the link flags, run the suites with
  `LLVM_PROFILE_FILE=%%p.profraw`, then `llvm-profdata merge` and `llvm-cov
  report`. The LLVM tools must match the clang-cl version.

Visual Studio Enterprise also has coverage built in, if that is the SKU to hand.

[occ]: https://github.com/OpenCppCoverage/OpenCppCoverage

## Consuming the library

The public interface is `<pathime/pathime.h>` and the `pathime` library; nothing
else about the build is part of the contract, and a client never includes a
backend header or links a backend library. On Windows that is a hard
requirement rather than a style preference for anthy — see
`docs/anthy-mapping.md`.

An installed libpathime is consumed either way round:

```cmake
find_package(pathime REQUIRED)
target_link_libraries(app PRIVATE libpathime::pathime)
```

```sh
cc app.c $(pkg-config --cflags --libs pathime)
```

The imported target carries the include directory, the library, and the
`PATHIME_STATIC` definition a static build needs — that one matters, because
without it the declarations pick up `__declspec(dllimport)` on Windows to match
`PATHIME_BUILT_STATIC` in `<pathime/config.h>`. `pkg-config` puts the same
definition in `--cflags`. Within this build tree the `pathime` target already
carries all of it, so anything linking `libpathime::pathime` needs no wiring
either way.

Two things a **static** consumer has to know, both because the vendored
archives end up on its own link line rather than inside a shared library:

- **A C project must `enable_language(CXX)`** (or `project(app C CXX)`).
  libpathime is C++ behind a C header; the export records that, but CMake can
  only act on it if the project has a C++ compiler to name. Without it the link
  fails on `std::` symbols. The `pkg-config` route names the C++ runtime in
  `Libs.private` and needs nothing extra.
- **The external libraries come back**: `find_package(pathime)` calls
  `find_dependency()` for SQLite3, GLib and libuuid, so those must be findable
  where the consumer builds. A consumer of a shared build needs none of them.

The generated `pathime-targets.cmake` does name the vendored libraries, because
CMake needs them named to work out RPATHs and static link lines. That does not
make them part of the interface: there is no installed header to compile
against them with, and the library they belong to is `libpathime::pathime`.

## How the pieces fit together

- `cmake/LibpathimeOptions.cmake` — build-wide preamble and the
  `LIBPATHIME_WITH_*` options.
- `cmake/LibpathimeDependencies.cmake` — probes dependencies, gates backends,
  prints the configuration summary.
- `cmake/LibpathimeInstall.cmake` — the installed layout: where the vendored
  libraries go and why, the RPATHs that reach them, and the `find_package` and
  `pkg-config` files generated from `cmake/pathime-config.cmake.in` and
  `cmake/pathime.pc.in`.
- `cmake/LibpathimeCompat.cmake`, `cmake/compat/win32/` — the Windows compat
  layer; `docs/windows-port.md`.
- `engines/libhangul/` ships modern CMake, so the build descends into its `hangul/`
  library directory (skipping its `po/` and `tools/`, which need gettext) and
  generates its `config.h`.
- `cmake/ports/anthy-unicode/`, `cmake/ports/pyzy/` — native CMake ports for the
  two submodules that ship no CMake. They compile the vendored sources directly
  and reproduce each project's build-time code and data generation. The
  submodule trees are never modified.

`src/` builds the single `pathime` target: the core plus each enabled adapter,
added source by source, so a disabled backend contributes no object at all. The
one file that lives there without being part of it is
`src/engines/table/coverage.cc`, which `tools/` compiles into
`pathime-table-compile` instead — glyph filtering happens when a table is
compiled, never at runtime. `docs/source-layout.md` is the map of what each file
owns. `tests/` builds only under `LIBPATHIME_BUILD_TESTS` and installs nothing;
see `docs/testing.md`.
