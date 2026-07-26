# CMake infra: submodule test status

**Not applied to the repo.** The Windows-port work was in progress in another
session at the time this was written, so these findings live here instead of
in `docs/submodule-testing.md`. Fold this in (and delete this file) once
that's safe.

## What was done

Out-of-source build via the repo's own `CMakeLists.txt`, entirely in `/tmp`,
source tree untouched:

```sh
cmake -S /c/dev/libpathime -B /tmp/libpathime-cmake-build -G Ninja \
  -DLIBPATHIME_BUILD_TESTS=ON -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/libpathime-cmake-build
cd /tmp/libpathime-cmake-build && ctest -N
```

Build: **succeeds**, 89/89 targets, all three backends (`LIBPATHIME_WITH_HANGUL`
/ `_ANTHY` / `_PYZY` all `ON`), including pyzy's `android.db` codegen step
(which, notably, failed when driven by pyzy's own hand-built autotools
`Makefile` — the CMake port's `gen_android_db.py` wiring handles it correctly
where upstream's does not).

Tests: **`ctest -N` → `Total Tests: 0`**. No test binary is produced for any
of the three backends — `bin/` after a full build contains only the anthy
codegen tools (`anthy_calctrans`, `anthy_mkdepgraph`, `anthy_mkfiledic`,
`anthy_mkworddic`); there is no `hangul` Check-suite binary, no anthy
`anthy`/`checklib`/`prediction`/`test-matrix`, no pyzy `basic`.

## Root cause: `LIBPATHIME_BUILD_TESTS` is a no-op

`cmake/LibpathimeOptions.cmake:48` declares:

```cmake
option(LIBPATHIME_BUILD_TESTS "Build submodule test suites where available" OFF)
```

and `BUILD.md`'s options table documents it as if it does something. But
`grep -rn LIBPATHIME_BUILD_TESTS` across every `*.cmake`/`CMakeLists.txt` in
the repo turns up exactly that one declaration — **nothing ever reads the
variable**. Setting it `ON` changes nothing about the build graph. (It didn't
trigger CMake's "manually-specified variables were not used" warning the way
plain `BUILD_TESTING` did, only because `option()` itself counts as "using"
the cache variable — that warning is not a reliable signal here.)

## Per-backend gap detail

**libhangul** — upstream ships real CMake test wiring
(`libhangul/test/CMakeLists.txt:54` has `add_test(NAME unittest ...)`, gated
by `libhangul/CMakeLists.txt:65 if(BUILD_TESTING)`), but the top-level
`CMakeLists.txt:37` deliberately bypasses it:
`add_subdirectory(libhangul/hangul)` descends straight into the library
directory and never adds `libhangul` itself as a subdirectory (see the
comment at the top of the file — this is intentional, to dodge `po/`/`tools/`
needing `msgfmt`). Because `libhangul`'s own top-level `CMakeLists.txt` is
never processed, `BUILD_TESTING` has no `test/` subdirectory to gate — hence
CMake's "unused variable" warning for it. Confirmed by hand-build in the
sibling doc: this suite passes cleanly (1/1) under autotools/Check.

**anthy-unicode** — `cmake/ports/anthy-unicode/CMakeLists.txt` reproduces the
meson build's libraries and the four-stage dictionary codegen, but has no
equivalent of `test/meson.build`'s four test executables
(`test/main.c`→`anthy`, `check.c`→`checklib`, `prediction.c`, `test-matrix.c`).
None of the `test/` sources are referenced anywhere in the port. Confirmed by
hand-build: all four binaries build and exit 0 under autotools.

**pyzy** — `cmake/ports/pyzy/CMakeLists.txt` builds `libpyzy-1.0` and the
Android DB, but never touches `src/tests/`. No equivalent of the `basic`
test executable exists in the port. Note this one has an extra wrinkle found
during the hand-build: pyzy's own `src/tests/basic.cc` currently fails to
compile against `src/InputContext.h` at the pinned submodule commit —
`DummyObserver` doesn't implement all of `PyZy::InputContext::Observer`'s pure
virtual methods (`commitText`, `inputTextChanged`, `cursorChanged`,
`preeditTextChanged`, `auxiliaryTextChanged`, `candidatesChanged`), so it's
abstract and can't be instantiated. Wiring pyzy's test into CMake will hit
this same compile error immediately — it's an upstream-test bug, not a CMake
problem, but worth knowing before someone spends time on the CMake side only
to hit this next.

## What wiring up `LIBPATHIME_BUILD_TESTS` would need

1. **libhangul**: either descend into the full `libhangul` top-level
   `CMakeLists.txt` with `BUILD_TESTING=ON` (reintroducing the `po`/`tools`
   dependency problem the current bypass avoids — would need scoping), or
   hand-roll a `test` target from `libhangul/test/test.c` + Check, mirroring
   what the top-level file already does for `hangul/`.
2. **anthy-unicode**: add targets for the four `test/*.c` sources in the
   native port, gated by `if(LIBPATHIME_BUILD_TESTS)`, linked against the
   already-built `anthy-unicode`/`anthydic-unicode` libs, registered via
   `add_test()`. Needs `CONFFILE`/`ANTHYDIR`/dict paths pointed at the build
   tree's `dic/` output (see `anthy-unicode-test.conf` generation pattern in
   the autotools build for the shape of this).
3. **pyzy**: add `src/tests/basic.cc` as a target gated by
   `LIBPATHIME_BUILD_TESTS`, registered via `add_test()` — but fix (or get
   upstream to fix) the `DummyObserver` abstract-class issue first, or the
   target won't compile.

None of this has been implemented — this file only documents the gap for
follow-up after the Windows port lands.
