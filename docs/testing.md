# Testing

Everything under `tests/`, how to run it, and the conventions the suites follow.

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

**Skip protocol.** A test whose backend or data is absent is still registered,
and exits 77 — `SKIP_RETURN_CODE`, which ctest reports as *skipped* rather than
passed or failed. Deciding it in the source rather than in CMake is what keeps
the loss visible: a configuration that drops a backend produces skips in the
ctest output, not a quietly shorter list. The registrations that *are*
CMake-conditional are listed under "Conditional registrations" at the end.

## Running under AddressSanitizer and UBSan

No preset carries the sanitizers; they go on the compiler flags, and the whole
tree — the library, the four adapters, the three vendored libraries and every
suite — is built with them:

```bash
cmake -S . -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DLIBPATHIME_BUILD_TESTS=ON \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
      -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined"
ASAN_OPTIONS=detect_leaks=0 cmake --build build/asan
ctest --test-dir build/asan --output-on-failure
```

**The build needs `detect_leaks=0`; the tests do not.** anthy's dictionary is
generated during the build by host tools compiled from the same sources, and
those tools exit without freeing — a build-time program has no reason to. Under
LeakSanitizer they exit non-zero and the build stops.

A `ctest -j` run whose pyzy user database does not exist yet can log
`database is locked` from the tests that share one: several processes create it
at the same moment. It is contention between test programs rather than anything
a client would see, and it does not recur once the file is there.

One suite is not leak-clean: **`anthy.vendor.main`** is upstream's own test
program, which converts several hundred lines and releases neither the contexts
it creates nor the library. It is vendored code testing vendored code, left as
upstream wrote it. Everything else passes with leak detection on, and nothing
reports a memory error or undefined behaviour.

## Running with uninitialized locals made hostile

Neither sanitizer above catches a *read of an uninitialized local* — that is
MemorySanitizer's job, and MSan is unusable here because it reports false
positives unless every library in the process is instrumented too, glibc
included. The cheap substitute catches the same class:

```bash
cmake -S . -B build/uninit -G Ninja -DLIBPATHIME_BUILD_TESTS=ON \
      -DCMAKE_C_FLAGS="-ftrivial-auto-var-init=pattern -g" \
      -DCMAKE_CXX_FLAGS="-ftrivial-auto-var-init=pattern -g"
cmake --build build/uninit
ctest --test-dir build/uninit --output-on-failure
```

Every uninitialized local is filled with a repeating pattern instead of whatever
the stack happened to hold, so a read of one stops being luck and becomes a
consistent wrong answer — and an uninitialized *pointer* becomes a wild address
that faults on the spot. The whole suite passes under it, and it is worth
re-running after touching anything that fills a struct on the caller's behalf.

**The case it exists for is `pathime_init_params_t`.** `struct_size == sizeof` is
the caller's promise that every member of that layout is filled in, so
`pathime_init()` reads all of them, and `resource_dir` is *dereferenced* to
reject an empty one (`src/init.cc`). A caller that sets `struct_size` and
`data_dir` and leaves the third member alone is passing a wild pointer whenever
the stack beneath it is dirty — which depends on optimization level, on the
environment block, and on what ran before, so it presents as an intermittent
failure rather than a reliable one. Every caller of `pathime_init()` in this tree
therefore zeroes the struct first, tests and demo alike.

## Measuring what the suites reach

`LIBPATHIME_TEST_COVERAGE=ON` instruments the build; `pathime-test-coverage`
clears the counters, runs the suites and writes an annotated report. BUILD.md,
"Test coverage", has the commands, the Windows recipes, and what the option
requires; it is worth reading once for the reason the counters are cleared,
which is not obvious and was hiding a real gap.

Three things about the numbers themselves:

- **Branch coverage sits below line coverage by about ten points**, and that gap
  is where the untested cases mostly are: a line counts as covered the moment it
  runs once, whichever way its conditions went. Read `index.html`, which marks
  partially taken branches, rather than the summary — several suites here exist
  because a file was line-green while half its conditions had never been
  evaluated, `core.punctuation` most clearly.
- **The branch figure excludes exception-unwind edges**, via gcovr's
  `--exclude-throw-branches`. Without that, gcov counts the implicit throw edge
  out of every statement that can allocate — every `std::string`, every
  `push_back` — as an untaken branch. Those are 846 of 4294 here, enough to move
  the reported figure by sixteen points, and reaching one means an allocation
  failed. Counting them would report a decision made and declined as a coverage
  gap, every time.
- **A file's number depends on the build's shape, not only on the suites.**
  `src/module_path.cc` reports the lowest figure in the tree, and most of the
  shortfall is its `/proc/self/exe` fallback, which exists for static builds and
  cannot run in the shared one the report is normally taken from.

This document describes the suites that exist; the report is how you find out
what they miss, and it is meant to be re-run rather than summarised here, where
a copied figure would be stale by the next commit.

## The four kinds of test

### `tests/api/` — `api.<name>`

Links the built library and touches only exported symbols. Plain C11 against
`<pathime/pathime.h>`, which doubles as proof that the header works from strict
C and that every symbol a client needs is actually exported. Holds the ABI,
lifecycle and options suites, plus one end-to-end test per engine —
`api.engine_hangul`, `api.engine_anthy`, `api.engine_pyzy` and
`api.engine_table` — each typing real Korean, Japanese or Chinese through the
public API. `api.engine_pyzy_nodb` is the negative half of the pyzy one: it
gives the engine a resource directory that will never hold a database and
asserts it reports itself unavailable instead of pretending to work.

`api.engine_table` carries a job the other three do not. For hangul, anthy and
pyzy the vendored library is the authority on what correct output looks like and
the test records what it does; the table engine has no such authority, because
libpathime wrote it. So that test *is* where the behaviour
`docs/ibus-table-mapping.md` specifies is pinned down — §8, the lookup and the
candidate ordering, and §11's negotiated options. It runs against the real
tables the build compiles out of `ibus-table-chinese` rather than a fixture,
because the format is an interoperability contract and a table this repository
invented would prove much less about it.

`api.multicontext` is the one test that is not about a single engine. Every
other program here creates contexts of one backend, so none of them can show
that a client with a Korean field and a Japanese field open at once gets the
same answer in each as it would with only one open — the claim
`docs/CONCEPTS.md` makes when it says an engine may serve many contexts and
requires only that calls be serialized. It is differential rather than
expectation-based: each script is typed alone, then every context types its
script again round-robin, and any difference is a leak. That is what lets it
cover every engine at once without restating what the four engine tests pin.
It prints the engines it interleaved, because the set depends on the
configuration and a bare "passed" would not distinguish two from five, and it
skips when fewer than two engines are usable. The four engine tests each carry
an interleaving test of their own for what is specific to that backend — two
romaji front ends holding a pending consonant at the same time, two contexts
over one shared table database, an engine-level option reaching one context
and not its neighbour.

**Nothing here may link a backend library.** The public header is the whole
interface these tests are entitled to; reaching past it would cost them the
one thing they exist to show, which is that the header is complete and that
everything a client touches is exported. `tests/anthy/` and `tests/pyzy/` are
where a backend gets driven directly.

### `tests/core/` — `core.<name>`

Compiles the internal sources under test directly into each executable, because
internal helpers carry no `PATHIME_API` and a shared build does not export them.
C++17, covering `utf8.*`, `composition.*`, `keys.*`, `punctuation.*`,
`engines/anthy/romaji.*`,
backend.h's inherited defaults, the options machinery, and
the table engine's data layer at seams the public API cannot reach.

`core.keys` is the clearest case of that reach. `key_event_from_public()`'s two
rejections are statuses a client provokes by getting the struct wrong, which the
API tests have no reason to do, and the keysym decoding is otherwise observable
only through an engine that has already folded the answer into a composition.
Called directly, each rule of the X11 mapping the public header commits to — the
Latin-1 identity below U+0100, the `0x01000000 | scalar` form above it, and the
surrogate and out-of-range rejections inside that form — is one assertion.

`core.table` and `core.table_compile` split the table engine's data layer along
one line: `core.table` opens nothing, and `core.table_compile` writes databases
and reads them back. Keeping them apart is what lets `core.table` stay a
structural assertion about a parser that knows nothing of SQLite, while the
compiler — which is nothing but SQLite — is still covered. The round trip is the
point of the second: the compiled format is an interoperability contract, so a
matched pair of bugs on both sides of our own writer and reader would be
invisible to a test that went only one way.

`core.table_compile` also carries the paths a client can name through
`PATHIME_OPT_TABLE_FILE` but is unlikely to think about — a path that holds
`#`, `%` or an apostrophe, each of which is syntax to one of the two
layers of quoting `TableDatabase::open()` builds its `ATTACH` out of.

`core.table` is also a structural assertion, not only a functional one. Its
source list is six files from `src/engines/table/` plus `paths.cc` and `utf8.cc` —
no core, no adapter — because everything below `table_backend.cc` is meant to be
usable without the engine. A link error there would be the first sign that
something in the parser, the database or the ranking had reached back across the
seam.

The mirror-image assertion sits in `core.options`, which compiles the *library's*
table sources: `coverage.cc` is deliberately absent from that list, because the
library does not link it. Glyph filtering runs once in `tools/table-compile`, and
if `coverage.cc` ever became reachable from the library the two lists would stop
agreeing and one of them would fail to link.

`core.table` compiles `coverage.cc` against whichever map
`LIBPATHIME_TABLE_COVERAGE` selected, so what it asserts is the map the build
actually shipped. Its assertions are chosen to hold for either — ASCII, common
Han, and one Extension B code point neither map reaches — so configuring a
different map is not a test edit. Nothing asserts a range count or a total, which
would turn every font refresh into one.

### `tests/<backend>/` — `<backend>.vendor*`

The submodule's own suite. None of the three upstream build systems is used by
this build, so the wiring is reproduced rather than reused, and the vendored
sources are compiled where they sit.

### `tests/<backend>/` — `<backend>.<name>`

Tests written for libpathime against a backend directly. These exist to make the
Windows build prove it behaves like the Linux one, so all of them must build and
pass on both.

## Runtime data

Every test written for libpathime finds its data the way a client does, and
none of their registrations sets an environment variable or a working directory
to arrange it.

- **Under `tests/api/`**, the engines read the `pathime-data/` that
  `src/CMakeLists.txt` stages beside the built library, which is where
  `pathime_init_params_t::resource_dir` looks by default. Those tests link
  libpathime and nothing else, so there is nothing else they could do.
- **Under `tests/anthy/` and `tests/pyzy/`**, which drive a backend directly,
  each program written here names its data itself:
  `anthy_conf_override("DIC_FILE", …)` and `pyzy_set_data_dir(…)`, both with
  absolute paths the build supplied as compile definitions.

The `anthy.vendor.*` programs are the exception, because upstream hard-codes
build-tree-relative paths into its conf overrides. Their registrations are the
one place in `tests/` that set a working directory — and, for the three that
open a dictionary, blank `XDG_CONFIG_HOME`, which anthy would otherwise prefer
to the per-test `HOME` compiled in, so a desktop run cannot write into the real
`~/.config/anthy`. The header comment in `tests/anthy/CMakeLists.txt` walks
through it.

The four end-to-end engine tests are where the data actually has to exist, and
they are registered all the same: each compiles to a skip when its
`PATHIME_WITH_*` macro is 0. What the `if(LIBPATHIME_WITH_*)` arms in
`tests/api/CMakeLists.txt` hold is the wiring around them — the `data_dir`
compile definitions and the `.clean` fixtures — plus `api.engine_pyzy_nodb`,
which has nothing to assert without the pyzy adapter present.

The anthy, pyzy and table tests are each confined to a `data_dir` of their own
under the build tree, so a run never touches the developer's real
`~/.config/anthy` or pyzy user database. `api.engine_hangul` needs no such
confinement: libhangul compiles its tables into the library and writes nothing.
The other three all learn on commit — two of the shipped tables declare
`DYNAMIC_ADJUST` — so `api.engine_anthy.clean`,
`api.engine_pyzy.clean` and `api.engine_table.clean` wipe that directory before
the run. Without them a run would be graded against whatever the previous run
taught it.

`api.multicontext` has a `data_dir` of its own for the same reason but no
`.clean` fixture, because it never commits: its two phases have to produce
identical results, which is only true of an engine that has not learned
anything between them.

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
- **`pyzy.full_pinyin`, `pyzy.double_pinyin` and `pyzy.bopomofo`** need
  `android.db`, which the pyzy port builds only when configure finds a Python 3
  interpreter. Without it they are not registered — three tests failing for a
  reason unrelated to anything they check would be worse — and the configure
  output warns about the loss. `api.engine_pyzy` stays registered either way
  and compiles to a skip instead.
- **`core.romaji`, `core.table` and `core.table_compile`** compile backend
  sources directly, so each is registered only when the option that builds
  those sources is on — `LIBPATHIME_WITH_ANTHY` for the first,
  `LIBPATHIME_WITH_TABLE` for the other two.
- **`api.multicontext`** is registered unconditionally and decides at runtime.
  It asks `pathime_has_engine()` about each engine, drops the ones that answer
  no — and the table engine if no table opens — and skips if fewer than two
  survive, which is the one configuration where it has nothing to interleave.

## What is deliberately not tested

`pyzy`'s own `src/tests/basic.cc` is not wired up: its `DummyObserver` declares
the `InputContext::Observer` callbacks with the wrong constness, so nothing
overrides, the class stays abstract, and the file does not compile. What it was
written to check is covered by the tests in `tests/pyzy/`,
which take their expected values from it.

One upstream bug has no test because it cannot have one:
`anthy_dic_util_set_personality()` aborts the process on both Linux and
Windows — `anthy_dic_set_personality()` replaces the record and personal-dic
cache that `anthy_dic_util_init()` installed without releasing them, and the
orphan is freed twice at teardown. See `tests/anthy/test_dicutil.c`.
