# Testing

Everything under `tests/`, how to run it, and the things about it that are
deliberate and should not be "fixed".

## Running the suites

Tests are off by default — they need the build-time data (`anthy.dic`,
`android.db`) and take considerably longer than the libraries.

```bash
cmake --preset linux-release -DLIBPATHIME_BUILD_TESTS=ON
cmake --build --preset linux-release
ctest --test-dir build/linux-release --output-on-failure
```

On Windows add `-C Release` to the `ctest` line; the Visual Studio generator is
multi-config. Tests are named `<area>.<name>`, so `ctest -R '^anthy\.'` runs one
backend's suite and `ctest -R '^api\.'` runs the public-API tests.

**Skip protocol.** A test whose subject is not implemented yet, or whose data is
absent, exits 77 — `SKIP_RETURN_CODE`, which ctest reports as *skipped* rather
than passed or failed. As implementation lands, replace the skip with real
checks. Never delete a registration.

## The four kinds of test

### `tests/api/` — `api.<name>`

Links the built library and touches only exported symbols. Plain C11 against
`<pathime/pathime.h>`, which doubles as proof that the header works from strict
C and that every symbol a client needs is actually exported. Holds the ABI,
lifecycle and options suites, plus one end-to-end test per engine that types
real Korean, Japanese and Chinese through the public API.

Nothing here may link a backend library. The public header is the whole
interface these tests are entitled to, and on Windows linking anthy alongside
libpathime is actively broken — see "Runtime data" below.

### `tests/core/` — `core.<name>`

Compiles the internal sources under test directly into each executable, because
internal helpers carry no `PATHIME_API` and a shared build does not export them.
C++17, covering `utf8.*`, `composition.*` and the options machinery at seams the
public API cannot reach.

### `tests/<backend>/` — `<backend>.vendor*`

The submodule's own suite. None of the three upstream build systems is used by
this build, so the wiring is reproduced rather than reused, and the vendored
sources are compiled where they sit.

### `tests/<backend>/` — `<backend>.<name>`

Tests written for libpathime against a backend directly. These exist to make the
Windows build prove it behaves like the Linux one, so all of them must build and
pass on both.

## Runtime data: a test-environment problem, not a library one

Read this before "fixing" anything in `src/` because a test would not otherwise
run. In a real installation both backends find their own data and none of the
following is needed; in a build tree neither backend's data is at its installed
location. The three end-to-end engine tests are therefore the only ones under
`tests/api/` gated on a `LIBPATHIME_WITH_*` option, and the library must not
grow code to hunt for data files itself.

Each test reaches its data the way an installation would:

- **anthy** reads `CONFFILE` at `anthy_init()` time and takes `DIC_FILE` from
  it. `tests/api/CMakeLists.txt` writes a second, build-tree conf file and names
  it through the `CONFFILE` environment variable. It cannot instead call
  `anthy_conf_override()` the way `tests/anthy/` does, because those calls would
  have to reach the copy of anthy *inside* libpathime and on Windows there is no
  route to it: anthy is static there, so a test executable that also linked it
  would get a second copy with its own conf database. On ELF the two resolve to
  a single definition, which is why the same code passed on Linux and could not
  work on Windows. See `docs/anthy-mapping.md`, "Why the anthy family is built
  static on Windows".
- **pyzy**'s `Database::open()` searches its compiled-in `PKGDATADIR` and then
  `main.db` relative to the working directory. There is no API to point it
  elsewhere, so `android.db` is staged into a per-test run directory that the
  test is given as its `WORKING_DIRECTORY`. A run without it does not fail
  loudly — pyzy warns on stderr, reports success anyway, and produces zero
  candidates — so a test that skipped this would pass vacuously. That is why the
  engine tests name several expected candidates rather than one.

Both tests are additionally confined to their own `data_dir` under the build
tree, so a run never touches the developer's real `~/.config/anthy` or pyzy user
database. Both backends learn on commit, so each has a `.clean` fixture test
that wipes that directory before the run — without it a run would be graded
against whatever the previous run taught it.

## Conditional registrations

Before reading anything into a test's absence:

- **`api.engine_pyzy_nodb`** is the only test whose *registration* depends on
  the machine rather than the build. It asserts that a pyzy which cannot find a
  database reports itself absent through `pathime_has_engine()` instead of
  pretending to work — which is only a true statement when no database is
  reachable. The working directory is one this build creates and never stages a
  `main.db` into, but `PKGDATADIR` is not ours to control, so it is probed at
  configure time and the test is not registered when a system-wide pyzy is
  installed. CMake says so at `STATUS`. Do not "fix" its absence on such a
  machine.
- **`hangul.vendored.unittest`** needs the Check framework *and* a 32-bit
  `wchar_t`, so in practice it runs on Linux and not on Windows. Upstream's
  `test.c` compares UCS-4 output with `wcscmp((const wchar_t *) …)`, which on
  Windows' 16-bit `wchar_t` compares one character and stops — it would pass
  while testing almost nothing. `hangul.ic` restates the same expectations
  against `ucschar`, so nothing is lost on Windows but the vendored suite
  itself.
- **`hangul.vendored.hangul`** needs iconv, which it uses only to print UCS-4 as
  UTF-8. glibc has it; on Windows it comes from vcpkg, which glib pulls in
  anyway.

## What is deliberately not tested

`pyzy`'s own `src/tests/basic.cc` is not wired up: its `DummyObserver` declares
the `InputContext::Observer` callbacks with the wrong constness, so nothing
overrides, the class stays abstract, and the file has not compiled in a long
time. What it was written to check is covered by the tests in `tests/pyzy/`,
which take their expected values from it.

One upstream bug has no test because it cannot have one:
`anthy_dic_util_set_personality()` aborts the process on both Linux and
Windows — `anthy_dic_set_personality()` replaces the record and personal-dic
cache that `anthy_dic_util_init()` installed without releasing them, and the
orphan is freed twice at teardown. See `tests/anthy/test_dicutil.c`.
