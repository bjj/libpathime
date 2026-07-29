# Third-party components

What libpathime links, what it builds data from, and under what terms.

libpathime itself is MIT (`LICENSE`); everything below carries its own terms.

## Libraries linked at runtime

Each is built as a **shared library** and loaded as one.
`BUILD_SHARED_LIBS=OFF` produces a static build instead — see "Linkage" below.

| Component | Licence | Upstream | We build from |
|---|---|---|---|
| libhangul | LGPL-2.1 | `github.com/libhangul/libhangul` | upstream, unmodified |
| anthy-unicode | LGPL-2.1 — its dictionary sources differ, see "Data files" below | `github.com/fujiwarat/anthy-unicode` | `github.com/bjj/anthy-unicode`, branch `libpathime` |
| pyzy | LGPL-2.1 | `github.com/openSUSE/pyzy` | `github.com/bjj/pyzy`, branch `libpathime` |

**anthy-unicode and pyzy are modified.** Each fork's `libpathime` branch carries
a short series of titled commits on top of an unmodified `main`/`master`;
`git log vendor/<branch>..libpathime` in the submodule is the complete list of
changes, with the date of each. The forks are the corresponding source for the
binaries this project builds.

libhangul is unmodified — we generate its `config.h` and call
`add_subdirectory()` on its own CMake, which changes nothing about the library.

### External dependencies of those libraries

| Component | Licence | Linkage |
|---|---|---|
| GLib | LGPL-2.1-or-later | shared; required by pyzy |
| SQLite3 | public domain | required by pyzy and the table engine |

## Data files shipped in `pathime-data/`

These are data files, compiled at build time from the sources named below and
installed alongside the library. None of them is linked into a binary.

| Shipped file | Built from | Licence of the source |
|---|---|---|
| `anthy/anthy.dic` | anthy-unicode's `alt-cannadic/*` and the `mkworddic/*.t` read by `mkworddic/dict.args` | **GPL-2** |
| `pyzy/main.db` | pyzy's `data/db/android/rawdict_utf16_65105_freq.txt` | LGPL-2.1 |
| `pyzy/phrases.txt` | pyzy's `src/phrases.txt` | LGPL-2.1 |
| `table/*.db` | `engines/ibus-table-chinese` table sources | **GPL-3** |

Either GPL data set can be left out of a build, along with the engine that
reads it: `LIBPATHIME_WITH_ANTHY=OFF` and `LIBPATHIME_WITH_TABLE=OFF`. With
both off, `pathime-data/` holds only pyzy's files.

## Development-time only

| Component | Licence | Used by |
|---|---|---|
| cpp-terminal | MIT | `demo/` (`LIBPATHIME_BUILD_DEMO=ON`) |

Not linked into `libpathime` and not shipped in an install.

## Linkage

The default build (`BUILD_SHARED_LIBS=ON`) produces libhangul, anthy and pyzy
as separate shared libraries beside `libpathime`.

`BUILD_SHARED_LIBS=OFF` builds them into `libpathime` instead. That changes
what is being distributed and the terms that apply to it; anyone shipping such
a build should read the licences above with their own advice.
