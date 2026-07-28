# The Windows port

How `libpathime` and its three vendored submodules build and behave on Windows.
`BUILD.md` is what to install and type; this is what the build is doing behind
that and where the result still differs from Linux.

Verified on Windows 11 with Visual Studio 2022 (MSVC 19.44) and with
clang-cl 19 + Ninja, x64, against vcpkg's glib 2.88 and sqlite3 3.53.
Both presets produce identical `anthy.dic`, `android.db` and compiled
`table/*.db`, and both pass the full test suite — which is the real check that
the workarounds below preserve Linux behaviour rather than merely compiling.

## The rule

Nothing under `engines/` — `libhangul/`, `anthy-unicode/`, `pyzy/` — is ever
edited. A
vendored source that will not compile as it stands is handled by one of the
three mechanisms below, never by a patch to the submodule tree.

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

Where a vendored file is not valid on an MSVC-style compiler, or cannot be told
something libpathime needs to tell it, the port compiles a fixed *copy* produced
at configure time. Editing a submodule therefore needs a re-run of CMake for the
change to reach the copy.

Two of them are not Windows-specific and are generated on every platform:

- `engines/anthy-unicode/src-diclib/conf.c` expands `${NAME}` references inside every
  value stored, and reads its compiled-in conf file unconditionally. The copy
  stores `anthy_conf_override()` values verbatim and treats an empty `CONFFILE`
  as "there is no conf file", which together make the override API an exact and
  complete way to configure anthy.
- `engines/pyzy/src/Database.cc` and `SpecialPhraseTable.cc` name their data by a
  compiled-in `PKGDATADIR` and by the process's working directory. The copies
  take it from `pyzy_set_data_dir()` instead — `DataDir.h`, which the port adds
  to pyzy.

The Windows-only ones:

- `engines/anthy-unicode/src-diclib/alloc.c` casts heap pointers through `unsigned
  long`, which is 32-bit under LLP64; the copy uses `uintptr_t`.
- `engines/anthy-unicode/src-diclib/file_dic.c` walks paths POSIX-style and so cannot
  create `%USERPROFILE%\.config\anthy`; the copy teaches it drive letters and
  `\`. This is what lets `pathime_init_params_t::data_dir` name a multi-level
  Windows path that does not exist yet.
- `engines/anthy-unicode/src-diclib/filemap.c` opens the dictionary with the narrow
  `open()`, which decodes its argument in the active code page; the copy
  converts from UTF-8 and calls `_wopen`, so an install path outside that code
  page still works.
- `engines/pyzy/src/`'s Windows fixes ride on the same mirror as the data-directory
  change above. `PinyinParserTable.h` uses GNU labelled-field initialisers;
  `String.h` is missing `operator<<` overloads that only LP64 made unnecessary;
  `PhraseEditor.h` forward-declares `class Config` where it is a `struct`,
  which MSVC's mangling notices; and `BopomofoContext.cc` casts a
  `const wchar_t *` to UCS-4, which is only correct where `wchar_t` is 32 bits.

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

## Linkage: anthy is static, libhangul and pyzy are not

On Windows the anthy family is built **static** regardless of
`BUILD_SHARED_LIBS`. The reason is anthy's own — cross-library global data — and
is set out in `docs/anthy-mapping.md`, "Why the anthy family is built static on
Windows", along with the constraint it imposes: **nothing that links
`libpathime` may also link anthy.** That constraint is why nothing under
`tests/api/` links a backend library; see `docs/testing.md`.

libhangul and pyzy still produce DLLs under a shared build.

## Known runtime limitations

Runtime gaps, not build failures; none of them affect the artifacts the build
produces.

- The anthy **runtime** library still opens its private-dictionary and history
  files in the CRT's default text mode. The binary-mode fix above is
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

Built and tested here, on both presets: 33 suites pass under MSVC and under
clang-cl + Ninja, and the two toolchains produce byte-identical compiled tables.
It needed nothing structural — SQLite comes from vcpkg, which pyzy already
requires, and `tools/table-compile` is an ordinary host tool.

### Glyph coverage: two maps, and why Windows is the interesting one

Compiled tables are trimmed to what the target can render (BUILD.md, "Glyph
coverage"), from a map checked into the tree rather than a font read at build
time. Windows now has its own map, `coverage_data_windows.h`, selected by
`LIBPATHIME_TABLE_COVERAGE`, which defaults to `windows` here and `noto`
elsewhere.

The measurement that shaped it: the filter is in practice "drop CJK Extension B
and beyond" — 40,603 of the 40,686 characters the Noto map removes are
supplementary-plane. And Windows can draw them. A system with the Chinese
language feature installed carries **SimSun-ExtB** (60,349 supplementary code
points on its own) and **MingLiU-ExtB**; against the whole Windows CJK font set
the five shipped tables lose exactly one character out of 70,948. So on Windows
the filter is not the same bargain it is on Linux, and
`LIBPATHIME_TABLE_COVERAGE=none` is a sensible setting rather than a footgun —
it costs about 10 MB of table data and gives back every Extension B character.

The `windows` map is therefore the *conservative* reading: the union of the
in-box faces (SimSun, Microsoft YaHei, Microsoft JhengHei, Yu Gothic, Malgun
Gothic), which a bare en-US install has without any language feature added. It
drops 38.5% of rows against Noto's 36.6% — close, because both maps stop at the
BMP; the Extension B faces are what actually separate the platforms.

### GetFontUnicodeRanges does not work for this, and the earlier brief was wrong

`tools/generate-coverage.py` used to say the Windows equivalent of `fc-query
--format=%{charset}` was GDI's `GetFontUnicodeRanges`, warning that `WCRANGE` is
in UTF-16 code *units* so supplementary coverage would arrive as surrogate halves
needing recombination.

Measured, it is worse than that: **supplementary coverage does not arrive at
all.** Against SimSun-ExtB, whose `cmap` covers 60,349 supplementary code points,
`GetFontUnicodeRanges` reported 97 code units and *zero* surrogates — exactly the
font's 97 BMP characters, with the entire rest silently absent. A generator built
to that brief would have produced a Windows map that excluded the one range the
whole filter is deciding about, and it would have looked plausible.

`read_charset()` now parses the font's own `cmap` table, formats 4 and 12, in
about fifty lines of stdlib Python. That is correct above the BMP, needs nothing
installed on either platform, and removed the `sys.platform` dispatch rather than
adding to it — so the generator is no longer Linux-only and there is no
fontconfig dependency left. (DirectWrite's `IDWriteFontFace::GetUnicodeRanges`
uses `UINT32` and would have worked, at the price of COM through ctypes.)
