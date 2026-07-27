# Building libpathime

One CMake build produces the `libpathime` library and the three vendored
input-method submodules it wraps (libhangul, anthy-unicode, pyzy), on Linux and
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
| Japanese (anthy-unicode) | nothing external; its dictionary is built by host tools at build time, so cross-compiling is not yet supported |
| Chinese (pyzy) | `glib-2.0 ≥ 2.24`, `sqlite3`, a UUID provider (`libuuid` on Unix; the bundled Rpcrt4 shim on Windows), plus Python 3 for the optional `android.db` |
| Table-driven | nothing external — it is written in this repository, and is not written yet |

The table-driven backend (`PATHIME_ENGINE_TABLE`: Wubi, Cangjie, Stroke5,
Zhuyin, …) has no submodule. `ibus-table`, the reference implementation, is
Python and cannot be linked against, so this engine is written in `libpathime`
itself against `docs/ibus-table-spec.md`. That code does not exist yet, so
`LIBPATHIME_WITH_TABLE` defaults to `OFF` and turning it on is refused at
configure time with a warning naming the implementation — not a dependency — as
what is missing.

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
| `LIBPATHIME_WITH_TABLE` | `OFF` | The table-driven backend. Not implemented yet; forced back off (or a hard error under `LIBPATHIME_REQUIRE_BACKENDS`) if enabled. |
| `LIBPATHIME_REQUIRE_BACKENDS` | `OFF` | Turn "missing dependency ⇒ skip" into a hard error (for CI). |
| `LIBPATHIME_BUILD_TESTS` | `OFF` | Build the test suites — see `docs/testing.md`. |
| `PYZY_BUILD_DB_ANDROID` | `ON` | Build pyzy's bundled Android pinyin database (needs Python 3). |
| `BUILD_SHARED_LIBS` | `ON` | Shared vs. static libraries. On Windows the anthy family is static either way; see `docs/anthy-mapping.md`. |

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
- **Data**: `anthy.dic` (built by anthy's four-stage codegen, ~20 MB) and
  `android.db` (pyzy, ~3.4 MB / 16 tables).

Pass `-DCMAKE_INSTALL_PREFIX=...` at **configure** time if you plan to
install. libhangul's upstream CMake installs to `CMAKE_INSTALL_FULL_LIBDIR`,
an absolute path baked in during configure, so `cmake --install --prefix`
cannot relocate it afterwards.

`cmake --install` lays out headers under `include/pathime/`, libraries under
`lib/`, DLLs under `bin/`, and data under `share/`.

## Consuming the library

The public interface is `<pathime/pathime.h>` and the `pathime` library; nothing
else about the build is part of the contract, and a client never includes a
backend header or links a backend library. On Windows that is a hard
requirement rather than a style preference for anthy — see
`docs/anthy-mapping.md`.

No CMake package config is installed yet, so a consumer names the include
directory and library itself. A consumer of a **static** build must define
`PATHIME_STATIC` when compiling against the header, to match
`PATHIME_BUILT_STATIC` in `<pathime/config.h>`; without it the declarations pick
up `__declspec(dllimport)` on Windows. Within this build tree the `pathime`
target carries both the include directories and that definition, so anything
linking `libpathime::pathime` needs no further wiring.

## How the pieces fit together

- `cmake/LibpathimeOptions.cmake` — build-wide preamble and the
  `LIBPATHIME_WITH_*` options.
- `cmake/LibpathimeDependencies.cmake` — probes dependencies, gates backends,
  prints the configuration summary.
- `cmake/LibpathimeCompat.cmake`, `cmake/compat/win32/` — the Windows compat
  layer; `docs/windows-port.md`.
- `libhangul/` ships modern CMake, so the build descends into its `hangul/`
  library directory (skipping its `po/` and `tools/`, which need gettext) and
  generates its `config.h`.
- `cmake/ports/anthy-unicode/`, `cmake/ports/pyzy/` — native CMake ports for the
  two submodules that ship no CMake. They compile the vendored sources directly
  and reproduce each project's build-time code and data generation. The
  submodule trees are never modified.
- `src/` — the library target; `docs/source-layout.md` is the map of what each
  file owns.
- `tests/` — every test suite, vendored and our own; `docs/testing.md`.
