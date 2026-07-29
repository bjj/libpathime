# The Windows port

How `libpathime` and the three vendored input-method libraries it wraps build
and behave on Windows. `BUILD.md` is what to install and type; this is what the
build is doing behind that and where the result differs from Linux.

Verified on Windows 11 with Visual Studio 2022 (MSVC 19.44) and with
clang-cl 19 + Ninja, x64, against the vcpkg glib and sqlite3 the port was
developed with (2.88 and 3.53). Both presets produce identical `anthy.dic`,
`android.db` and compiled `table/*.db`, and both pass the full test suite —
which is the real check that the workarounds below preserve Linux behaviour
rather than merely compiling.

## The rule

A vendored source that will not compile or behave as it stands is fixed **in
the submodule**, as a titled commit on its `libpathime` branch — never by a
rewrite applied while the build runs. `libhangul/` and the
`ibus-table-chinese/` table sources need nothing; `anthy-unicode/` and `pyzy/`
each carry a short series of portability fixes on that branch.

What stays here is what is *not* a fix to anthy or pyzy: the compat layer
below, the build files, and the one configure-time rewrite that expresses
libpathime's own contract with anthy rather than a bug in it.

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

## 2. Generated source variants

Where a vendored library cannot be *told* something libpathime needs to tell
it, the port compiles a fixed *copy* produced at configure time. Editing a
submodule therefore needs a re-run of CMake for the change to reach the copy.

Both are about where a library finds its data, both apply on every platform,
and neither is a bug in the library it rewrites — which is why they are here
rather than in the submodule:

- `engines/anthy-unicode/src-diclib/conf.c` expands `${NAME}` references inside every
  value stored, and reads its compiled-in conf file unconditionally. The copy
  stores `anthy_conf_override()` values verbatim and treats an empty `CONFFILE`
  as "there is no conf file", which together make the override API an exact and
  complete way to configure anthy.
- `engines/pyzy/src/Database.cc` and `SpecialPhraseTable.cc` name their data by a
  compiled-in `PKGDATADIR` and by the process's working directory. The copies
  take it from `pyzy_set_data_dir()` instead — `DataDir.h`, which the port adds
  to pyzy.

The portability fixes each library needs in order to compile and behave on
Windows at all are commits on its submodule's `libpathime` branch, not
rewrites: pointer arithmetic through `unsigned long` under LLP64, the
POSIX-only path walk that cannot create `%USERPROFILE%\.config\anthy`, the
narrow `open()` that decodes in the active code page, pyzy's GNU labelled-field
initialisers, its missing `operator<<` overloads, its `class`/`struct`
mismatch, and its `wchar_t`-as-UCS-4 cast. `git log` in each submodule is the
list.

Consumers of pyzy's headers must use `PYZY_EFFECTIVE_SRC_DIR` rather than
`engines/pyzy/src`: the mirror is what the library was actually built from, on every
platform. `src/CMakeLists.txt` does this when it stages the public headers for
the adapter.

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
- `cmake --install` leaves a second copy of `hangul.dll` in `lib/` — upstream's
  install rule uses one bare `DESTINATION`. The usable copy in `bin/` is added
  by this build.
- The installed tree does not carry pyzy's vcpkg runtime dependencies
  (`glib-2.0-0.dll` and friends). They are staged into the build tree's `bin/`
  by vcpkg's applocal step; deploying an install needs them copied alongside.
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
