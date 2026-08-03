# Changelog

One dated entry per release. Everything else in this repository stays in the
present tense and describes what is; this file is the one place that says
when.

## 0.1.3 — 2026-08-03

No artifact ships an external shared library. pyzy provides the glib calls
its sources make itself (`GlibLess`, on the fork's `libpathime` branch),
which removes glib — and on Windows its whole DLL closure: iconv, intl,
pcre2, with their LGPL relinking obligations — from every platform; pyzy's
own autotools build keeps real glib and is unchanged. On Windows SQLite
links statically from vcpkg's `x64-windows-static-md` triplet
(`LIBPATHIME_STATIC_SQLITE`, ON by default; OFF links and ships
`sqlite3.dll` instead), so the Windows packages carry only libpathime's own
five binaries. POSIX artifacts still link the system sqlite3 and ship
nothing; their dependency list is down to libsqlite3 and libuuid. A new
`pyzy.glibless` suite pins the shim's contracts. No API change.

## 0.1.2 — 2026-08-02

The distribution boundary hardened, following an external review of this
repository and both bindings. The shared library's SONAME is
`libpathime.so.0.1` — pre-1.0 it tracks the minor, which is the promise the
CMake package file already made; programs linked against an earlier 0.1
library relink at their next build. Binary archives unpack into a single
top-level directory named after themselves; the release carries an attested
`SHA256SUMS`; release binaries are tested before packaging, and ARM Linux and
the CMake 3.21 floor joined regular CI. The version is defined once, in
`pathime.h`, and the build parses it — 0.1.1 shipped reporting itself as
0.1.0, the class of drift this removes. The project description no longer
names IBus. The bindings release in lockstep from this version on;
RELEASING.md has the order.

## 0.1.1 — 2026-08-01

macOS (arm64) is a supported platform, and `libpathime-*-macos-arm64` /
`pathime-demo-*-macos-arm64` join the release artifacts. The port is the
ordinary POSIX path plus a Darwin static-build fallback, with glib from
Homebrew and sqlite3 and the UUID functions from the system; the full test
suite passes in both link modes. No behaviour change on Linux or Windows.

## 0.1.0 — 2026-08-01

The first release. A CJK input method engine as a plain C library: Korean
(libhangul), Japanese (anthy-unicode), Chinese Pinyin/Bopomofo (pyzy), and a
table-driven engine of our own reading the ibus-table format (Wubi, Cangjie,
Quick, Stroke, Zhuyin), behind one synchronous C API with a phone-keyboard
composition model. Linux and Windows; an interactive terminal demo; per
platform, a library package for embedders and a standalone demo package, plus
a complete source tarball. Both binary packages are GPL-3 as a whole —
THIRD-PARTY.md has the component terms and the flags that build
differently-licensed trees.
