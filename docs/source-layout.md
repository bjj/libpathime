# Source layout — `src/` and `tests/api/`

The map of the implementation tree: what each file is for, the conventions
that hold across it, and which choices are settled versus deliberately still
open. This is the starting point for implementation work; `TODO.md` §2 holds
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
                        two questions every other file asks — initialized(),
                        data_dir()
  engine.h/.cc          pathime_engine_*: registry, handles, requirements. The
                        header defines struct pathime_engine
  context.h/.cc         pathime_context_*: lifecycle, process_key entry, focus /
                        reset / surrounding text, callback dispatch ordering. The
                        header defines struct pathime_context
  composition.h/.cc     structured composition model + projection to the flat value
  candidates.cc         pathime_context_candidate/_select_candidate; eager
                        materialization to the cap; currently-shown cursor
  options.h/.cc         descriptor table, two-level store, kind-typed accessors,
                        introspection walk — and all 19 public option entry
                        points, both levels
  keys.h/.cc            engine-agnostic key layer: validation, routing,
                        handled/unhandled
  utf8.h/.cc            encoding boundaries; copy-on-return helpers
  backend.h             the internal engine interface — the load-bearing seam
  engines/
    hangul/             hangul_backend.h/.cc
    anthy/              anthy_backend.h/.cc, romaji.h/.cc
    pyzy/               pyzy_backend.h/.cc, observer.h/.cc
    table/              README.md only, until the engine of docs/ibus-table-spec.md
                        is written
tests/
  api/                  the API-surface suite: abi, lifecycle, options
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
(`candidates.cc`) is the standing example.

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
| `pathime_context_create/destroy/engine/user_data`, `_process_key`, `_composition`, `_set_surrounding_text`, `_set_focused`, `_reset` | `context.cc` |
| `pathime_context_candidate`, `pathime_context_select_candidate` | `candidates.cc` |
| all 19 `pathime_*option*` functions, both levels | `options.cc` |

The two seams: **candidate access lives with the candidate subsystem**, not with
the rest of `pathime_context_*`, because both functions are thin faces over the
materialized list and the cursor that `candidates.cc` owns outright — though
`select_candidate` still ends in `context.cc`'s post-mutation assembly like
every other mutating call. And **the option entry points live in `options.cc`
rather than being split between `engine.cc` and `context.cc`**: they are
kind-typed pairs that differ only in which store they consult first, and
splitting them would put the two halves of one resolution rule in two files.

## Who owns which finding

The findings are `TODO.md` §2's; the layout gives each exactly one home.

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
- Vendor **include** wiring for anthy and pyzy is deliberately not replicated
  in `src/CMakeLists.txt` yet. When an adapter first includes a vendor
  header, lift the solved pattern from that backend's
  `tests/<backend>/CMakeLists.txt` (port config dirs, generated forwarding
  headers, glib).

## `tests/api/`

A peer of `tests/{hangul,anthy,pyzy}` under the same rules — plain C11, no
external framework, must build and pass identically on Linux and Windows,
ctest names `api.<name>`. Being C is part of the point: these are the only
programs that consume the header the way a client does, so they double as
proof it works from strict C and that the symbols are exported.

- `abi_test.c` — live now: version macro/function lockstep, status-string
  totality, explicit enum values, `pathime_has_engine`'s pre-init falsity.
- `lifecycle_test.c`, `options_test.c` — registered but exiting 77, which
  ctest reports as *skipped* (`SKIP_RETURN_CODE`), until the functions they
  exercise exist. Each file's header comment is its planned coverage. Replace
  the skip with real checks as implementation lands; never delete a
  registration.

## Deliberately undecided

- **The shape of `backend.h`** and **the types in `composition.h`.** They are
  one decision — `TODO.md` §3, question 1 — and should be designed against
  all three mapping docs at once, not accreted engine by engine. Until then
  both headers carry constraints, not signatures.

## Decided here, cheap to revisit

- **The romaji/kana front end lives with anthy, not in core** — the
  per-engine answer to `TODO.md` §3, question 2. Only Japanese needs a
  composing state machine before its backend sees input; hangul and pyzy
  dispatch is simple enough to live in their adapters. If the table engine
  turns out to want a shared front end, hoisting `romaji.*` into `src/` is
  cheap.
