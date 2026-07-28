# Source layout — `src/`, `tests/api/` and `tests/core/`

The map of the implementation tree: what each file is for, the conventions
that hold across it, and which choices are settled versus deliberately still
open. This is the starting point for implementation work; `docs/adapter-findings.md` holds
the numbered findings this layout answers to, and `docs/CONCEPTS.md` plus
`include/pathime/pathime.h` hold the contract being implemented.

## The shape

```
src/
  CMakeLists.txt        the `pathime` library target; defines PATHIME_BUILD, carries
                        the public include dirs (the old libpathime::headers
                        interface target is folded in and retired)
  init.h/.cc            process-global layer: pathime_init/shutdown, init params,
                        version + status introspection; the header answers the
                        questions every other file asks — initialized(),
                        data_dir(), resource_dir()
  engine.h/.cc          pathime_engine_*: registry, handles, requirements. The
                        header defines struct pathime_engine
  context.h/.cc         pathime_context_*: lifecycle, process_key entry, focus /
                        reset / surrounding text, callback dispatch ordering. The
                        header defines struct pathime_context
  composition.h/.cc     structured composition model + projection to the flat value
  candidates.h/.cc      pathime_context_candidate/_set_candidate_cursor/
                        _select_candidate; eager materialization to the cap;
                        the hovered-candidate cursor, which context.cc
                        publishes into the composition rather than a getter
  options.h/.cc         descriptor table, value-name table, two-level store,
                        kind-typed accessors, introspection walk — and all 20
                        public option entry points, both levels
  keys.h/.cc            engine-agnostic key layer: validation, routing,
                        handled/unhandled
  utf8.h/.cc            encoding boundaries; copy-on-return helpers
  module_path.h/.cc     which file this library is, so that the resource
                        directory can default to one beside it
  paths.h/.cc           path_join and is_regular_file, for the adapters that
                        reach into the resource directory
  win32_utf.h           UTF-8 <-> UTF-16 for the Windows entry points that
                        name a file; Windows-only, header-only
  punctuation.h/.cc     width and Chinese punctuation, shared by both Chinese
                        engines — see the note below on why it is not per adapter
  backend.h             the internal engine interface — the load-bearing seam
  engines/
    hangul/             hangul_backend.h/.cc
    anthy/              anthy_backend.h/.cc, romaji.h/.cc — the composing front end
    pyzy/               pyzy_backend.h/.cc, observer.h/.cc — the dirty-flag Observer
    table/              table_backend.h/.cc — the only file here that includes
                        backend.h; table_source.*, table_db.*,
                        table_properties.*, ranking.*, variants.* — the data
                        layer below it, which names no engine type at all;
                        variants_data.h is generated (tools/generate-variants.py).
                        coverage.*/coverage_data.h are a further step out: the
                        *library* does not link them, only tools/table-compile,
                        because glyph filtering happens once at build time and
                        the map would otherwise be dead weight in every process
tools/
  table-compile/        pathime-table-compile: source .txt to compiled .db, built
                        from the engine's own data-layer sources
  generate-variants.py  regenerates engines/table/variants_data.h from Unicode
tests/
  api/                  links the built library, exported symbols only, C11:
                        abi, lifecycle, options, and one test per engine
  core/                 compiles internal sources directly, C++17:
                        utf8, composition, options, table
```

## Two structural decisions, and why

**API and core are fused.** There is no `api/` directory: the public entry
points are thin validators over the internal objects, and a separate layer
would be a handful of small files reaching across a boundary that isolates
nothing. Core files sit directly in `src/`, roughly one per section of the
public header.

**`backend.h` is the seam.** Core code (`src/*.cc`) never names a vendor
type; engine code (`src/engines/*`) never touches the public API surface;
everything an adapter provides crosses through `backend.h`. There is
deliberately no `engines/common/`: if something is shared between engines, it
is a core obligation and lives in `src/` — the eager candidate pump
(`candidates.cc`) is the standing example. That rule says shared code moves
*up*, not that adapters duplicate; `engines/table/variants.*` stays put because
nothing shares it — pyzy collapses the same option onto a single bool and lets
its own data convert, so there is no second implementation to unify with.

**The table engine draws a second boundary inside its own directory.**
`table_backend.cc` is the only file under `engines/table/` that includes
`backend.h`; everything else there answers questions about table data and
names no `Composition`, `KeyEvent`, `Output` or `OptionReader`. That is what
lets `tools/table-compile` build the parser and the database writer without any
of the engine, and what lets `tests/core/table_test.cc` reach the edge cases
directly. It is a header boundary, deliberately not a library one: a standalone
table library would have needed its own key-event and composition types,
translated by an adapter that did nothing else — a second IME API invented so
the first could wrap it.

**The two handle types are defined in headers, not in their `.cc` files.**
`struct pathime_engine` is in `engine.h` and `struct pathime_context` is in
`context.h` because in each case more than one core file has to reach inside:
`options.cc` reads and writes both levels of the option store, `candidates.cc`
reads the context's materialized list, and `context.cc` registers itself in the
engine's context list for the engine-level broadcast. Both complete the
incomplete types the public header's typedefs declare, so they sit in the
global namespace while everything around them is in `namespace pathime`.

## Which file owns which entry point

Roughly one file per section of the public header, with two seams worth stating
because they are not obvious from the names:

| Entry points | Home |
|---|---|
| `pathime_version*`, `pathime_status_string`, `pathime_init`, `pathime_shutdown` | `init.cc` |
| `pathime_has_engine`, `pathime_engine_create/destroy/id/requirements` | `engine.cc` |
| `pathime_context_create/destroy/engine/user_data/requirements/is_focused`, `_process_key`, `_composition`, `_set_surrounding_text`, `_set_focused`, `_reset` | `context.cc` |
| `pathime_context_candidate`, `_set_candidate_cursor`, `_select_candidate` | `candidates.cc` |
| all 20 `pathime_*option*` functions, both levels | `options.cc` |

Tier 3 — the value a table declares — is the one resolution input that does not
live in `options.cc`, because it lives in a data file only a backend can read.
`resolve_number()` and `resolve_string()` reach it through
`EngineBackend::declared_number/declared_text`, between the engine store and the
descriptor default. It is deliberately not returned as an `OptionValue *` like
the two stores: those point at storage the caller owns, and a table's
declaration does not.

The two seams: **candidate access lives with the candidate subsystem**, not with
the rest of `pathime_context_*`, because both functions are thin faces over the
materialized list and the cursor that `candidates.cc` owns outright — though
`select_candidate` still ends in `context.cc`'s post-mutation assembly like
every other mutating call. And **the option entry points live in `options.cc`
rather than being split between `engine.cc` and `context.cc`**: they are
kind-typed pairs that differ only in which store they consult first, and
splitting them would put the two halves of one resolution rule in two files.

## Who owns which finding

The findings are `docs/adapter-findings.md`'s; the layout gives each exactly one home.

| Concern | Home |
|---|---|
| Structured model richer than the flat value (Finding 1) | `composition.*` for the model and projection; each adapter supplies its own structure |
| Currently-shown candidate is ours (Finding 2) | `candidates.cc` |
| Two-layer lifetime (Finding 3) | `init.cc` / `engine.cc` / `context.cc` — one file per layer |
| Encoding at every boundary (Finding 4) | `utf8.*`, invoked inside each adapter |
| Push vs pull reconciliation (Finding 5) | `engines/pyzy/observer.*` sets dirty flags; `context.cc`'s post-call assembly reads them |
| The whole key-event layer is ours (Finding 6) | `keys.*` for the engine-agnostic part; `engines/anthy/romaji.*` for the composing front end |
| Eager materialization before callbacks | `candidates.cc`, sequenced by `context.cc` |

## Conventions

- Internal code is **C++17** in `namespace pathime`; the standard is requested
  on the `pathime` target only, because the build-wide default stays C++11
  (pyzy's floor). Public functions get C linkage from the header's
  `extern "C"` block; definitions in `.cc` files inherit it.
- Files are `.cc`/`.h`. Internal header guards are `LIBPATHIME_SRC_<PATH>_H`.
  Engine files carry the `<backend>_backend` basename so a search result says
  which side of the seam it is on.
- Internal includes are rooted at `src/` (`#include "backend.h"`,
  `#include "engines/pyzy/observer.h"`) — `src/` is a private include
  directory of the target, so no `../` chains.
- Engine sources and their vendor link libraries are gated in
  `src/CMakeLists.txt` on the same `LIBPATHIME_WITH_*` options as everything
  else. Core files compile regardless of which engines exist and branch on
  `<pathime/config.h>` where they must.
- Vendor **include** wiring lives in `src/CMakeLists.txt`, lifted from the
  pattern each backend's test directory had already solved. anthy needs only
  the submodule root, since `<anthy/anthy.h>` is self-contained. pyzy needs its
  three public headers staged into an install-shaped directory and included as
  `<PyZy/InputContext.h>`, because its `src/String.h` shadows the C library's
  `<string.h>` on a case-insensitive filesystem — pyzy's source directory must
  never appear in an `-I`.

## `tests/api/` and `tests/core/`

Two suites, and the difference between them is what each links.

**`tests/api/`** links the built library and touches only exported symbols.
Plain C11, no external framework, identical on Linux and Windows, ctest names
`api.<name>`. Being C is part of the point: these are the only programs that
consume the header the way a client does, so they double as proof it works from
strict C and that the symbols are exported.

- `abi_test.c` — version macro/function lockstep, status-string totality,
  explicit enum values, `pathime_has_engine`'s pre-init falsity.
- `lifecycle_test.c` — init/shutdown pairing and its rejections, params
  validation, engine and context lifecycle, the focus rules, NULL handling
  across every entry point in three initialization states.
- `options_test.c` — the introspection walk, name totality and distinctness,
  the pre-init-safe subset.
- `engine_hangul_test.c`, `engine_anthy_test.c`, `engine_pyzy_test.c` — one
  end-to-end test per engine, typing real Korean, Japanese and Chinese through
  the public API. These are the only tests here gated on a `LIBPATHIME_WITH_*`
  option, because they need their backend's *runtime data* rather than just the
  library; `tests/api/CMakeLists.txt` carries that wiring and says why it is a
  test-environment problem rather than a library one.

**`tests/core/`** compiles the internal sources under test directly into each
executable, because internal helpers carry no `PATHIME_API` and a shared build
does not export them — and decorating them purely to make them testable would
widen the ABI for no client's benefit. C++17, ctest names `core.<name>`.

- `utf8_test.cc` — the encoding boundary, with the rejection cases (overlongs,
  surrogates, CESU-8 pairs, truncation, embedded NUL) tested at least as hard
  as the acceptance ones.
- `composition_test.cc` — the model and its projection, especially
  `preedit_settled` as a scalar count rather than a byte count.
- `options_test.cc` — the descriptor table against the header option by
  option, tier resolution, the `struct_size` protocol, the Hangul capping rule,
  and the engine-level broadcast. It builds `pathime_engine` and
  `pathime_context` aggregates directly, which is how it reaches machinery the
  public API alone cannot.

Compiling the sources in twice is the deliberate trade: a little build time for
tests that reach the seams where the rules actually live, without any of it
becoming public surface.

## The seam, as designed

`backend.h` and `composition.h` were one decision — `docs/design-history.md` §3, question 1 —
and were taken against all three mapping docs at once rather than accreted
engine by engine. The answer turned out to be smaller than expected, because
laid side by side the three backends describe the same three-part picture:

|          | settled              | active             | tail              |
|----------|----------------------|--------------------|-------------------|
| pyzy     | `selectedText()`     | `conversionText()` | `restText()`      |
| anthy    | segments < active    | segment[active]    | segments > active |
| hangul   | finished syllables   | trailing syllable  | (always empty)    |

So `Composition` is three strings, a candidate list for the active span, and a
cursor into it; the projection to the flat public value is a concatenation and
a scalar count, and `preedit_settled` falls out as the length of `settled`.
Greedy left-to-right resolution is then one function, `settle_active()`.

There is deliberately no segment array and no active index. anthy has both; it
keeps them privately and reports the three strings. That is the
phone-keyboard target breaking the tie, and it is why nothing in the model can
address a span other than the active one.

`backend.h` follows from that: two layers matching the API's own
(`EngineBackend`, `ContextBackend`), a `KeyEvent` the key layer has already
validated, an `OptionReader` an adapter pulls resolved values through, a
`SurroundingTextView` it asks before revising text it already committed, and an
`Output` carrying the commit and deletion requests one call produced. An
adapter mutates the model in place and never dispatches, never orders, never
learns that options have tiers.

`SurroundingTextView` is the newest of those and the narrowest: one method,
`can_delete_before(count)`. It exists because `PATHIME_HANGUL_PREEDIT_NONE`
builds its syllable inside the client's document, and the header's recovery
when the snapshot no longer covers that text is not something the dispatch can
perform on the adapter's behalf — "treat what is already there as final and
continue as if starting fresh" means *this* key must begin a new syllable, a
decision that has to be taken before libhangul folds the key into the old one.
So the adapter asks first. It is deliberately not a view of the document text:
reading preceding context is a Hanja feature and Hanja is out of scope, so
exposing the text would be a concept carried for no consumer. Its predicate and
`refresh_composition()`'s dispatch condition are one function,
`range_within_snapshot()` in `context.cc`, so the answer an adapter gets before
the fact and the check the library applies after it cannot disagree.

## Decided here, cheap to revisit

- **The romaji/kana front end lives with anthy, not in core** — the
  per-engine answer to `docs/design-history.md` §3, question 2. Only Japanese needs a
  composing state machine before its backend sees input; hangul and pyzy
  dispatch is simple enough to live in their adapters. Answered for the table
  engine now that it exists: it wants nothing from `romaji.*`. Its key run is
  the raw scalars the user typed, matched against a set the table declares, so
  there is no state machine in front of it at all — the nearest thing is
  char-prompt substitution, which is display-only and applies *after* the run is
  built.

- **The width and punctuation tables are per-language, so Chinese has one and
  Japanese has its own** — `punctuation.*` for Chinese, and the same job done
  inline by `engines/anthy/romaji.cc`'s `kSymbolTable` for Japanese.
  `PATHIME_OPT_LATIN_WIDTH` and `PATHIME_OPT_PUNCTUATION_WIDTH` are common
  options, but nothing about their *content* is: the comma key is 、in
  Japanese and ，in Chinese, and Chinese needs two tables of its own for the
  simplified and traditional variants. Only the half-to-full-width arithmetic
  is genuinely shared, and it is three lines.

  `punctuation.*` sits directly in `src/` rather than under an adapter because
  **both** Chinese engines use it. It began as pyzy's; the table engine then
  needed the same behaviour, and ibus-table's own answer (spec §11.4) disagrees
  with ibus-pinyin's on four characters. Following each reference per engine
  would make one option mean two things depending on which Chinese engine the
  client picked, so the table engine uses this and the parity cost is recorded
  at the top of `punctuation.h`. There is still no `engines/common/`: shared
  code does not get a subdirectory, it goes in `src/` with the rest of the core.
