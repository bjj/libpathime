# Third-party components

What libpathime links, what it builds data from, what it ships, and under what
terms. Written artifact-relative: this same file appears in the repository, in
both binary packages, and in the source tarball, so every statement says
*where* a component lands rather than assuming one context.

libpathime itself is MIT (`LICENSE`); everything below carries its own terms.

## The artifacts

A release publishes three things per platform–architecture pair:

- the **library package** (`libpathime-<ver>-<os>-<arch>`): headers, the
  libraries, the CMake package and `pathime.pc`, and `pathime-data/`. For
  developers embedding libpathime.
- the **demo package** (`pathime-demo-<ver>-<os>-<arch>`): the `pathime-demo`
  executable, the libraries it needs, and `pathime-data/`. Standalone —
  download, unpack, run.
- the **source tarball**: the repository tree with every submodule populated
  at its pinned commit. This is the corresponding source for both binary
  packages.

Both binary packages install the licence texts of everything they contain
under `share/doc/pathime/licenses/`.

**Both binary packages are GPL-3 as a whole.** They ship `table/*.db` (GPL-3)
and `anthy/anthy.dic` (GPL-2) alongside the MIT and LGPL binaries; the data is
most of the bytes. A consumer who wants different terms builds their own with
`LIBPATHIME_WITH_ANTHY=OFF` and/or `LIBPATHIME_WITH_TABLE=OFF` — with both
off, `pathime-data/` holds only pyzy's LGPL files.

## Where each component lands

| Component | Licence | Library pkg | Demo pkg | Source tarball |
|---|---|---|---|---|
| libpathime | MIT | ✓ | ✓ | ✓ |
| libhangul (shared library) | LGPL-2.1 | ✓ | ✓ | ✓ |
| anthy-unicode (shared library) | LGPL-2.1 | ✓ | ✓ | ✓ |
| pyzy (shared library) | LGPL-2.1 | ✓ | ✓ | ✓ |
| `pathime-data/pyzy/*` | LGPL-2.1 | ✓ | ✓ | as sources |
| `pathime-data/anthy/anthy.dic` | **GPL-2** | ✓ | ✓ | as sources |
| `pathime-data/table/*.db` | **GPL-3** | ✓ | ✓ | as sources |
| cpp-terminal (static, inside `pathime-demo`) | MIT | — | ✓ | ✓ |
| SQLite (static, inside the Windows `pathime` and `pyzy` libraries) | public domain | Windows only | Windows only | — |

No artifact ships an external shared library. The two places one could enter
are both closed off: SQLite, which pyzy and the table engine need and Windows
has no system copy of, links statically there from vcpkg's
`x64-windows-static-md` triplet (POSIX links the system copy and ships
nothing); and glib, whose handful of calls pyzy satisfies itself
(`src/GlibLess.*` on the fork's `libpathime` branch), so neither glib nor
glib's own closure (libiconv, libintl, pcre2) appears on any platform.

## The vendored libraries

| Component | Licence | Upstream | We build from |
|---|---|---|---|
| libhangul | LGPL-2.1 | `github.com/libhangul/libhangul` | upstream, unmodified |
| anthy-unicode | LGPL-2.1 — its dictionary sources differ, see "Data files" below | `github.com/fujiwarat/anthy-unicode` | `github.com/bjj/anthy-unicode`, branch `libpathime` |
| pyzy | LGPL-2.1 | `github.com/openSUSE/pyzy` | `github.com/bjj/pyzy`, branch `libpathime` |
| ibus-table-chinese | GPLv3 | `github.com/mike-fabian/ibus-table-chinese` | upstream, unmodified |

**anthy-unicode and pyzy are modified.** Each fork's `libpathime` branch carries
a short series of titled commits on top of an unmodified `main`/`master`.
The forks — and, at a release, the source tarball — are the corresponding
source for the binaries this project builds.

libhangul is unmodified — we generate its `config.h` and call
`add_subdirectory()` on its own CMake, which changes nothing about the library.

cpp-terminal (`github.com/jupyter-xeus/cpp-terminal`, MIT, unmodified) is the
demo's terminal layer, linked statically into `pathime-demo` and shipped only
in the demo package. It is not linked into `libpathime`.

### External dependencies of those libraries

| Component | Licence | Linkage |
|---|---|---|
| SQLite | public domain | required by pyzy and the table engine. On Windows the vcpkg static build (`x64-windows-static-md`) is compiled into the libraries; on POSIX the system shared library is linked and nothing ships |
| libuuid | Modified BSD | required by pyzy on POSIX; Windows uses a bundled Rpcrt4 shim instead |

## Data files shipped in `pathime-data/`

These are data files, compiled at build time from the sources named below and
shipped in both binary packages. None of them is linked into a binary.

| Shipped file | Built from | Licence of the source |
|---|---|---|
| `anthy/anthy.dic` | anthy-unicode's `alt-cannadic/*` and the `mkworddic/*.t` read by `mkworddic/dict.args` | **GPL-2** |
| `pyzy/main.db` | pyzy's `data/db/android/rawdict_utf16_65105_freq.txt` | LGPL-2.1 |
| `pyzy/phrases.txt` | pyzy's `src/phrases.txt` | LGPL-2.1 |
| `table/*.db` | `engines/ibus-table-chinese` table sources | **GPL-3** |

Either GPL data set can be left out of a build, along with the engine that
reads it: `LIBPATHIME_WITH_ANTHY=OFF` and `LIBPATHIME_WITH_TABLE=OFF`.

## Linkage

The default build (`BUILD_SHARED_LIBS=ON`) produces libhangul, anthy and pyzy
as separate shared libraries, installed into a private `pathime/` directory
beside `libpathime` (`BUILD.md`, "What gets produced"). Being separate
replaceable files is what makes them straightforward to satisfy the LGPL for:
the private directory changes where they sit, not what they are. Every
released binary package is this shared arrangement.

`BUILD_SHARED_LIBS=OFF` does not fold them into `libpathime`. It builds each one
as its own static archive, installs the archives into the same private
`pathime/` directory, and names them in `pathime.pc`'s `Libs.private` — so they
reach the **embedder's** link line and their code ends up inside whatever
program links libpathime. `libpathime.a` itself holds only this project's own
objects.

That changes what is being distributed and the terms that apply to it: the
relinking clause is now something the embedder's own binary has to satisfy,
rather than something the arrangement of files satisfies for them. No release
artifact is a static build; anyone shipping one should read the licences above
with their own advice.
