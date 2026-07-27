# libpathime — unfinished business

Start here in a fresh session. This file is the successor to the old `PLAN.md`,
which described the step that produced `include/pathime/pathime.h`; that step is
done and the plan has been deleted. What follows is what is *not* done, plus the
constraints that will shape it.

Read `docs/CONCEPTS.md` for the model and `include/pathime/pathime.h` for the
contract. They are kept in lockstep — the header carries no list of deviations
because there are none.

---

## Where things stand

Done:

- **The build.** CMake over the three vendored submodules, verified on Linux and
  Windows (`BUILD.md`). `include/pathime/config.h` is generated from
  `cmake/pathime-config.h.in`.
- **The model and the contract.** `docs/CONCEPTS.md` and
  `include/pathime/pathime.h`, both settled and kept in lockstep.
- **The core.** All 40 public entry points, implemented rather than stubbed:
  process-global lifetime and `data_dir` resolution, the engine registry, both
  handle lifecycles, focus, the surrounding-text snapshot, the key layer
  (`keys.*`), the encoding boundary (`utf8.*`), the structured composition
  model and its projection (`composition.*`) — including `Output`, which is
  where a mutation's pending commit text and deletion range live until
  `refresh_composition()` dispatches them in the header's fixed order — the
  eager candidate pump (`candidates.cc`), and the options machinery: a 32-row
  descriptor table, four-tier resolution, the two-level store, and the
  `struct_size` protocol.
  `docs/source-layout.md` maps which file owns what.
- **The seam.** `src/backend.h` and `src/composition.h` — §3's two deferred
  questions, answered together against all three mapping docs.
- **The first vertical slice, across all three backends.** Key in, composition
  and commit out: real Korean, Japanese and Chinese, driven through the public
  C API and verified end to end. `src/engines/{hangul,anthy,pyzy}` implement
  `backend.h`, and `src/engines/anthy/romaji.*` is the composing front end anthy
  needs. What the slice left undone is §4a — start there.
- **Tests: 31, all passing.** `tests/core/` compiles internal sources directly,
  because internal helpers carry no `PATHIME_API` and a shared build would not
  export them. `tests/api/` holds the ABI, lifecycle and options suites plus one
  end-to-end test per engine; those three need their backend's *data*, which is
  why they are the first thing there that is gated —
  `tests/api/CMakeLists.txt` explains why that is a test-environment problem
  rather than a library one.

Not started:

- The table-driven engine. `PATHIME_ENGINE_TABLE` exists in the header and
  `LIBPATHIME_WITH_TABLE` exists in the build, but the option defaults OFF and
  is forced off with an explanatory warning if requested, because there is no
  code behind it. See §4.

---

## 1. Design rounds not yet held

The header covers the core loop and options. One area has no API surface at
all, and is now **deliberately out of v1** rather than pending:

- **Input purpose and hints** — ordinary text / name / email / URL / number /
  telephone / password, single-line vs. multiline, and the assistance toggles
  (spelling, prediction, completion, auto-capitalization).

  **Deferred past v1** (decided 2026-07-27). Nothing in the three backends
  consumes a purpose or a hint, and this project's standing habit is to remove
  concepts no backend justifies rather than carry them "just in case". The
  extension is additive when a consumer appears — most likely the table engine,
  which is the one with a plausible opinion (a URL field wanting Latin
  passthrough). Until then this is not unfinished work, and a reader should not
  treat it as a gap.

**Engine options and negotiation are done** — the Options section of
`include/pathime/pathime.h`. 32 options, 8 of them common to several engines and
24 engine-specific, set through three kind-typed setters at two levels, with a
descriptor query so a client can present options it does not know by name, and
`pathime_option_count()` so it can walk options its own header never named.
`pathime_context_set_max_candidates()` is gone, folded in as
`PATHIME_OPT_MAX_CANDIDATES`. `pathime_init()` gained a params struct carrying
`data_dir`, which is the whole of the library's persistent-storage surface.

Roughly three quarters of the catalogued options in `docs/*-options.md` were
cut, in four groups: internal plumbing (encodings, backend init parameters);
presentation, which `docs/CONCEPTS.md` already excludes (key bindings, page
size, orientation, labels, sounds); things earlier rounds settled (thumb-shift,
segment modes, focus-out policy, and direct/Latin passthrough, which is engine
activation state); and options that are dead in their own reference
implementation. The surviving inventory is in the header, not here.

### Deferred out of the options round, with reasons

- **Anthy dictionary selection.** The auxiliary dictionaries (zipcode, symbol,
  old characters, era, emoji) cannot be enabled per context through anthy's
  public API. ibus-anthy switches them by switching "personality", which is the
  process-global write-once trap `data_dir` was chosen to avoid, and it reaches
  past the public header to do it. Left out rather than half-delivered; revisit
  if a supportable mechanism appears.
- **Romaji and kana table variants.** ibus-anthy's schema ships exactly one
  romaji table; the MS-IME, ATOK, Gairaigo, ANSI/BSI and historical-kana
  variants named in its comments were never written. There is nothing to
  reconcile yet, so `PATHIME_OPT_ANTHY_TYPING_METHOD` chooses the state machine
  and not the table. Worth adding once real alternative tables exist.
- **Hangul jamo output.** libhangul can emit decomposed jamo instead of
  precomposed syllables. Nothing in the composition model wants it and the
  guard field that would force it is vestigial in the shipped library.
- **User-defined phrases.** Tables can declare that users may define their own
  phrases, but the API has no operation for defining one, so the flag would
  configure something unreachable. It belongs with a future editing surface.
- **Candidate annotations.** Per-candidate readings, glosses, and table author
  flags. `pathime_context_candidate()` returns text only. Nothing shipping in
  v1 has an annotation to carry, and the extension is additive — a
  `pathime_context_candidate_info()` returning a `struct_size`-versioned struct
  — so it waits for a real consumer. Recorded in the header so the omission
  reads as chosen.

### Cut in the API review round

- **Hanja conversion, entirely.** `PATHIME_OPT_HANGUL_HANJA` and
  `PATHIME_KEY_HANGUL_HANJA` are gone, along with any use of libhangul's
  `HanjaTable`/`HanjaList` API. Hangul now produces **no candidates at all**, so
  `PATHIME_OPT_MAX_CANDIDATES` reports itself unsupported there, and the UCS-4
  vs. UTF-8 split inside libhangul no longer matters to us. `docs/libhangul-*.md`
  still describe the Hanja subsystem: they document the reference implementation,
  not our scope.
- **`PATHIME_KEY_HANGUL` and `PATHIME_KEY_HIRAGANA_KATAKANA`.** Neither is a key
  any engine dispatches on. In their reference engines both are rebindable
  hotkeys driving something this API puts on the client's side: `IBUS_Hangul`
  only ever appears in ibus-hangul's `switch_keys` list
  (`refs/ibus-hangul/src/engine.c:422`), which calls
  `ibus_hangul_engine_switch_input_mode()` (`engine.c:1432`) — activation state,
  excluded from the model; `Hiragana_Katakana` only appears in ibus-anthy's
  keymap tables, whose handlers (`engine/python3/engine.py:2128-2160`) call
  `__set_input_mode` and touch no composition state — that is
  `PATHIME_OPT_ANTHY_KANA_SCRIPT`. `HENKAN` and `MUHENKAN` stay: they bind to
  convert, cancel, and candidate navigation, which are real composition
  operations. The general rule is now stated in the header — a key that changes
  a mode rather than the composition belongs to the client.
- The surrounding-text surface **stays**, and its justification changed hands:
  not hanja but `PATHIME_HANGUL_PREEDIT_NONE`, the no-preedit mode in which the
  syllable is built up inside the client's document by deleting the partial form
  and recommitting a fuller one (ibus-hangul's `PREEDIT_MODE_NONE`,
  `docs/libhangul-mapping.md:158`). It is the only thing in the library that
  sets either `PATHIME_REQUIRES_*` bit.

### One claim to re-check during implementation — **answered, and acted on**

Whether `PATHIME_OPT_PINYIN_FUZZY` and `PATHIME_OPT_PINYIN_CORRECTION` are
Pinyin-only was traced through pyzy's `bopomofo_table` while writing the
adapter, and the answer is **split**: fuzzy *is* reachable from bopomofo (61
rows carry a `PINYIN_FUZZY_*` bit), correction is not (zero rows reach a
`PINYIN_CORRECT_*` bit). `src/options.cc` widened the fuzzy row to `kPyzy` and
left correction at `kPinyin`; the derivation and the behavioural confirmation
are in the comment at `src/options.cc:229`.

Both keep their `PATHIME_OPT_PINYIN_` prefix: the name describes what the rules
are *about*, and bopomofo reaches them only by being parsed into pinyin first.

**Done (2026-07-27):** the header now names the engines on both. Fuzzy reads
"Pinyin, Bopomofo."; correction reads "Pinyin only." and carries a paragraph
saying *why* it does not extend — a correction repairs a Latin spelling and
there is none to repair when the syllable was typed as bopomofo, whereas fuzzy
rules do extend because bopomofo is parsed into pinyin before it is matched.

## 2. The adapter layer

Wrapping each backend behind the header. The numbered findings below came out of
the mapping review and still hold; they are the reason the adapter is more than
a thin shim. The numbering is inherited from the old `PLAN.md`, because
`docs/*-options.md` cites these findings by number.

### Finding 1 — the internal model must be richer than the API projection

Every backend keeps state that the flat `{preedit, preedit_settled,
auxiliary, candidates}` value cannot hold: anthy has N segments, each with its
own candidate array, plus an active-segment index; pyzy's preedit is three parts
(`selectedText | conversionText | restText`) with the middle one provisional and
its own focused-candidate index; libhangul exposes only the trailing mutable
syllable, so the settled prefix must be accumulated by us. Keep the structured
form internally and compute the flat value at the boundary on every change. The
API's `preedit_settled` is the boundary between the settled prefix and the
still-mutable region, and the candidate list always describes the leftmost
unsettled span — that projection is what makes greedy resolution work.

### Finding 2 — we own the "currently shown" candidate

Neither anthy nor pyzy durably records which candidate the user is hovering
before commit — anthy records only at `anthy_commit_segment` time. The input
context must track it itself.

### Finding 3 — two-layer lifetime everywhere

Process-global one-time init (pyzy's shared SQLite `Database` and
`SpecialPhraseTable` via `InputContext::init()`; anthy's `anthy_init()`) is
separate from per-context handles (`HangulInputContext*`, `anthy_context_t`,
`PyZy::InputContext*`), which are one owned handle each and caller-destroyed.

**Corrected during the `src/` stub-out:** this finding used to name libhangul's
keyboard registry via `hangul_init()` as a third case. It is not one in our
build. `hangul_init()`/`hangul_fini()` exist only under
`ENABLE_EXTERNAL_KEYBOARDS` (`libhangul/hangul/hangul.h:99-103`,
`hangulkeyboard.c:994-1033`), which the top-level `CMakeLists.txt:34` turns off
to avoid an EXPAT dependency and a `sed -i` codegen step. Without it the nine
built-in layouts are static tables and hangul has **no** process-global setup at
all — it is the one backend whose availability cannot fail at runtime. Recorded
at the two places that would otherwise have coded around it, in `src/init.cc`'s
`pathime_init()` and `src/engine.cc`'s registry.

This one is already realized in the API: `pathime_init()` / `pathime_shutdown()`
are the global layer, `pathime_engine_*` holds what is shared across contexts,
and `pathime_context_*` is the per-context layer. It stays listed because the
options docs classify options by which layer they belong to, and because it is
the reason `pathime_init()` is documented as the one slow call.

### Finding 4 — encoding is not uniform

Conversion happens at every boundary. libhangul's composition API is UCS-4 and
everything we hand out is UTF-8; anthy is UTF-8 only after
`anthy_context_set_encoding(ctx, ANTHY_UTF8_ENCODING)` and its `seg_len` counts
input reading xchars, not bytes; pyzy is UTF-8 but its `cursor()` is a byte
offset into the raw ASCII input, which must never be conflated with output
scalar positions. Returned strings from all three are borrowed and volatile —
valid only until the next mutating call — so copy immediately.

### Finding 5 — push and pull must be reconciled

pyzy fires six granular `Observer` callbacks synchronously *during* a mutation
call; anthy and libhangul are pull-only. A small per-context Observer that sets
dirty flags, plus a post-call assembly step, reconciles all three into one
atomic composition value. No event loop is needed. (ibus-pinyin does not buffer,
so it is not a model for this.)

### Finding 6 — we own the whole key-event layer

The backends take only finished input: anthy wants completed kana, pyzy
accepts only `[a-z]` and `'`, and libhangul takes a US-QWERTY `int` (uppercase =
Shift, no modifiers, no release, backspace via a separate
`hangul_ic_backspace`). Everything from `pathime_key_event_t` to
handled/unhandled to preedit assembly — including the romaji/kana state machine
for Japanese — is ours. Only candidate retrieval, selection, and commit map onto
backend calls.

### Obligations from the API round

**Candidates must be materialized eagerly, up to the cap.** The header promises
`pathime_context_candidate()` is callback-safe, which is only true if every
candidate the cap allows is fetched *before* `composition_changed` is
dispatched. pyzy's `hasCandidate(i)` is lazy and mutating, so this is a real
obligation, not a formality.

Per-backend gotchas (flush semantics, the unknown-keyboard crash, the two-call
length protocol, which pyzy properties are honoured only in subclasses) are
documented in `docs/*-mapping.md` with file:line citations. Consult those rather
than re-deriving.

## 3. Open design questions

Both of the original two are now **answered**; they are kept here because the
reasoning is what stops them being reopened.

1. **Internal composition representation — answered.** `src/composition.h`.
   Laid side by side the three backends describe one three-part picture:

   |        | settled            | active             | tail              |
   |--------|--------------------|--------------------|-------------------|
   | pyzy   | `selectedText()`   | `conversionText()` | `restText()`      |
   | anthy  | segments < active  | segment[active]    | segments > active |
   | hangul | finished syllables | trailing syllable  | (always empty)    |

   So `Composition` is three strings, a candidate list for the active span, and
   a cursor into it. The projection is a concatenation and a scalar count, and
   `preedit_settled` falls out as the length of `settled`. There is
   deliberately no segment array and no active index — anthy has both and keeps
   them privately. `backend.h` follows from it; `docs/source-layout.md` has the
   fuller note.

2. **Where the romaji/kana and key-dispatch layer lives — answered per-engine.**
   The engine-agnostic part is `src/keys.*` (validation, chording, keysym to
   scalar/ASCII); the romaji state machine is `src/engines/anthy/romaji.*`,
   because only Japanese needs one before its backend sees input. It holds no
   anthy types, so hoisting it into `src/` stays cheap.

### Open now, in their place

3. **The composition model has no cursor inside a span, and two adapters have
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

4. **Nothing moves `Composition::cursor` except a selection.** The API has no
   candidate-hover operation — a client paginates and displays for itself, and
   `pathime_context_select_candidate()` is the only thing it can call. So the
   currently-shown candidate this library tracks (Finding 2) is only ever set at
   the moment it stops being ours. That is coherent, but it means pyzy's
   `focusCandidate()` can never be driven and the active span always shows
   candidate 0's text however the client's highlight moves. If a preedit that
   follows the hover is wanted, `ContextBackend` needs a `set_cursor(size_t)`
   and the API needs an operation to reach it.

Resolved by the API round, recorded so they are not reopened: candidates are
active-region-only (greedy, no segment navigation); lazy enumeration is hidden
behind `PATHIME_OPT_MAX_CANDIDATES`; the canonical text unit is UTF-8
with all positions in Unicode scalar values.

Resolved since: **the table engine is ours to write** — see §4.

## 4. The table engine

`ibus-table` cannot be a backend. It is Python, so there is nothing to link
against; what it offers is a proven feature set, not a library. It is the
reference we trust for the table-driven methods we want (Cangjie, Wubi, and the
rest), and `refs/ibus-table-chinese` supplies real tables to test against.

The decision: **libpathime implements its own table engine**, a peer of the
vendored submodules rather than a wrapper around one. `docs/ibus-table-spec.md`
is already the specification for it — source `.txt` format, compiled SQLite
schema, key-event state machine, candidate sorting — and
`docs/ibus-table-options.md` is its option inventory.

What that means for the API work now:

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

So the options round can treat table as a real fourth engine and design against
it; nothing is blocked on writing it. Implementation follows the spec, after the
adapter layer for the three backends that do exist.

## 4a. What the first vertical slice left undone

The first slice — key in, composition and commit out, for all three backends —
is implemented and tested end to end (`api.engine_hangul`, `api.engine_anthy`,
`api.engine_pyzy`). What it deliberately did not reach:

- **`PATHIME_HANGUL_PREEDIT_NONE`.** Resolved but not implemented: it currently
  behaves as `_SYLLABLE`, which is *not* what the header documents. It is the
  only consumer of the surrounding-text surface and the only thing that sets
  either `PATHIME_REQUIRES_*` bit, so it is the natural next slice — it
  exercises `Output::request_deletion()`, the delete-before-commit dispatch
  order, and the snapshot-invalidation rule in one go.
- **`PATHIME_ANTHY_TYPING_KANA`.** Marked, and declines every key rather than
  silently falling through to romaji.
- **`PATHIME_OPT_LEARNING` on pyzy — decided, not yet applied.** The header says
  the library implements it for anthy and pyzy "by withholding the learning
  commit". That works for anthy — `anthy_commit_segment` is a separate call we
  can skip — but pyzy learns *inside* `selectCandidate()`/`commit()` via
  `PhraseEditor::commit()`, and its public header exposes no switch, only
  `resetCandidate()` to unlearn one entry afterwards.

  **Decision (2026-07-27): report it unsupported on pyzy.** The rejected
  alternative was redirecting pyzy's user-cache directory from
  `pyzy_global_init()`. That was turned down because it does not fix the second
  mismatch: the option is per-context while pyzy's user database is
  process-global, so two contexts disagreeing about learning would still be
  unrepresentable — the redirect would buy a half-true implementation at the
  cost of a changed global-init contract. Unsupported is the honest report.
  **To do:** widen the option's engine set to exclude pyzy in `src/options.cc`,
  and amend the header to promise anthy only.
- ~~**pyzy's availability cannot be detected.**~~ **Done (2026-07-27.)**
  `pyzy_database_present()` in `src/engines/pyzy/pyzy_backend.cc` runs in front
  of `PyZy::InputContext::init()` and mirrors `Database::open()`'s four
  candidates (`Database.cc:247-252`) with the same `stat`/`S_ISREG` predicate
  glib's `G_FILE_TEST_IS_REGULAR` uses; `PKGDATADIR` reaches the adapter as
  `PATHIME_PYZY_PKGDATADIR`, derived in `src/CMakeLists.txt` from the same
  expression the pyzy port uses so the two cannot drift. Returning false leaves
  `PyZy::InputContext::init()` uncalled, which keeps `finalize()` balanced.
  Covered by `api.engine_pyzy_nodb`, which is registered only when the
  configure-time probe finds no system-wide pyzy database — its premise is a
  property of the machine, not of the build.

  Two things worth keeping: the conversion probe is still *not* the answer (with
  no database open `m_db` is NULL and the query path dereferences it, so the
  probe meant to detect the broken install is what crashes on it), and the
  mirrored candidate list is now duplicated in `tests/api/CMakeLists.txt`'s
  probe as well — three places if pyzy's list ever changes, in a vendored tree
  we do not edit.
- **`ContextBackend::options_changed()` closed a real gap, and there may be
  more of its kind.** A mid-composition option change reached the store and the
  getters but not the engine: pyzy had already converted, and "options are
  pulled" meant pulling at the next keystroke, of which there might be none.
  The hook carries the *moment*, not the value. Two adapters take the default
  no-op honestly — hangul and anthy consult options at the point of use — so
  the question to ask of any new option is whether the backend has derived
  state that outlives the call.

- **pyzy's wrapper-only options.** `PATHIME_OPT_LATIN_WIDTH`,
  `PATHIME_OPT_PUNCTUATION_WIDTH` and `PATHIME_OPT_PINYIN_SHOW_RAW` are
  ibus-pinyin features with nothing behind them in pyzy itself.

Three smaller header/implementation divergences, all pinned down by tests:

- `src/init.cc` rejects a non-NULL but *empty* `data_dir` as
  `INVALID_ARGUMENT`. The behaviour is right — NULL and `""` must not both mean
  "use the default" — but the header documents only what NULL does.
- The descriptor reports `max-candidates`'s maximum as `INT64_MAX`. The header
  states a minimum of 1 and no maximum, leaving the representation open.
- `Return` on pyzy commits the **raw** input (`"nihao"`), not the 你好 the
  preedit is showing. Verified, and it matches ibus-pinyin. **Decided
  (2026-07-27): keep it, and write the rule into the header** so it reads as
  chosen rather than accidental — `Return` means "I did not want conversion,
  give me what I typed", and it is the only key that escapes conversion without
  backspacing out of the composition. Documenting it is the whole fix; the
  behaviour does not change.

## 5. Loose ends

- **pyzy's user-database save is handled — do not go looking for a save call.**
  This used to read as an open obligation: `Database` schedules its save with
  `g_timeout_add_seconds` (`pyzy/src/Database.h:98-101`) and the timeout never
  fires, because nothing runs a `GMainLoop`. But the timeout id is still set,
  which is exactly what `~Database` tests before saving, so
  `Database::finalize()` at our global-shutdown does the save. Recorded because
  the next reader will re-derive the first half and conclude, wrongly, that
  learning is being dropped. `src/engines/pyzy/pyzy_backend.cc:816`.

- **The JIS ¥-vs-ろ distinction cannot be expressed.** `layout_key` is a
  US-QWERTY keysym and neither key exists on that layout, so anthy's one
  keycode-dependent case is lost. Accepted: it is kana-hardware-only and
  irrelevant to the phone-keyboard target.
- **Thread-safety is a documented requirement, not an enforced one.** Nothing
  detects overlapping calls. If that proves to be a common client bug, a debug
  build could catch it cheaply with a non-recursive in-call flag per context.

- **A backend's global init failing is per-engine, not fatal — and that was a
  bug first.** The first wiring of `pathime_init()` returned
  `PATHIME_ERROR_BACKEND` if any compiled-in backend's hook failed. Running the
  three adapters together for the first time showed what that costs: anthy
  cannot find its dictionary in an uninstalled build tree, and one missing data
  file took down hangul and pyzy with it. The header already had the right
  channel — `pathime_has_engine()` is documented false for an engine "whose
  runtime prerequisites, such as its dictionaries, are unavailable" — so
  `init.cc` now records per-backend readiness and `engine_available()` consults
  it, and `pathime_init()` succeeds. `PATHIME_ERROR_BACKEND` from
  `pathime_init()` would mean the library itself is unusable, which is a
  different and much rarer claim.

- **Neither anthy nor pyzy can find its data in an uninstalled build tree.**
  This is a test-environment fact, not a library bug: both find their own data
  once installed. `tests/api/CMakeLists.txt` solves it the way the vendored
  suites do — `anthy_conf_override()` with build-tree paths for anthy, a staged
  `main.db` plus a per-test working directory for pyzy. Worth knowing before
  anyone "fixes" the library to hunt for data files itself.

- **Two header sentences the stub-out had to interpret.** Both were decided in
  code with the reasoning written at the decision point; both are cheap to flip
  if the reading is wrong, and neither is a design change.

  `pathime_*_reset_option()` says "resetting an option that was never set is a
  no-op" and, in the next sentence, "behaves in every other respect like a
  setter, including resetting the composition for options that require it."
  Taken literally: nothing was dropped, so nothing resolves differently, so
  there is no composition reset and no dispatch. The second sentence governs
  the case where a value *was* set. `src/options.cc`'s `reset_option()`.

  Whether a *context*-level set dispatches `composition_changed` is spelled out
  in the header only for the engine level, where it matters because the
  callbacks belong to contexts the caller never passed. The context form was
  made symmetric, on the strength of "when a change takes effect: always
  immediately."

- **`src/candidates.h` still does not exist, and the trigger has now fired.**
  `materialize_candidates()` is declared at the top of `src/context.cc` rather
  than in a header, on the grounds that one function nothing else names is not
  worth one. `src/candidates.cc` now has real work in it, so the condition that
  note set for itself is met: the enumeration entry point, the currently-shown
  cursor accessor, and the selection path all want declaring in one place.
  Mechanical, do it with the next change that touches the pump.

- **The header explains itself against the backends; one day it should not.**
  Much of the commentary in `include/pathime/pathime.h` justifies a decision by
  naming what a backend does — anthy's write-once personality, pyzy's
  creation-time `InputType`, libhangul's nine built-in layouts, which engines
  learn unconditionally. That is exactly right *now*: the implementation does
  not exist, the reasoning is not yet embodied in code anyone can read, and
  these notes are what keep the design honest and stop settled questions being
  reopened. Once the library is written, the same notes become a liability — a
  client does not care which backend forced a choice, and a reader who does can
  find it in `docs/*-mapping.md`, which is where it belongs.

  So: after the implementation lands, make a deliberate pass over the header
  and move backend-specific justification out of it, leaving the contract and
  the rules a client must obey. The two tests for whether a passage stays are
  whether a client's behaviour depends on it, and whether removing it would let
  someone reopen a question this design already closed. Do not do this early —
  removing the reasoning before the code replaces it loses it entirely.
