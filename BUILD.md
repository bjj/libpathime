# Building libpathime

At this stage the CMake build exists to compile the three vendored input-method
submodules (libhangul, anthy-unicode, pyzy) from a single tree, on Linux and
Windows. The `libpathime` library itself is not written yet.

## Prerequisites

- CMake ≥ 3.21 and a C/C++11 toolchain (GCC, Clang, or MSVC).
- Ninja (recommended) or any CMake generator.
- Submodules checked out: `git submodule update --init --recursive`.

Per-backend dependencies:

| Backend | Needs |
|---------|-------|
| Korean (libhangul)     | nothing beyond a C compiler |
| Japanese (anthy-unicode) | nothing external; its dictionary is built by host tools at build time (so cross-compiling is not yet supported) |
| Chinese (pyzy)         | `glib-2.0 ≥ 2.24`, `sqlite3`, a UUID provider (`libuuid` on Unix; the bundled Rpcrt4 shim on Windows), plus Python 3 for the optional `android.db` |
| Table-driven           | nothing — but there is nothing to build yet either; see below |

The table-driven backend (`PATHIME_ENGINE_TABLE`: Wubi, Cangjie, Stroke5,
Zhuyin, …) has no submodule. `ibus-table`, the reference implementation, is
Python and cannot be linked against, so this engine is written in `libpathime`
itself against the specification in `docs/ibus-table-spec.md`. That code does
not exist yet, so `LIBPATHIME_WITH_TABLE` defaults to `OFF` and turning it on
is refused at configure time with a warning naming the implementation — not a
dependency — as what is missing.

Linux (Debian/Ubuntu): `sudo apt-get install build-essential cmake ninja-build pkg-config libglib2.0-dev libsqlite3-dev uuid-dev`

### Windows

Visual Studio 2022 (or the Build Tools), plus vcpkg for pyzy's dependencies:

```bat
git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat
C:\dev\vcpkg\vcpkg install glib sqlite3
set VCPKG_ROOT=C:\dev\vcpkg
```

UUID, `mmap`, and the two dozen other POSIX facilities the submodules expect are
supplied by an in-tree compat layer (`cmake/compat/win32`), so there is nothing
else to install. Python 3 is optional; it is only used for pyzy's `android.db`,
and the build finds it through the `py` launcher if `FindPython3` cannot.

Two things reliably trip people up:

- **A developer command prompt overwrites `VCPKG_ROOT`.** `vcvars64.bat` points
  it at the vcpkg bundled with Visual Studio, which has no packages installed.
  Set your own `VCPKG_ROOT` *after* running vcvars. (The `windows-msvc` preset
  uses the Visual Studio generator and needs no vcvars at all; only the Ninja
  preset does.)
- **Check the tree out with LF line endings.** The codegen tools run with the
  CRT in binary mode, so a dictionary source converted to CRLF by
  `core.autocrlf=true` would leave stray CRs in parsed tokens. `git config
  --global core.autocrlf input` (or `false`) before cloning avoids the question.

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

> Build in a normal filesystem that supports symlinks. Versioned `.so` symlinks
> cannot be created on some network/VM-mounted paths.

Windows:

```bat
cmake --preset windows-msvc
cmake --build --preset windows-msvc
```

There is also a `windows-ninja` preset (clang-cl + Ninja); it must be run from a
developer command prompt, and remember to re-set `VCPKG_ROOT` afterwards.

> Pass `-DCMAKE_INSTALL_PREFIX=...` at **configure** time if you plan to
> install. libhangul's upstream CMake installs to `CMAKE_INSTALL_FULL_LIBDIR`,
> an absolute path baked in during configure, so `cmake --install --prefix`
> cannot relocate it afterwards.

## Options

| Option | Default | Meaning |
|--------|---------|---------|
| `LIBPATHIME_WITH_HANGUL` / `_ANTHY` / `_PYZY` | `ON` | Enable each backend. A backend whose dependencies are missing is warned about and skipped. |
| `LIBPATHIME_WITH_TABLE` | `OFF` | The table-driven backend. Not implemented yet; forced back off (or a hard error under `LIBPATHIME_REQUIRE_BACKENDS`) if enabled. |
| `LIBPATHIME_REQUIRE_BACKENDS` | `OFF` | Turn "missing dependency ⇒ skip" into a hard error (for CI). |
| `LIBPATHIME_BUILD_TESTS` | `OFF` | Build the test suites — see below. |
| `PYZY_BUILD_DB_ANDROID` | `ON` | Build pyzy's bundled Android pinyin database (needs Python 3). |
| `BUILD_SHARED_LIBS` | `ON` | Shared vs. static submodule libraries. |

## Tests

```bash
cmake --preset linux-release -DLIBPATHIME_BUILD_TESTS=ON
cmake --build --preset linux-release
ctest --test-dir build/linux-release --output-on-failure
```

On Windows add `-C Release` to the `ctest` line (the Visual Studio generator is
multi-config). Tests are named `<backend>.<name>`, so `ctest -R '^anthy\.'`
runs one backend's suite.

The suite passes on Linux (30 tests). On Windows the vendored count is one
lower — see `hangul.vendored.unittest` below for the missing one.

Two of the directories are libpathime's own, and they differ in what they link:

- `tests/api/` (`api.<name>`) links the built library and touches only
  exported symbols. Plain C11 against `<pathime/pathime.h>`, which doubles as
  proof the header works from strict C and that every symbol a client needs is
  actually exported. It holds the ABI, lifecycle and options suites, plus one
  end-to-end test per engine that types real Korean, Japanese and Chinese
  through the public API. Nothing here may link a backend library — the public
  header is the whole interface these tests are entitled to, and on Windows
  linking anthy alongside libpathime is actively broken (see "Why anthy is
  static on Windows").
- `tests/core/` (`core.<name>`) compiles the internal sources under test
  directly into each executable, because internal helpers carry no
  `PATHIME_API` and a shared build does not export them. C++17, covering
  `utf8.*`, `composition.*` and the options machinery at seams the public API
  cannot reach.

The three engine tests are the only ones under `tests/api/` gated on a
`LIBPATHIME_WITH_*` option, because they need their backend's *runtime data*
rather than just the library — anthy's dictionary and pyzy's database, neither
of which exists at its installed location in a build tree. That wiring lives in
`tests/api/CMakeLists.txt`; it is a test-environment problem, not a library
one, and the library must not grow code to hunt for data files itself. Both
tests reach their data the way an installation would: anthy through a
build-tree conf file named by the `CONFFILE` environment variable, pyzy through
a `main.db` staged into the test's working directory.

A test registered ahead of its implementation exits 77, which ctest reports as
*skipped*. Never delete a registration.

Everything else lives under `tests/<backend>/`, and each directory holds two
kinds:

- `<backend>.vendor*` — the submodule's own suite. None of the three upstream
  build systems is used by this build, so the wiring is reproduced rather than
  reused, and the vendored sources are compiled where they sit.
- `<backend>.<name>` — tests written for libpathime. These exist to make the
  Windows build prove it behaves like the Linux one, so all of them must build
  and pass on both.

Two vendored suites are conditional, for reasons that are worth knowing before
reading anything into their absence:

- `hangul.vendored.unittest` needs the Check framework *and* a 32-bit
  `wchar_t`, so in practice it runs on Linux and not on Windows. Upstream's
  `test.c` compares UCS-4 output with `wcscmp((const wchar_t *) …)`, which on
  Windows' 16-bit `wchar_t` compares one character and stops — it would pass
  while testing almost nothing. `hangul.ic` restates the same expectations
  against `ucschar` instead, so nothing is lost on Windows but the vendored
  suite itself.
- `hangul.vendored.hangul` needs iconv, which it uses only to print UCS-4 as
  UTF-8. glibc has it; on Windows it comes from vcpkg, which glib pulls in
  anyway.

`pyzy`'s own `src/tests/basic.cc` is deliberately not wired up: its
`DummyObserver` declares the `InputContext::Observer` callbacks with the wrong
constness, so nothing overrides, the class stays abstract, and the file has not
compiled in a long time. What it was written to check is covered by the tests
in `tests/pyzy/`, which take their expected values from it.

One upstream bug has no test because it cannot have one:
`anthy_dic_util_set_personality()` aborts the process on both Linux and
Windows — `anthy_dic_set_personality()` replaces the record and personal-dic
cache that `anthy_dic_util_init()` installed without releasing them, and the
orphan is freed twice at teardown. See `tests/anthy/test_dicutil.c`.

## What gets produced

Libraries: `libhangul`, `libanthy-unicode` (+ `libanthydic-unicode`,
`libanthyinput-unicode`), `libpyzy-1.0`. Data: `anthy.dic` (built by anthy's
four-stage codegen, ~20 MB) and `android.db` (pyzy, ~3.4 MB / 16 tables).
`cmake --install` lays out headers under `include/<pkg>/`, libraries under
`lib/`, DLLs under `bin/`, and data under `share/`.

On Windows the anthy family is built **static** regardless of
`BUILD_SHARED_LIBS`; libhangul and pyzy still produce DLLs. See below for why.

## How the submodules are integrated

- **libhangul** ships modern CMake; we descend into its `hangul/` library dir
  (skipping its `po/`/`tools/` which need gettext) and generate its `config.h`.
- **anthy-unicode** and **pyzy** ship no CMake. `cmake/ports/<name>/CMakeLists.txt`
  are native CMake ports that compile the vendored sources directly and reproduce
  each project's build-time code/data generation. The submodule trees are never
  modified.

## Windows

Verified on Windows 11 with Visual Studio 2022 (MSVC 19.44) and with clang-cl
19 + Ninja, x64, against vcpkg's glib 2.88 and sqlite3 3.53. Both presets
produce identical `anthy.dic` and `android.db`, and both pass the full test
suite — which is the real check that the workarounds below preserve Linux
behaviour rather than merely compiling.

Nothing under `libhangul/`, `anthy-unicode/` or `pyzy/` is edited. Portability
is handled three ways:

1. **`cmake/compat/win32/`** — replacement POSIX headers (`sys/mman.h`,
   `unistd.h`, `dirent.h`, `pwd.h`, `netinet/in.h`, `sys/time.h`,
   `uuid/uuid.h`, `sys/utsname.h`) placed ahead of the system include path,
   backed by a small static library. Anything that belongs in a header Windows
   *does* ship — `alloca`, `strncasecmp`, POSIX record locking — is injected
   with `/FI win32_prelude.h` instead, since those headers must not be shadowed.
2. **Generated source variants.** Where a vendored file is not valid on an
   MSVC-style compiler, the port compiles a fixed *copy* produced at configure
   time (so editing a submodule needs a re-run of cmake):
   - `anthy-unicode/src-diclib/alloc.c` casts heap pointers through
     `unsigned long`, which is 32-bit under LLP64; the copy uses `uintptr_t`.
   - `anthy-unicode/src-diclib/file_dic.c` walks paths POSIX-style and so
     cannot create `%USERPROFILE%\.config\anthy`; the copy teaches it drive
     letters and `\`.
   - `pyzy/src/` is mirrored whole. `PinyinParserTable.h` uses GNU
     labelled-field initialisers; `String.h` is missing `operator<<` overloads
     that only LP64 made unnecessary; `PhraseEditor.h` forward-declares
     `class Config` where it is a `struct`, which MSVC's mangling notices; and
     `BopomofoContext.cc` casts a `const wchar_t *` to UCS-4, which is only
     correct where `wchar_t` is 32 bits.
3. **Build-time behaviour.** The codegen tools get an 8 MB stack (they
   `alloca` in loops) and run with the CRT in binary mode, because they
   `fopen(..., "w")` binary dictionaries. Generated text inputs are written
   with LF for the same reason.

### Why anthy is static on Windows

anthy's three public libraries share global data across the library boundary —
the splitter reads `anthy_wt_all` / `anthy_wt_none`, which `src-worddic` owns
and initialises at runtime. Windows can only import a data symbol through
`__declspec(dllimport)` in the declaring header, and duplicating the definition
per DLL would give each copy its own uninitialised state. Building the family
static avoids the question entirely, and has the side benefit that the codegen
tools have no DLLs to locate when the build runs them.

It has one consequence worth knowing before it bites: a shared `libpathime`
absorbs a *private copy* of anthy, and anthy's configuration and
initialised-once flags are process-global (the conf database in
`src-diclib/conf.c`, `is_init_ok` in `src-main/main.c`, `dic_init_count` in
`src-worddic/word_dic.c`). So on Windows any other module in the process that
also links anthy gets a **second, independent anthy**: its
`anthy_conf_override()` calls are invisible to libpathime's copy, and
`anthy_init()` runs twice against two sets of state. ELF hides this by
resolving both to a single definition, so the same code can pass on Linux and
fail on Windows with no source difference at all — which is exactly how it was
found, in `api.engine_anthy`.

Nothing that links `libpathime` may therefore also link anthy. Where a test
needs to point anthy at build-tree data, it does so the way an installation
would — through anthy's own conf file, named by the `CONFFILE` environment
variable; `tests/api/CMakeLists.txt` sets that up and explains it. The suites
under `tests/anthy/` are unaffected: they link anthy and not libpathime, so
they have exactly one copy and may keep calling `anthy_conf_override()`.

`pathime_init_params_t::data_dir` is unaffected by all of this. It reaches
anthy as `anthy_conf_override("XDG_CONFIG_HOME", data_dir)` *inside* the
library, so it lands in the copy that matters, and the per-user state
(`<data_dir>/anthy/last-record2_*`, the private dictionary, the lock file) is
created there on Windows as it is on Linux — including creating a multi-level,
backslash-separated `data_dir` from nothing, which works because of the
generated Windows `file_dic.c` described above. The directory is created when
the first *context* is created, not by `pathime_init()`.

### Known Windows limitations

These are runtime gaps, not build failures; none of them affect the artifacts
the build produces.

- The anthy **runtime** library still opens its private-dictionary and history
  files in the CRT's default text mode (the binary-mode fix is deliberately
  scoped to the codegen executables, which own their process — a library must
  not change `_fmode` under its host). The only observed effect is that
  `textdict`'s newline padding is written twice as long as intended; the
  private-dictionary write/read/delete cycle round-trips correctly, and
  `ctest -R '^anthy\.dicutil$'` covers it.
- `cmake --install` leaves a second copy of `hangul.dll` in `lib/` — upstream's
  install rule uses one bare `DESTINATION`. The usable copy in `bin/` is added
  by this build.
- The installed tree does not carry pyzy's vcpkg runtime dependencies
  (`glib-2.0-0.dll` and friends). They are staged into the build tree's `bin/`
  by vcpkg's applocal step; deploying an install needs them copied alongside.
