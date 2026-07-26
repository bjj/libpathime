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
| `LIBPATHIME_REQUIRE_BACKENDS` | `OFF` | Turn "missing dependency ⇒ skip" into a hard error (for CI). |
| `LIBPATHIME_BUILD_TESTS` | `OFF` | Build submodule test suites where available. |
| `PYZY_BUILD_DB_ANDROID` | `ON` | Build pyzy's bundled Android pinyin database (needs Python 3). |
| `BUILD_SHARED_LIBS` | `ON` | Shared vs. static submodule libraries. |

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
produce identical `anthy.dic` and `android.db`, and the built anthy converts
correctly against its generated dictionary.

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
   - `pyzy/src/` is mirrored whole. `PinyinParserTable.h` uses GNU
     labelled-field initialisers; `String.h` is missing `operator<<` overloads
     that only LP64 made unnecessary; `PhraseEditor.h` forward-declares
     `class Config` where it is a `struct`, which MSVC's mangling notices.
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

### Known Windows limitations

These are runtime gaps, not build failures; none of them affect the artifacts
the build produces.

- The anthy **runtime** library still opens its private-dictionary and history
  files in the CRT's default text mode (the binary-mode fix is deliberately
  scoped to the codegen executables, which own their process — a library must
  not change `_fmode` under its host).
- pyzy's Bopomofo path casts `wchar_t *` to UCS-4 in one place
  (`BopomofoContext.cc`), which is wrong where `wchar_t` is 16-bit.
- `cmake --install` leaves a second copy of `hangul.dll` in `lib/` — upstream's
  install rule uses one bare `DESTINATION`. The usable copy in `bin/` is added
  by this build.
- The installed tree does not carry pyzy's vcpkg runtime dependencies
  (`glib-2.0-0.dll` and friends). They are staged into the build tree's `bin/`
  by vcpkg's applocal step; deploying an install needs them copied alongside.
