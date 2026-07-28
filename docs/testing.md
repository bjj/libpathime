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

## Runtime data

Every test finds its data the way a client does, and nothing in `tests/` sets an
environment variable or a working directory to arrange it.

- **Under `tests/api/`**, the engines read the `pathime-data/` that
  `src/CMakeLists.txt` stages beside the built library, which is where
  `pathime_init_params_t::resource_dir` looks by default. Those tests link
  libpathime and nothing else, so there is nothing else they could do.
- **Under `tests/anthy/` and `tests/pyzy/`**, which drive a backend directly,
  each program names its data itself: `anthy_conf_override("DIC_FILE", …)` and
  `pyzy_set_data_dir(…)`, both with absolute paths the build supplied as
  compile definitions.

The three end-to-end engine tests under `tests/api/` are the only ones there
gated on a `LIBPATHIME_WITH_*` option, because they are the only ones whose data
has to exist.

Each engine test is additionally confined to its own `data_dir` under the build
tree, so a run never touches the developer's real `~/.config/anthy` or pyzy user
database. Both backends learn on commit, so each has a `.clean` fixture test
that wipes that directory before the run — without it a run would be graded
against whatever the previous run taught it.

One rule holds for `tests/api/`: **nothing there may link a backend library.**
These are API-surface tests, so the public header is the whole interface they
are entitled to — and it is also the only linkage that works, since anthy is
static on Windows and a test that linked it would get a second, independent copy
of anthy's process-global state. See `docs/anthy-mapping.md`, "Why the anthy
family is built static on Windows".

## Conditional registrations

Before reading anything into a test's absence:

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
