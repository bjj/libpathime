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

Linux (Debian/Ubuntu): `sudo apt-get install build-essential cmake ninja-build pkg-config libglib2.0-dev libsqlite3-dev uuid-dev`

Windows (MSVC + vcpkg): `vcpkg install glib sqlite3` and set `VCPKG_ROOT`. UUID and
the few POSIX headers the submodules use are supplied by an in-tree Windows compat
shim (`cmake/compat/win32`), so no libuuid/pthreads packages are needed.

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

## Options

| Option | Default | Meaning |
|--------|---------|---------|
| `LIBPATHIME_WITH_HANGUL` / `_ANTHY` / `_PYZY` | `ON` | Enable each backend. A backend whose dependencies are missing is warned about and skipped. |
| `LIBPATHIME_REQUIRE_BACKENDS` | `OFF` | Turn "missing dependency ⇒ skip" into a hard error (for CI). |
| `LIBPATHIME_BUILD_TESTS` | `OFF` | Build submodule test suites where available. |
| `PYZY_BUILD_DB_ANDROID` | `ON` | Build pyzy's bundled Android pinyin database (needs Python 3). |
| `BUILD_SHARED_LIBS` | `ON` | Shared vs. static submodule libraries. |

## What gets produced

Libraries: `libhangul`, `libanthy-unicode` (+ `libanthydic-unicode`,
`libanthyinput-unicode`), `libpyzy-1.0`. Data: `anthy.dic` (built by anthy's
four-stage codegen) and `android.db` (pyzy). `cmake --install` lays out headers
under `include/<pkg>/`, libraries under `lib/`, and data under `share/`.

## How the submodules are integrated

- **libhangul** ships modern CMake; we descend into its `hangul/` library dir
  (skipping its `po/`/`tools/` which need gettext) and generate its `config.h`.
- **anthy-unicode** and **pyzy** ship no CMake. `cmake/ports/<name>/CMakeLists.txt`
  are native CMake ports that compile the vendored sources directly and reproduce
  each project's build-time code/data generation. The submodule trees are left
  untouched; Windows source portability is handled by include-path shims in
  `cmake/compat/win32` rather than edits to the vendored code.

## Windows status

The Windows build path (compat shims, presets, dependency handling) is
implemented but has not yet been compiled on a Windows host. The two areas most
likely to need iteration there are anthy's `mkfiledic` dictionary staging and the
`mmap`/`uuid`/`uname` shims.
