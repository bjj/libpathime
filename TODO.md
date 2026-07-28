# libpathime — unfinished business

Start here in a fresh session. This file holds only what is *not* done: the
work queued, the questions genuinely open, and the deferrals a reader might
mistake for gaps. Everything settled lives elsewhere, and the split is
deliberate:

- `docs/CONCEPTS.md` and `include/pathime/pathime.h` — the model and the
  contract, kept in lockstep. The header carries no list of deviations from
  the concepts because there are none.
- `docs/adapter-findings.md` — the six numbered constraints that shape the
  adapter layer, cited by number from `src/` and `docs/*-options.md`.
- `docs/design-history.md` — the settled design rounds, question by question,
  with the evidence each was answered against and what the answer cost. Its
  section numbers (§1, §3, §4a–§4c, §5) are the ones code comments cite.
  **Read it before reopening anything that looks undecided** — most things
  that look open were closed there, on purpose, with reasons.

Status in one paragraph: the build (Linux and Windows), the core (all 44
public entry points), the three adapters (hangul, anthy, pyzy) with their
composing front ends, options and negotiation, the terminal demo client, the
preedit rule, and the eager candidate strip (`PATHIME_OPT_PREDICTION`,
default on) are built and tested — 31 suites, all passing
(`docs/testing.md`). The detailed ledger is at the top of
`docs/design-history.md`.

---

## Next: the table engine

The one large unstarted piece.

`ibus-table` cannot be a backend. It is Python, so there is nothing to link
against; what it offers is a proven feature set, not a library. It is the
reference we trust for the table-driven methods we want (Cangjie, Wubi, and the
rest), and `refs/ibus-table-chinese` supplies real tables to test against.

The decision: **libpathime implements its own table engine**, a peer of the
vendored submodules rather than a wrapper around one. `docs/ibus-table-spec.md`
is the specification — source `.txt` format, compiled SQLite schema, key-event
state machine, candidate sorting — and `docs/ibus-table-options.md` is its
option inventory. The API already treats it as a real fourth engine:

- `PATHIME_ENGINE_TABLE` is in the header. One id covers every table-driven
  method, because they differ only in the table loaded. Table selection is
  `PATHIME_OPT_TABLE_FILE`, a per-context option rather than a separate enum
  entry — because the engine is ours, one engine can hold several compiled
  tables and hand a different one to each context.
- `PATHIME_WITH_TABLE` is in the generated `config.h`, currently always 0, and
  `pathime_has_engine(PATHIME_ENGINE_TABLE)` will be false until there is code.
- `LIBPATHIME_WITH_TABLE` defaults OFF. Turning it on is gated in
  `cmake/LibpathimeDependencies.cmake` with a warning naming the missing piece
  as the implementation itself, so nobody mistakes it for a missing dependency.
- Several options already claim table membership in `src/options.cc` — among
  them `PATHIME_OPT_PREDICTION`, whose table meaning is suggestion mode
  (spec §11.3): post-commit continuations, not the as-you-type candidates,
  which are structural to table input.

## Queued work

- **The header self-explanation pass.** Much of the commentary in
  `include/pathime/pathime.h` justifies decisions by naming backend behaviour
  (anthy's write-once personality, pyzy's creation-time `InputType`,
  libhangul's nine built-in layouts). That was right while the implementation
  did not exist; now that it does, those notes belong in `docs/*-mapping.md`
  and `docs/design-history.md`, and the header should keep only the contract.
  The two tests for whether a passage stays: does a client's behaviour depend
  on it, and would removing it let someone reopen a question the design
  closed. Best done after the table engine lands, so the pass is made once.

## Open questions

- **The composition model has no cursor inside a span, and two adapters have
  already paid for it.** Both anthy and pyzy decline Left/Right/Home/End while
  composing, because there is no position for them to move and nothing the
  client could be told about the result. For pyzy that decision was forced
  rather than chosen: routing the keys through made pyzy render its own input
  cursor as a literal `'|'` inside `conversionText()`
  (`PinyinContext.cc:129-142`), so typing "nihao" then Left made the preedit
  read `ni h|a` — a display marker inside a string the API promises is plain
  content text. Declining is right for the phone-keyboard target, which has no
  such key. It is wrong for a desktop client wanting to repair the middle of a
  long pinyin run without backspacing to it. Revisit only with a real consumer.
- **Thread-safety is a documented requirement, not an enforced one.** Nothing
  detects overlapping calls. If that proves to be a common client bug, a debug
  build could catch it cheaply with a non-recursive in-call flag per context.

## Deferred, deliberately

- **Input purpose and hints** (text/name/email/URL/password, single- vs.
  multiline, assistance toggles) — deferred past v1, 2026-07-27. No backend
  consumes them, and the extension is additive when a consumer appears (most
  plausibly the table engine: a URL field wanting Latin passthrough). Not a
  gap; `docs/design-history.md` §1 has the reasoning.
- **The smaller per-engine deferrals** — anthy auxiliary dictionaries, romaji
  table variants, hangul jamo output, user-defined phrases, candidate
  annotations, per-composition character-type conversion (ibus-anthy's F6–F10
  family) — are each recorded with reasons in `docs/design-history.md` §1.
  Every one is additive if a consumer appears.
