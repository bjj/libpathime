# Submodule Test Suites

Research notes on how each vendored submodule's own upstream test suite is structured and
invoked. This is **discovery only** — nothing here has been built or executed as part of
producing this document. Findings are based on reading each submodule's build files, test
sources, and CI configs as checked out at the commits noted below.

None of the three submodules run their tests through the parent repo's CMake build
(`cmake/LibpathimeCompat.cmake` / `cmake/ports/*`); that build only compiles the libraries.
Running a submodule's own test suite means using *that submodule's own* build system
(autotools, and in two cases also CMake/Meson) from within its directory.

---

## libhangul (`libhangul/`)

- **Pinned commit:** `a34aef73378c0992316861bbf13fc914ee7577d9` (`libhangul-0.2.0-5-ga34aef7`)
- **Upstream:** https://github.com/libhangul/libhangul

### Build systems
- **Autotools** (canonical, per README): `configure.ac`, `Makefile.am` (root, `hangul/`, `data/`,
  `tools/`, `po/`, `test/`, `doc/`, `bindings/`), `autogen.sh`.
- **CMake** (parallel): `CMakeLists.txt`, `hangul/CMakeLists.txt`, `test/CMakeLists.txt`.
- A `.vcxproj` also exists for Visual Studio but defines no test target.

### Test code and framework
- Tests live in `test/`: `test.c` is the real unit-test suite; `hangul.c` and `hanja.c` are
  small standalone demo programs, not part of the automated suite.
- Framework: **[Check](https://libcheck.github.io/check/)**, the C unit-test library
  (`#include <check.h>` in `test/test.c`), located via `PKG_CHECK_MODULES([CHECK], [check])`
  in `configure.ac`.
- The top-level `Makefile.am`'s `SUBDIRS` does **not** include `test` — it's only built via
  `make check` (automake's `TESTS`/`check_PROGRAMS` mechanism), not `make`/`make all`.

### How to run (autotools)
```sh
./autogen.sh
./configure
make
make check
```
(`make check` is inferred from `test/Makefile.am`'s `TESTS = test` / `check_PROGRAMS = test`;
README only documents `autogen.sh && configure && make`, not the test step explicitly.)

### How to run (CMake)
```sh
cmake -B build -DENABLE_UNIT_TEST=ON   # ON is the default
cmake --build build
ctest --test-dir build
```
`test/` is added with `EXCLUDE_FROM_ALL`, so it's only built as part of the CTest target, not
the default `all` target.

### CI
None — no `.github/workflows` or other CI config exists in this submodule.

### Dependencies
`autoconf`, `automake`, `libtool`, `pkg-config`, **libcheck** (`check.pc`), `gettext`
(`AM_GNU_GETTEXT` 0.22.5). Optional: `expat` for `--enable-external-keyboards`.

Test binaries expect data paths supplied via compile defs (`TEST_LIBHANGUL_KEYBOARD_PATH` →
`data/keyboards`, `TEST_HANJA_TXT` → `data/hanja/hanja.txt`, `TEST_SOURCE_DIR` → `test/`) — set
up automatically by both build systems, but relevant if invoking the test binary directly.

---

## anthy-unicode (`anthy-unicode/`)

- **Pinned commit:** `44a16491df37a0f067e7a431ad1acd3ab4e9cda8` (`1.0.0.20260213-1-g44a1649`)
- **Upstream:** https://github.com/fujiwarat/anthy-unicode

### Build systems
- **Autotools**: `configure.ac`, `Makefile.am` (root and per-subdir, incl. `test/Makefile.am`),
  `autogen.sh`.
- **Meson**: `meson.build` (requires meson ≥ 0.59), `meson_options.txt`, `test/meson.build`.

### Test code and framework
- Tests live in `test/`, each a hand-rolled C program with its own `main()` (no external test
  framework):
  - `main.c` → `anthy` executable — reads `test.txt` input, compares against `test.exp`
    expected output; supports `--all`, `--from`, `--to`, `--verbose`, `--print-miss-only`,
    `--ask`.
  - `check.c` → `checklib` executable — library load smoke test.
  - `prediction.c` → `prediction` executable — prediction feature tests.
  - `test-matrix.c` → `test-matrix` executable — matrix tests.
- **Meson** wires these up as real tests (`test/meson.build`):
  ```
  test('Main anthy tests', anthy, args: ['--all'])
  test('Library load tests', checklib)
  test('Anthy prediction tests', prediction)
  test('Anthy matrix tests', test_matrix)
  ```
- **Autotools** only declares them as `noinst_PROGRAMS` in `test/Makefile.am` — there's no
  `TESTS =` variable, so `make check` does **not** auto-run them under autotools; they must be
  invoked manually.

### How to run (autotools, manual — per `INSTALL` and CI)
```sh
./autogen.sh          # or ./configure directly from a release tarball
make
cd test
./anthy --all
./checklib
```

### How to run (Meson, inferred — not documented in README/INSTALL)
```sh
meson setup builddir
meson compile -C builddir
meson test -C builddir
```

### CI
Travis CI (`.travis.yml`), "Build" job:
```yaml
- ./autogen.sh --enable-installed-tests --disable-static
- make VERBOSE=1 DESTDIR="$HOME/build/$USER/dest"
- sed -e "s|@datadir@|$PWD|" -e "s|@PACKAGE@|mkanthydic|" anthy-unicode.conf.in > test.conf
- cd test
- ./anthy --all
- ./checklib
- cd ../src-util
- ./anthy-dic-tool-unicode --load dic-tool-input
- diff $HOME/.config/anthy/private_words_default dic-tool-result
- ./anthy-dic-tool-unicode --dump
- mkdir -p $HOME/.anthy
- mv $HOME/.config/anthy/private_words_default $HOME/.anthy
- ./anthy-dic-tool-unicode --migrate
- diff $HOME/.config/anthy/private_words_default dic-tool-result
```
Env: `LD_LIBRARY_PATH="$PWD/src-main/.libs:$PWD/src-worddic/.libs"`, `CONFFILE="$PWD/test.conf"`.
Note CI also exercises `src-util`'s dictionary tool beyond the `test/` binaries.

### Dependencies
`autopoint`, `gettext`, `pkg-config`, `emacs-el`, `xemacs21-supportel` (from Travis apt list),
standard C toolchain, autotools. `configure.ac` also probes for `emacs`/lispdir via
`AM_PATH_LISPDIR`.

---

## pyzy (`pyzy/`)

- **Pinned commit:** `5ac51d833777a881e80f0b23d704345cf0feb0d0` (`1.1-8-g5ac51d8`)
- **Upstream:** https://github.com/openSUSE/pyzy

### Build system
- **Autotools only** inside the submodule: `configure.ac`, `Makefile.am` (root, `src/`,
  `src/tests/`, `data/`, `docs/`, `m4/`), `autogen.sh` (wraps `gnome-autogen.sh`, needs
  `gnome-common`).
- No CMake/Meson exists inside `pyzy/` itself. The parent repo's
  `cmake/ports/pyzy/CMakeLists.txt` adds a hand-written CMake port (its own comment: "pyzy
  ships only autotools") but that port only builds the library — it does not wire up or run
  pyzy's test binary.

### Test code and framework
- Single test source: `src/tests/basic.cc` (~1090 lines), a plain C++ program with its own
  `int main()` calling `testFullPinyin()`, `testDoublePinyin()`, `testBopomofo()`,
  `testCommit()`, etc.
- No gtest/CppUnit — assertions use **GLib's** `g_assert` / `g_assert_cmpint` /
  `g_assert_cmpstr` (plus a local `g_assert_cmpstring` wrapper). The process aborts with a
  non-zero exit on failure, which automake's `TESTS` runner treats as a failing test.
- Wired into automake in `src/tests/Makefile.am`: `TESTS = basic`, `noinst_PROGRAMS = $(TESTS)`,
  linked against `libpyzy-@PYZY_API_VERSION@.la`.
- The `tests` subdir is only entered if `--enable-tests` was passed to `configure`
  (`src/Makefile.am`, gated by `configure.ac`'s `AC_ARG_ENABLE(tests, ...)`) — **default is
  off**.

### How to run (inferred; no README/INSTALL documents this)
```sh
./autogen.sh                  # needs gnome-common
./configure --enable-tests    # tests are opt-in, default off
make
make check                    # runs src/tests/basic via the automake TESTS harness
```

### CI
None — no `.github/workflows`, `.travis.yml`, or `appveyor.yml` exist in this submodule.

### Dependencies
`glib-2.0 >= 2.24.0` (also required at test runtime for `g_assert*`), `sqlite3`, libuuid (or
libc `uuid_create`), autoconf/automake/libtool, `gnome-common`, pkg-config. Optional:
`--enable-boost`, `--enable-opencc`.

The test binary writes/removes a scratch sqlite DB directory at
`$TMPDIR/__pyzy_test_dir__` (`src/tests/basic.cc`) during `InputContext::init` — relevant if
running the test binary directly outside of a clean CI-style environment.

---

## Summary table

| Submodule       | Primary build system(s) | Test framework          | Wired to `make check`? | CI config      |
|------------------|--------------------------|--------------------------|--------------------------|-----------------|
| libhangul        | Autotools, CMake         | Check (libcheck)         | Yes (autotools + CTest) | None            |
| anthy-unicode    | Autotools, Meson         | Custom (`main()` + diff) | No (autotools); yes (Meson) | Travis (`.travis.yml`) |
| pyzy             | Autotools only           | Custom (`main()` + GLib asserts) | Yes, but opt-in (`--enable-tests`) | None            |
