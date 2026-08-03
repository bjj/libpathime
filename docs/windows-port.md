# The Windows port

How `libpathime` and the three vendored input-method libraries it wraps build
and behave on Windows. `BUILD.md` is what to install and type; this is what the
build is doing behind that and where the result differs from Linux.

Verified on Windows 11 with Visual Studio 2022 (MSVC 19.44) and with
clang-cl 19 + Ninja, x64, against the vcpkg sqlite3 the port was
developed with (3.53). Both presets produce identical `anthy.dic`,
`android.db` and compiled `table/*.db`, and both pass the full test suite —
which is the real check that the workarounds below preserve Linux behaviour
rather than merely compiling.

## The rule

A vendored source that will not compile or behave as it stands is fixed **in
the submodule**, as a titled commit on its `libpathime` branch — never by a
rewrite applied while the build runs. `libhangul/` and the
`ibus-table-chinese/` table sources need nothing; `anthy-unicode/` and `pyzy/`
each carry a short series of commits, and `git log` in each is the list.

What stays here is what is not part of those libraries at all: the compat layer
below, and the build files that replace their meson and autotools.

## 1. The compat layer

`cmake/compat/win32/` supplies replacement POSIX headers — `sys/mman.h`,
`unistd.h`, `dirent.h`, `pwd.h`, `netinet/in.h`, `sys/time.h`, `uuid/uuid.h`,
`sys/utsname.h` — placed ahead of the system include path and backed by a small
static library. `libpathime_add_win32_compat()` in `cmake/LibpathimeCompat.cmake`
attaches it to a target.

Anything that belongs in a header Windows *does* ship — `alloca`,
`strncasecmp`, POSIX record locking — is injected with `/FI win32_prelude.h`
instead, because those headers must not be shadowed.

This is also where pyzy's UUID provider comes from: the shim forwards to
`Rpcrt4`, so there is no `libuuid` to install.

## 2. Getting pyzy's headers without its directory on the -I path

`engines/pyzy/src/String.h` shadows the C library's `<string.h>` on a
case-insensitive filesystem, so that directory must never appear in an include
path. pyzy's own sources dodge it by sitting *in* `src/` and including each
other with quotes, which resolve relative to the including file.

Anything else reaches them two ways, both configure-time copies out of
`PYZY_SRC_DIR`: the **public** headers are staged into the layout upstream
installs and included as `<PyZy/InputContext.h>`, which is what
`src/CMakeLists.txt` does for the adapter; the **private** ones are
reached through generated forwarding headers that include them by absolute
path, which is what `tests/pyzy/` does. `DataDir.h` is among the public ones —
it is how a program tells pyzy where its data is, and pyzy's only header that
upstream does not also have.

## 3. Build-time behaviour

The codegen tools get an 8 MB stack (they `alloca` in loops) and run with the
CRT in binary mode, because they `fopen(..., "w")` binary dictionaries.
Generated text inputs are written with LF for the same reason — and it is why
the tree itself must be checked out with LF endings (`core.autocrlf input`): a
dictionary source converted to CRLF would leave stray CRs in parsed tokens.

## Linkage: anthy is one DLL, libhangul and pyzy are one each

All three backends produce DLLs under a shared build. anthy is the one that
differs in *shape*: upstream packages it as six libraries that pass global data
between them, so Windows builds the whole family as a **single** DLL rather
than annotating those references. `docs/anthy-mapping.md`, "Why the anthy
family is one library on Windows", has the reasoning; the target names
`anthydic-unicode` and `anthyinput-unicode` survive as interfaces onto that
one DLL, so nothing else in the tree links differently.

No external DLL follows them by default: SQLite links statically from
vcpkg's `x64-windows-static-md` triplet (`LIBPATHIME_STATIC_SQLITE`, ON by
default and in every release), and the glib calls in pyzy's sources are
satisfied by its own `GlibLess` (a titled commit on its `libpathime`
branch), not by a glib DLL. The install still computes the runtime-DLL
closure of every installed target — each joins a CMake runtime dependency
set whose install rule, in `cmake/LibpathimeInstall.cmake`, includes what
resolves from the vcpkg tree and excludes the operating system's own DLLs —
which is also what makes `LIBPATHIME_STATIC_SQLITE=OFF` work: that build
links `sqlite3.dll` and the closure ships it beside the libraries.

## Known build limitation: the Visual Studio generator builds serially

The `windows-msvc` build preset does not pass `/m`, and the reason is a CMake
behaviour rather than anything this port does.

The Visual Studio generator gives **every target in a directory** the same
custom build step, `cmake --check-stamp-file <dir>/CMakeFiles/generate.stamp`,
and that step *rewrites* the stamp — temporary file, then rename over it — even
when it finds nothing out of date. On the first build after a configure there
are no MSBuild tracking logs to skip those steps with, so they all run at once;
up to nine sibling projects share one stamp, the renames collide, and CMake
reads a failed rename as "out of date" and reconfigures the whole project. It is
the concurrent reconfigures, not the stamp, that then fail the build in
`configure_file`. Ninja regenerates through a single rule that cannot overlap,
which is why `windows-ninja` is unaffected.

`/MP` still parallelises compilation within each project (see
`cmake/LibpathimeOptions.cmake`), so a serial MSBuild is slower than Ninja, not
single-threaded.

## Known runtime limitations

Runtime gaps, not build failures; none of them affect the artifacts the build
produces.

- The anthy **runtime** library opens its private-dictionary and history files
  in the CRT's default text mode. The binary-mode fix above is
  deliberately scoped to the codegen executables, which own their process — a
  library must not change `_fmode` under its host. The only observed effect is
  that `textdict`'s newline padding is written twice as long as intended; the
  private-dictionary write/read/delete cycle round-trips correctly, and
  `ctest -R '^anthy\.dicutil$'` covers it.
- `hangul.vendored.unittest` does not run on Windows; `docs/testing.md`
  explains why, and what covers the same ground instead.

## The table engine on Windows

The engine needs nothing structural here — SQLite comes from vcpkg, which pyzy
already requires, and `tools/table-compile` is an ordinary host tool.

### Glyph coverage: why Windows has its own map

Compiled tables are trimmed to what the target can render, from a map checked
into the tree rather than a font read at build time. BUILD.md, "Glyph coverage",
carries the maps, the measurements and the guidance; what is Windows-specific is
why a second map exists at all.

The filter is in practice "drop CJK Extension B and beyond" — and Windows can
draw those. A system with the Chinese language feature installed carries
**SimSun-ExtB** (60,349 supplementary code points on its own) and
**MingLiU-ExtB**; against the whole Windows CJK font set the five shipped tables
lose exactly one character out of 70,948. So the filter is not the bargain here
that it is on Linux, and `LIBPATHIME_TABLE_COVERAGE=none` is a sensible setting
rather than a footgun — it costs about 10 MB of table data and gives back every
Extension B character.

`coverage_data_windows.h`, the default here, is the *conservative* reading: the
union of the in-box faces (SimSun, Microsoft YaHei, Microsoft JhengHei, Yu
Gothic, Malgun Gothic), which a bare en-US install has without any language
feature added. It sits close to the Noto map, because both stop at the BMP; the
Extension B faces are what actually separate the platforms.

### GetFontUnicodeRanges cannot express what this map is deciding about

`tools/generate-coverage.py` reads a font's coverage by parsing its own `cmap`
table, formats 4 and 12, in about fifty lines of stdlib Python — the same code
path on both platforms, with no fontconfig on Linux and no platform library
here.

GDI's `GetFontUnicodeRanges`, the obvious Windows equivalent of `fc-query
--format=%{charset}`, is unusable for it: `GLYPHSET`/`WCRANGE` count UTF-16 code
*units*, so supplementary coverage cannot be expressed — which is precisely the
range the filter decides about. Measured against SimSun-ExtB, whose `cmap`
covers 60,349 supplementary code points, `GetFontUnicodeRanges` reports 97 code
units and *zero* surrogates: exactly the font's 97 BMP characters, with the
entire rest absent. Not surrogate halves needing recombination — simply gone. A
Windows map built from it would exclude Extension B entirely and look plausible
doing it. (DirectWrite's `IDWriteFontFace::GetUnicodeRanges` uses `UINT32` and
does express it, at the price of COM through ctypes.)
