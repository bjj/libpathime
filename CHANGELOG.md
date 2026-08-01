# Changelog

One dated entry per release. Everything else in this repository stays in the
present tense and describes what is; this file is the one place that says
when.

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
