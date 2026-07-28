# libpathime — design history

The settled rounds, in the words they were settled in. This file exists so
that `TODO.md` can stay what its name says — upcoming work — without losing
the record that keeps decisions from being reopened: every entry here is a
question that was asked, the evidence it was answered against, and what the
answer cost. It was split out of `TODO.md` on 2026-07-28; the section numbers
(§1, §2, §3, §4a, §4b, §4c, §5) are preserved from that file because code
comments and other docs cite them, and they must not be renumbered. The
adapter-layer findings are §2, back under their original number. §6, the table
engine, was written here directly rather than carried over, and follows the same
rule.

Nothing here is pending. For what is, read `TODO.md`; for the model, read
`docs/CONCEPTS.md`; for the contract, `include/pathime/pathime.h`.

---

## The ledger — what is built (as of 2026-07-28)

Done:

- **The build.** CMake over the three vendored submodules, verified on Linux and
  Windows (`BUILD.md`). `include/pathime/config.h` is generated from
  `cmake/pathime-config.h.in`.
- **The model and the contract.** `docs/CONCEPTS.md` and
  `include/pathime/pathime.h`, both settled and kept in lockstep.
- **The core.** All 44 public entry points, implemented rather than stubbed:
  process-global lifetime and `data_dir` resolution, the engine registry, both
  handle lifecycles, focus, the surrounding-text snapshot, the key layer
  (`keys.*`), the encoding boundary (`utf8.*`), the structured composition
  model and its projection (`composition.*`) — including `Output`, which is
  where a mutation's pending commit text and deletion range live until
  `refresh_composition()` dispatches them in the header's fixed order — the
  eager candidate pump (`candidates.cc`), and the options machinery: a 31-row
  descriptor table, four-tier resolution, the two-level store, and the
  `struct_size` protocol.
  `docs/source-layout.md` maps which file owns what.
- **The seam.** `src/backend.h` and `src/composition.h` — §3's two deferred
  questions, answered together against all three mapping docs.
- **The first vertical slice, across all three backends.** Key in, composition
  and commit out: real Korean, Japanese and Chinese, driven through the public
  C API and verified end to end. `src/engines/{hangul,anthy,pyzy}` implement
  `backend.h`, and `src/engines/anthy/romaji.*` is the composing front end anthy
  needs. What the slice left undone was §4a, since closed.
- **The first client.** `demo/`, an interactive IME in a terminal, behind
  `LIBPATHIME_BUILD_DEMO`. It exists to be *used*, not to verify anything — but
  being the first program written against the header rather than to test it
  found six places where a client has to work around the API's shape. Those
  were §4b, and **all six are now closed** — three by new API, three by saying
  in the header what was already true.
- **The preedit rule, and the removal of auxiliary text.** §4c. Asking why the
  Chinese and Japanese engines disagreed about which field held what the user
  typed produced one rule covering all four engines, and left the auxiliary
  text field with nothing in it. `docs/japanese-input-model.md` is the
  measurement behind it.
- **The eager candidate strip** (2026-07-28). `PATHIME_OPT_PREDICTION` is
  implemented on anthy as eager conversion, default **on**: candidates from
  the first keystroke, the preedit staying kana, the cursor browsing until
  Space adopts it. The end of §4c records the rulings — optional and why,
  default, adoption, one option spanning anthy and table, the span gap
  accepted — and the strip-selection semantics built with it.
- **The table engine.** §6 — the fourth engine, written here rather than
  wrapped, to `docs/ibus-table-mapping.md`. Shape in §6a, the behaviour round in
  §6b, and what was ruled out of scope in §6c. Five tables ship, trimmed at
  build time to a font's glyph coverage.
- **Tests: 34, all passing.** `tests/core/` compiles internal sources directly,
  because internal helpers carry no `PATHIME_API` and a shared build would not
  export them. `tests/api/` holds the ABI, lifecycle and options suites plus one
  end-to-end test per engine; those three need their backend's *data*, which is
  why they are the first thing there that is gated —
  `tests/api/CMakeLists.txt` explains why that is a test-environment problem
  rather than a library one.

---

## 1. The options round — held, deferred, and cut

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
`include/pathime/pathime.h`. 31 options, 8 of them common to several engines and
23 engine-specific, set through three kind-typed setters at two levels, with a
descriptor query so a client can present options it does not know by name, and
`pathime_option_count()` so it can walk options its own header never named.
`pathime_context_set_max_candidates()` is gone, folded in as
`PATHIME_OPT_MAX_CANDIDATES`. `pathime_init()` gained a params struct carrying
`data_dir`, which is the whole of the library's persistent-storage surface.

Roughly three quarters of the options catalogued in the per-backend
inventories that fed this round were
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

  **That bullet examined the wrong handler, and the correction is the next
  entry.** `Hiragana_Katakana` really is a mode switch. But ibus-anthy has a
  *second*, unrelated family that converts the composition itself, reached
  through a different handler, and this round never looked at it.
- **Per-composition character-type conversion — considered and left out of
  scope (2026-07-28).** ibus-anthy's F6–F10 family: convert what has already
  been typed to hiragana (F6), katakana (F7), half-width katakana (F8), wide
  latin (F9) or latin (F10), plus the `_all` variants. They run through
  `__on_key_conv` (`engine/python3/engine.py:1590-1612`), which sets
  `__convert_mode` and makes `__update_convert_chars` (`engine.py:1226-1228`)
  re-render the preedit in the chosen script. Guarded `_chk_mode('12345')`, so
  they are live while composing.

  This is **not** `PATHIME_OPT_ANTHY_KANA_SCRIPT`, which chooses what typing
  produces going forward; F7 converts what is already there. It is a real
  composition operation and the bullet above should have caught it.

  **Out of scope**, decided by the API owner: there are no F-keys on the
  phone-keyboard target, and the ibus-based client this API was shaped against
  never needed them. Two things soften it. Katakana is *partly* reachable
  already, because conversion offers it as a candidate — にほんご gives
  `[2] ニホンゴ`, わたし gives ワタシ — though the position varies with the
  record and some readings do not offer one at all. And the operation would be
  anthy-and-table-only, since pyzy has no equivalent, so it would be a concept
  carried for one and a half engines.

  What is genuinely lost, stated plainly: half-width katakana, wide latin and
  latin are reachable by **no** route at all.

  Cheap to add if a consumer appears. It is one additive operation, and
  `RomajiComposer` already splits `display()` from `commit_text()`, which is the
  whole of the F6 case.

- **A misreading worth not repeating: no key resolves pending romaji without
  committing or converting, and Tab is not an exception.** Typing `nihon` leaves
  `にほn`, because one more key still decides between ん and な. It is tempting
  to read ibus-anthy's `__cmd_predict` (Tab) as resolving it, since it calls
  `get_hiragana(True)` — but `get_hiragana()` is a *pure read*, building from
  `__segments` and never assigning back, so the resolved form goes only to
  `set_prediction_string()` as a lookup key. The displayed preedit comes from
  `__get_preedit()` with `commit` defaulting to false and still reads `にほn`;
  with no predictions, `__cmd_predict` returns False and nothing changed at all.

  F6 is the key that really does it (see above), and it is out of scope. So the
  routes to `にほん` are: type the second `n`, press Return, or convert. That is
  what ibus-anthy offers too, and typing `nn` is what Japanese typists do.

  The confusion is easy to come by honestly: ibus-anthy binds Tab to *both*
  `predict` (modes 1,4) and `select_next_candidate` (modes 2,3,5), so what Tab
  appears to do depends on state. `docs/japanese-input-model.md` §2 has the
  binding table.

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


---

## 2. The adapter findings

The six numbered constraints that shape the adapter layer — why it is more than
a thin shim over the three vendored libraries. They came out of the mapping
review (`docs/*-mapping.md`), still hold, and are **cited by number from `src/`
comments**; the numbering is inherited from the old `PLAN.md` and must not be
reused or reordered.

These lived in `docs/design-history.md` §2 until 2026-07-28, and in `TODO.md` §2
before that — which is why this file's numbering used to skip §2. They are back
under their original number because the code cites them and the constraints are
still true of it: a reader changing `backend.h` needs Finding 6 to know why
backends are handed finished input, not a record that someone once decided it.

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
`ENABLE_EXTERNAL_KEYBOARDS` (`engines/libhangul/hangul/hangul.h:99-103`,
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
---

## 3. The early open design questions

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

### The later pair, in their place

3. **The composition model has no cursor inside a span** — the one question
   from this section still open. It moved to `TODO.md` (the in-span cursor
   question) when this file was split out, evidence and all.

4. ~~**Nothing moves `Composition::cursor` except a selection.**~~
   **Answered, and built (2026-07-28.)** It ends exactly where this note
   predicted: `ContextBackend::set_cursor(size_t)` and a public operation
   reaching it, `pathime_context_set_candidate_cursor()`, with the position
   published in `pathime_composition_t::candidate_cursor`. pyzy's
   `focusCandidate()` is driven and the active span follows the hover. See §4b
   for what else came with it — chiefly that anthy's Up/Down bindings went, so
   that navigating a list is the client's decision and not two engines'.

Resolved by the API round, recorded so they are not reopened: candidates are
active-region-only (greedy, no segment navigation); lazy enumeration is hidden
behind `PATHIME_OPT_MAX_CANDIDATES`; the canonical text unit is UTF-8
with all positions in Unicode scalar values.

Resolved since: **the table engine is ours to write** — see `TODO.md`.

---

## 4a. What the first vertical slice left undone

The first slice — key in, composition and commit out, for all three backends —
is implemented and tested end to end (`api.engine_hangul`, `api.engine_anthy`,
`api.engine_pyzy`). What it deliberately did not reach:

- ~~**`PATHIME_HANGUL_PREEDIT_NONE`.**~~ **Done (2026-07-27.)** It behaves as
  documented: each jamo goes into the client's document as it is struck, and
  the syllable grows by deleting what was written a moment ago and committing
  the fuller form. Backspace does the same in reverse. Ending the composition
  leaves the syllable where it is rather than committing it again — safe
  because `hangul_ic_flush()` returns exactly the preedit string standing at
  the moment of the call, measured identical across all nine built-in layouts
  and 72 key sequences, including the three-set jaso forms that come back with
  `U+1160` fillers intact.

  The slice needed one seam change, which is the part worth knowing about:
  `ContextBackend::process_key()` gained a `const SurroundingTextView &`. The
  header's recovery for a snapshot that no longer covers the text — "abandon
  the revision, treat what is already in the document as final, continue as if
  starting fresh" — cannot be performed by the dispatch that drops the
  deletion, because by then libhangul has already folded the key into the old
  syllable and the commit is decided. So the adapter asks first, and
  `hangul_ic_reset()`s when the answer is no. `src/backend.h` explains why the
  view is one boolean question rather than a window onto the text.

  Two things were found and fixed on the way. `refresh_composition()`'s
  dispatch comment claimed it only delivered deletions "where the snapshot
  actually covers the range" and did not in fact check the range; it does now,
  through `range_within_snapshot()`, which is the same predicate the adapter's
  view asks. And `composition_changed` correctly never fires in this mode —
  the composition is empty before and after every key — which is asserted
  rather than left to look like an omission.

  Covered by four cases in `api.engine_hangul`: the build-up, backspace, the
  no-double-commit on end, and the stale-snapshot recovery. Both guards were
  mutation-tested: removing the end-of-composition guard produces `한한` and
  removing the pre-flight check produces `ㅎ하한`, and the tests catch each.
- ~~**`PATHIME_ANTHY_TYPING_KANA`.**~~ **Done (2026-07-27.)**
  `RomajiComposer::insert_kana()`. One key, one kana, with the dakuten and
  handakuten keys folding into the kana before them where a voiced form exists
  and standing alone where none does — both halves being ibus-anthy's
  `KanaSegment::append()`.

  Simpler than the romaji machine because kana entry has nothing to pend:
  every key resolves on the spot, so `pending_` stays empty for the whole mode
  and `kana_` alone is the state. Whether the previous kana takes a mark is a
  property of that kana, so it is read off the last scalar rather than tracked
  — ibus-anthy keeps an `is_finished()` flag for it, and the flag is derivable.

  The table is ibus-anthy's `kana_typing_rule_static`, whose own comment calls
  it a port of scim-anthy's `101kana.sty`: the JIS kana arrangement over
  **US-101 key positions**. That is exactly what `KeyEvent::position_key()`
  already gives, so it transcribed with no remapping and a client never has to
  say what keyboard is attached. 94 of its 95 rows are here; the missing one is
  `'¥' -> ー`, the JIS ¥-vs-ろ case §5 records as unrepresentable, and nothing
  is lost by it because `'|'` gives ー from the US backslash position anyway.

  **Verified against the reference, not just spot-checked.** All 94 reachable
  rows and all 25 dakuten/handakuten pairs were driven through
  `pathime_context_process_key()` and diffed against `tables.py`: zero
  mismatches, no extra keys accepted, no table key declined. Worth redoing if
  the tables are ever edited; the note is in `romaji.cc`'s file comment.

  One refactor came with it: the US-QWERTY position-plus-Shift recombination
  moved from the hangul adapter into `keys.cc` as `us_layout_char()`, because
  libhangul is no longer the only backend that dispatches on where a key is.
  Including `keys.h` there also surfaced a duplicate `is_chorded()` in the
  hangul adapter, now removed.

  Covered in `api.engine_anthy`: the layout, both marks, a mark with no voiced
  form, a leading mark, backspace, and a full かんじ -> 漢字 conversion typed
  off the kana positions — the last being the one that shows the typing method
  chooses only how a reading is entered and nothing downstream of it.
- ~~**`PATHIME_OPT_LEARNING` on pyzy.**~~ **Done (2026-07-27.)** The header used
  to promise the library implemented it for anthy and pyzy "by withholding the
  learning commit". True for anthy, where `anthy_commit_segment` is a separate
  call the adapter skips; false for pyzy, which learns *inside*
  `selectCandidate()`/`commit()` via `PhraseEditor::commit()` with no public
  switch, only `resetCandidate()` to unlearn one entry afterwards.

  Resolved by **reporting it unsupported on pyzy**: the descriptor row in
  `src/options.cc` is now `kAnthy | kTable`, and the header's engine list and
  last paragraph say so. The rejected alternative was redirecting pyzy's
  user-cache directory from `pyzy_global_init()`; it was turned down because it
  does not fix the *second* mismatch — the option is per-context while pyzy's
  user database is process-global, so two contexts disagreeing about learning
  would still be unrepresentable. Pinned by `core.options`, both in the
  descriptor cross-check and as an explicit setter refusal on each pyzy id with
  anthy as the contrast case.
- ~~**pyzy's availability cannot be detected.**~~ **Done (2026-07-27.)**
  `pyzy_global_init()` in `src/engines/pyzy/pyzy_backend.cc` tests for
  `<resource_dir>/pyzy/main.db` in front of `PyZy::InputContext::init()`, using
  the same `stat`/`S_ISREG` predicate glib's `G_FILE_TEST_IS_REGULAR` uses —
  which is the test pyzy itself applies, so the two cannot disagree. Returning
  false leaves `PyZy::InputContext::init()` uncalled, which keeps `finalize()`
  balanced. Covered by `api.engine_pyzy_nodb`.

  Worth keeping: the conversion probe is still *not* the answer. With no
  database open `m_db` is NULL and the query path dereferences it, so the probe
  meant to detect the broken install is what crashes on it.
- **`ContextBackend::options_changed()` closed a real gap, and there may be
  more of its kind.** A mid-composition option change reached the store and the
  getters but not the engine: pyzy had already converted, and "options are
  pulled" meant pulling at the next keystroke, of which there might be none.
  The hook carries the *moment*, not the value. Two adapters take the default
  no-op honestly — hangul and anthy consult options at the point of use — so
  the question to ask of any new option is whether the backend has derived
  state that outlives the call.

- ~~**pyzy's wrapper-only options.**~~ **Done (2026-07-27.)** All three are
  ibus-pinyin features with nothing behind them in pyzy, but that turned out to
  be the *only* thing they had in common — unlike `PATHIME_OPT_LEARNING`, none
  of them ran into an obstruction, so the answer was work rather than a
  decision, and all three are now implemented.

  `PATHIME_OPT_LATIN_WIDTH` and `PATHIME_OPT_PUNCTUATION_WIDTH` are
  `src/punctuation.*` (written under `src/engines/pyzy/` and hoisted when the
  table engine turned out to need the same behaviour), transcribed from
  ibus-pinyin's
  FallbackEditor: two variant-specific substitution tables, the half-to-full
  arithmetic, and the three values that need memory (the two quote
  alternations and the digit-then-period look-behind). Full-width punctuation
  is ordinary Chinese orthography rather than a nicety — an engine that cannot
  produce ，and 。is not one you could write Chinese with — which is why
  reporting them unsupported was never really on the table.

  **Locating the reference settled a placement this repo had guessed wrong.**
  `pyzy_backend.cc`'s composing switch carried a comment saying the width work
  belonged in its `default:` arm. It does not: ibus-pinyin's
  `PinyinEditor::processPunct` returns FALSE only on *empty* input
  (`PYPinyinEditor.cc:82-84`), which is what lets a key fall through to the
  FallbackEditor at all, so the substitution runs when nothing is composing.
  The emit path now sits after the switch and the comment is gone.

  Two departures from ibus-pinyin, both deliberate:

  - Punctuation width governs *all* punctuation. ibus-pinyin falls back to the
    *Latin* width flag for punctuation its table has no row for, so its `@`
    stays ASCII while its `!` does not. The header says punctuation width is
    "the same choice for punctuation" and the anthy front end already reads it
    that way; one option governing one class of character beat fidelity to a
    split the reference never explains.
  - A key arriving mid-composition is not lost. ibus-pinyin swallows it unless
    its auto-commit option is on; here the composition is ended first — taking
    the hovered candidate, as Space does — and both commits are dispatched in
    order. Losing text the user typed is not a behaviour worth copying.

  The cost, stated plainly: these engines now report **every** printable key
  handled, including ones they pass through unchanged at half width. That is
  what ibus-pinyin does and it is the only way the width options can apply at
  all — a key the client inserts itself is one we never saw — but it is a real
  narrowing of what reaches the client, and it is now in the header.

  Writing the emit path surfaced a latent bug it had been hiding: a leading
  apostrophe went to pyzy as a syllable separator, which pyzy accepted and
  rendered as nothing at all — an invisible composition and no text.
  `insertable()` now takes the composing state and routes it the way
  ibus-pinyin does, so with nothing typed it is an opening quotation mark.

  `PATHIME_OPT_PINYIN_SHOW_RAW` is in `harvest()`: the raw input appended to
  the auxiliary text in ibus-pinyin's brackets, minus the spaces it pads them
  with, which are layout. Double pinyin only — the mode ibus-pinyin implements
  it for and the only one where the two texts differ. It also needed
  `options_changed()` to force an auxiliary refresh, because pyzy fires no
  `auxiliaryChanged` for an option it has never heard of.

  Covered by `api.engine_pyzy` (767 checks, up from 563): both variants' tables,
  every width combination, the stateful rules, the mid-composition case, the
  space split, the apostrophe's two meanings, and show-raw toggled live. Three
  mutation runs confirm the guards are load-bearing.

Three smaller header/implementation divergences, all pinned down by tests:

- ~~`src/init.cc` rejects a non-NULL but *empty* `data_dir` as
  `INVALID_ARGUMENT`, and the header documents only what NULL does.~~
  **Closed by the header (2026-07-28.)** Stated at `data_dir`, covering
  `resource_dir` in the same breath, with the reason: a caller who built the
  path and got nothing is told rather than silently writing to the default.
- ~~The descriptor reports `max-candidates`'s maximum as `INT64_MAX` while the
  header states no maximum, leaving the representation open.~~ **Closed by the
  header (2026-07-28.)** `pathime_option_info_t::max_value` now says that an
  option documented with no upper limit reports `INT64_MAX`, since the
  descriptor has no way to spell "none".
- ~~`Return` on pyzy commits the **raw** input.~~ **Done (2026-07-27.)** The
  behaviour is unchanged and matches ibus-pinyin; what was missing was the rule
  that makes it read as chosen. `pathime_context_process_key()` now states the
  pair — Space asks for conversion, Return ends the composition without
  applying any conversion the user has not explicitly chosen — and then states
  the consequence outright: Return may commit text differing from the last
  preedit shown, because an engine may *preview* an unchosen conversion, and
  Pinyin and Bopomofo do.

---

## 4b. What the first client found — **all six closed (2026-07-28)**

`demo/` is the first program written *against* this API rather than to test it,
and being a client is a different exercise from being a test: a test knows what
it is checking, so it can reach for whatever the header offers and assert on the
answer, while a client has to build a whole interface out of what is there and
discovers the shape of the holes. These were the holes. None of them blocked the
demo and none was a bug; each cost it either a workaround in `demo/src/` or a
feature it did not have.

The design round has now been held. Three were answered with new API —
`pathime_context_set_candidate_cursor()`, `pathime_context_requirements()`,
`pathime_option_value_name()`, plus `pathime_context_is_focused()` from the
smaller list — and three by writing down in the header what the implementation
already guaranteed. The header grew from 40 public entry points to 44, and
`pathime_composition_t` gained one field; no existing entry point changed shape,
so all of it is additive.

Kept in the order they were found, with what each became.

**The one decision that went the other way:** a `bool candidates_complete` in
`pathime_composition_t` was considered and **declined**. The struct stays as it
is and the completeness rule is stated beside `candidate_count` instead. The
argument for the field was that a backend which *knows* it has more could say
so where the length test cannot — pyzy can be asked, via `hasCandidate(cap)` —
but it would have grown the struct for one engine's benefit and made every
adapter answer a question only one of them finds interesting. Do not reopen this
without a backend whose list length genuinely misleads.

- ~~**A client cannot ask whether a candidate list is complete.**~~ **Closed by
  the header (2026-07-28.)** The rule is now stated beside `candidate_count`: a
  count below the resolved `PATHIME_OPT_MAX_CANDIDATES` was not truncated by the
  cap and there is nothing more to be had; a count equal to it may or may not
  have more behind it. The header also states plainly that the equal case is
  undecidable from there, rather than leaving a reader to wonder.

  `candidates_complete` was the alternative and was declined — see the note at
  the top of this section. What the backends can actually do was checked first,
  because the shape of the answer depended on it: anthy knows its total for free
  (`anthy_get_segment_stat().nr_candidate`), pyzy genuinely does not (candidates
  fill twelve at a time from a SQLite query, `FILL_GRAN` in
  `engines/pyzy/src/PhraseEditor.h:30`), hangul has none, and the table engine's will be
  ours. So the cap is not a wart that could be removed by enumerating
  everything: pyzy is what it exists for.

  `App::grow_candidate_list()` keeps its length test, which is now the
  documented rule rather than a client's guess.

- ~~**`pathime_engine_requirements()` has no context-level counterpart.**~~
  **Done (2026-07-28.)** `pathime_context_requirements()`, in `src/context.cc`:
  the same `resolve_option_number()` call with `ctx` in place of `nullptr`, so
  it also picks up the capping rule in `options.cc`'s `resolve_number()` and
  reports the *effective* value, which is what the note below argued for. The
  engine form keeps its uncapped answer and the header now says why the two
  differ. The demo's "engine requires:" line reads the context form and is
  right about the field in front of the user. Covered by
  `test_context_requirements()` in `api.engine_hangul`, including the case where
  a client that cannot delete caps the value back down.

  The original finding: Requirements depend on the resolved configuration, and
  `PATHIME_OPT_HANGUL_PREEDIT` — the option that drives them — is settable per
  context. The header says so and tells a client that sets it per context to
  "read the requirement from the option's own documentation rather than from
  this call." A client whose settings interface is built by *walking the
  inventory*, which is precisely what the header advertises the inventory for,
  has no documentation to read at runtime.

  The demo shows "engine requires: …" over its text field, and that line is
  wrong the moment a context overrides `hangul-preedit` — it keeps saying
  "nothing" while the context is in the mode that requires both callbacks.

  A `pathime_context_requirements()` closes it exactly, is additive, and is
  nearly free: `pathime_engine_requirements()` is one
  `resolve_option_number(engine, nullptr, PATHIME_OPT_HANGUL_PREEDIT)`
  (`src/engine.cc:236`), and the context form is the same call with `ctx` in
  place of `nullptr`. One question comes with it, and `src/engine.cc`'s comment
  is already half an answer: engine-level resolution deliberately does no
  capability capping, so that `pathime_context_create()` can reject against the
  true value. A context-level query should report the *effective* value — the
  capped one — because a client asking "what do you need from me" has already
  supplied what it has.

- ~~**The currently-shown candidate exists internally and is not exposed.**~~
  **Done (2026-07-28), and it grew into more than a projection.** The answer is
  not a read-only field but a pair —
  `pathime_context_candidate_cursor()` and
  `pathime_context_set_candidate_cursor()` — because the finding understated the
  gap: the model had no candidate *navigation* at all, only selection, which
  mashed hovering and committing into one irreversible step. A client can now
  move the highlight without choosing, and on an engine that previews its
  candidates the preedit follows.

  **The cursor is a `pathime_composition_t` field, plus a setter — and getting
  that wrong first is worth recording.** It was built as a getter/setter pair on
  the reasoning that a struct field would imply the engine could change the
  value at will, which had not been established. It had: Space advances the
  cursor on an engine that converts by cycling (`advance_candidate()`), a span
  settling drops it, and pyzy's observer resets it when its list is
  regenerated. Once that is true the cursor *is* composition data, a getter is a
  second way to read something the client must re-read on every
  `composition_changed` anyway, and the field is the honest shape.

  So the invariant is stated rather than implied: a client draws its highlight
  from the composition and never assumes the cursor is where it last put it.
  `refresh_composition()` includes the cursor in its change comparison, so a
  move is always announced. The setter's documentation says outright that it is
  a request rather than an assignment.

  `ContextBackend::set_cursor()` is the seam, defaulting to `UNSUPPORTED` so
  hangul inherits the honest answer. anthy's `move_candidate(delta)` became
  `show_candidate()` behind both an absolute `set_cursor()` and Space's
  `advance_candidate()`; **anthy's Up/Down bindings were removed**, because a
  key the engine reports handled never reaches the client's binding, which would
  take back the decision this API just handed over. pyzy's `set_cursor()` is one
  `focusCandidate()` call — the function §3 q4 recorded as reachable and
  undriven. Both are covered, and the anthy checks were mutation-tested against
  restoring the arrow binding and against dropping the preedit rewrite.

  `docs/CONCEPTS.md` gained a *Candidate cursor* section, since the model now
  has the concept in public.

  The original finding:
  `Composition::cursor` (`src/composition.h:117`) is per active span, is reset
  when a span settles, and is fed to the backend on selection — the core tracks
  it because neither anthy nor pyzy durably records it (`docs/design-history.md` §2, Finding 2). The
  public API has no way to read it: `pathime_composition_t` carries a count and
  `pathime_context_candidate()` carries text, and nothing says which entry the
  preedit currently reflects.

  Every candidate list a user has ever seen highlights one entry, so this is a
  gap in the ordinary case rather than an exotic one. Under anthy it is
  observable and unreachable at the same time: Space and Down move that cursor
  and the preedit changes to match, and a client that wants to highlight the
  matching row is left string-comparing the preedit against each candidate. The
  demo does not try, and its candidate list is the one panel that looks less
  capable than the engine behind it.

  A `size_t candidate_cursor` in `pathime_composition_t`, with a documented
  meaning when `candidate_count` is 0, is the whole fix. The question the design
  round should settle first is whether "hovering" is a concept this model wants
  to admit at all, given that it has no key bindings and no selection UI — but
  the internal cursor means the model already has it, and only the projection
  is missing.

  (That last paragraph called the field right and the scope wrong: the round
  admitted hovering *and* gave the client a way to drive it, because a
  projection with no operation behind it would have exposed a cursor only the
  engine could move.)

- ~~**Whether one dispatch may contain more than one `delete_surrounding_text`
  is unstated.**~~ **Closed by the header (2026-07-28.)** It now guarantees at
  most one per dispatch, with the reason a client cares. `App::flush_deletes()`
  and its `PendingDelete` queue are gone; `on_delete()` erases where it is told.

  The original finding: It matters because every deletion is expressed against the *same*
  snapshot: applying the first would move the text the second is described in
  terms of, so a client that assumes several must collect them and apply them
  back to front, and a client that assumes one can just delete. The header fixes
  the *ordering* — all deletions before any commit — which reads as though
  several are possible.

  The implementation permits exactly one: `Output` holds a single
  `has_deletion` / `delete_offset` / `delete_count` triple
  (`src/composition.h:180-182`), so a second request in one dispatch would
  overwrite the first rather than be dispatched. So the guarantee is already
  there and simply is not written down. The demo, reading only the header, wrote
  the queue (`App::flush_deletes()`) — about fifteen lines of machinery for a
  case that cannot arise. Saying "at most one per dispatch" in
  `delete_surrounding_text`'s documentation deletes that code from every client
  that ever reads it.

  Until the header says it, the demo keeps the queue: a client is entitled to
  program against the contract rather than against what the implementation
  happens to do today. Do not "simplify" it away without changing the header
  first.

- ~~**The inventory walk cannot produce a readable interface.**~~ **Done
  (2026-07-28.)** `pathime_option_value_name(option, value)` — the enumerator for
  an ENUM, a single bit for a FLAGS — returning a stable machine-readable name
  like `"kuten"`, `"double-mspy"`, `"gn-ng"`. A side table in `src/options.cc`
  rather than a tenth `OptionDescriptor` field, because only 15 of the 32
  options have values to name.

  No separate enumeration call was needed: `valid_values` already gives the
  legal set, so a client walks that bitmask and names each bit, and the header
  carries that loop as its example. `core.options`'s `test_value_names()` drives
  exactly that loop and demands a name at every stop, so a value added without
  one fails the build's tests.

  The demo deleted its thirteen hardcoded label sets, and its FLAGS editing is
  no longer all-bits-or-none: Left/Right walk the bits by name and Space toggles
  the one under the cursor.

  The original finding: The header
  advertises `pathime_option_count()` + `pathime_option_name()` +
  `pathime_engine_option_info()` as what lets a client "build a settings
  interface that follows the inventory rather than hardcoding it", and it does
  — a client learns the type, the legal values, the bounds, the default. What it
  cannot learn is what any of it is *called*. `pathime_option_name()` gives a
  machine-readable key for the option; there is no equivalent for an enum
  *value*, and no names at all for the bits of a FLAGS option.

  So the generic client is generic right up to the point where it has to print
  something. The demo carries its own table of thirteen enum label sets
  (`demo/src/options_view.cc`) and falls back to "value 3" for anything it does
  not know, which is exactly the hardcoding the inventory was meant to avoid;
  and its FLAGS editing degrades to all-bits-or-none, because offering twenty
  unnamed `pinyin-fuzzy` bits individually would teach a user nothing.

  This is at least partly *deliberate* — display text is presentation, and
  presentation is the client's, which is why there is no localization surface
  here and should not be. But `pathime_option_name()` already draws the line in
  the right place: a stable machine-readable key is not display text, it is
  something a client maps to its own strings. The symmetric extension is a
  `pathime_option_value_name(option, value)` returning `"kuten"`, `"full-width"`,
  `"double-mspy"`, and a name per FLAGS bit. Additive, static, and it turns the
  inventory walk from "types and numbers" into something a client can render
  before it has ever heard of the option.

- ~~**An option setter invalidates the borrowed composition, and does not read
  like it does.**~~ **Closed by the header (2026-07-28.)** The Ownership section
  now names all six setters and both reset forms as mutating calls, and spells
  out the trap: the `pathime_composition_t` keeps its address, so a stale
  pointer still reads a live struct and the mistake hides, but a `pathime_str_t`
  copied out of it before the set points into storage that has been reassigned.

  The original finding: The lifetime rule is "valid until the next call that mutates
  the same input context", and an option set is such a call — it can reset the
  composition, and it re-materializes the candidate list even when it does not.
  A client naturally reads "mutates" as `process_key` and `select_candidate`.

  The demo's paging path is where it shows: it reads `candidate_count`, calls
  `grow_candidate_list()`, and re-reads. That one is forgiving — the struct is
  the context's own and keeps its address, so a stale *pointer* still reads the
  new count. What is not forgiving is a held `pathime_str_t`: the slices point
  into `ctx->preedit` and `ctx->auxiliary`, which `refresh_composition()`
  reassigns, so a client that copied `composition->preedit` by value across an
  option set and then read `.bytes` has a dangling pointer. Worth naming the
  option setters explicitly where the lifetime rule is stated, rather than
  leaving them to be deduced from the word "mutates".

Three smaller ones, recorded so they are not rediscovered:

- ~~**There is no `pathime_context_is_focused()`.**~~ **Done (2026-07-28.)**
  Alongside `pathime_context_engine` and `pathime_context_user_data`, and for
  the same reason those exist: a language binding handed a bare context handle
  is in exactly the position of not knowing. No error channel — false for NULL
  and false before `pathime_init()`, both of which read the same way.
- **A pasted string has nowhere to go.** `pathime_context_process_key()` takes
  one key press, so text arriving other than by typing cannot be offered to an
  engine at all. The demo treats a paste as an ordinary client-side insertion
  and leaves the composition alone, which is almost certainly the right answer
  for a phone-keyboard model — recorded as considered rather than as a gap.
- **The demo is the one client in this tree that has to guess `layout_key`.** A
  terminal reports a decoded character and no key position, so the demo derives
  the position from the character on a US-QWERTY assumption; on any other
  physical layout Hangul and `anthy-typing-method = kana` will produce the wrong
  jamo or kana. That is a property of terminals, not of the API — a client on a
  windowing system has the scancode — but it is worth knowing that the field the
  header calls optional is the field the demo cannot supply honestly.

**What held up, since a list of holes is not a verdict.** Four things made the
client easier to write than expected, and each was a decision that could have
gone the other way. The rejection/failure split turned all error handling into
two lines — retry or reset — with no per-code knowledge. Eager candidate
materialization made `pathime_context_candidate()` a plain array read inside
`composition_changed`, which is what lets the demo render a candidate list from
the callback at all. Focus preserving composition state is what makes switching
engines mid-word a demonstration rather than a bug. And the fixed dispatch
order — deletions, then commits, then `composition_changed` last — is why the
document is never briefly wrong on screen, and is directly visible in the
demo's event log.

---

## 4c. The preedit rule, and the end of auxiliary text (2026-07-28)

This round started as "is `PATHIME_OPT_PREDICTION` implemented?" and ended
somewhere else entirely. The research that redirected it is
`docs/japanese-input-model.md`, which is measurement rather than argument and
should be read before anything here is reopened.

**What was wrong.** anthy and pyzy were mirror images. anthy's preedit showed
the reading (`にほn`) with an empty auxiliary; pyzy's showed a conversion the
user had not asked for (`你好`) with the reading displaced into the auxiliary.
The same two facts in opposite fields, so no client could render both with one
path. Bopomofo was the worst of it: the zhuyin the user was *literally typing*
was reachable only through the auxiliary text while the preedit showed Chinese.

**The rule, now in `pathime_composition_t::preedit` and `docs/CONCEPTS.md`:**

> The preedit is what the user has settled, followed by what they have typed
> and not yet settled, rendered in the script they are composing in. No engine
> rewrites it with a conversion the user has not chosen.

The guarantee that falls out — *the preedit is what a commit would produce* —
is what deleted the header's old warning that Return may commit something other
than what was shown. That warning existed to describe pyzy's preview, and the
preview is gone.

**What changed in the adapter.** `model->active` is pyzy's `auxiliaryText()`,
not its `conversionText()`; `conversionText()` is now read nowhere.
`auxiliaryText()` renders from `m_phrase_editor.cursor()` — the input not yet
consumed by a selection — so it shrinks as spans settle, and
`selectedText() + auxiliaryText()` is exactly anthy's shape. Two things had to
be handled that reasoning had not predicted, both found by tests rather than by
reading, and both now pinned:

- `commit(TYPE_CONVERTED)` disagrees with the preedit under **double pinyin**:
  it commits `nihk`, the raw keystrokes, where the preedit reads `ni hao`. So
  Return derives its text from the published preedit (`commit_preedit()`)
  rather than delegating, and the guarantee is structural instead of
  coincidental.
- pyzy suppresses its auxiliary text entirely when it has no candidate
  (`PinyinContext.cc:163-167`), leaving the input in `restText()`. Bopomofo
  reaches this on the first key of most syllables — `,` is ㄝ — so without the
  fallback a user typed a zhuyin symbol and saw an empty preedit.

**Auxiliary text is gone from the API.** `pathime_composition_t::auxiliary`,
the `Composition::auxiliary` member, the projection argument, the demo panel,
and `docs/CONCEPTS.md`'s whole section. All four engines were checked first:
hangul never had one; anthy's was always empty, and ibus-anthy's only content is
`( 3 / 12 )`, which this API publishes as `candidate_cursor` and
`candidate_count`; pyzy's turned out to be the preedit under another name; and
the table engine's is `get_aux_strings()` — the key run, which
`docs/ibus-table-mapping.md` §6.2 *already* specified as preedit text, plus the
same counter. Nothing was left in the field.

`PATHIME_OPT_PINYIN_SHOW_RAW` went with it, since the auxiliary text was the
only place it wrote. Option ids 23–31 shifted down by one; nothing has shipped.

**The one thing that would bring it back**, so the decision reads as reversible
rather than final: a table warranting a genuine composition-level hint with
nowhere else to go. `pathime_composition_t` carries `struct_size` and the header
already forbids reading past it, so the field returns as a *trailing* member
without breaking a compiled client. Do not re-add it in the middle.

**A consequence worth not rediscovering: the candidate cursor previews on anthy
and not on pyzy.** That looks like an inconsistency and is the rule working. On
anthy the user pressed Space, so the cursor is choosing *among conversions they
asked for* and the preedit follows. On pyzy the candidates arrived unbidden, so
moving the cursor is browsing and settles nothing. Both keep the invariant a
client depends on. Stated in the header at `candidate_cursor` and in
`docs/CONCEPTS.md`'s *Candidate cursor*.

**`PATHIME_OPT_PREDICTION` — decided and built (2026-07-28).** The option is
the **eager conversion strip**, not anthy's prediction API.
`docs/japanese-input-model.md` §4 and §5 are why. anthy's prediction is
*history completion* — `anthy_traverse_record_for_prediction`, empty on a fresh
profile and empty whenever `PATHIME_OPT_LEARNING` is off — whereas ordinary
conversion run eagerly over each growing prefix gives real candidates from the
first keystroke at 130 µs–1.7 ms per key, with no history at all.

Keeping the name is deliberate: 予測入力 is what Japanese IMEs call exactly this
strip, and MS-IME and Google Japanese Input merge history completions into it
rather than separating them. So the option's documentation changed and its name
did not, and anthy's real predictions can be merged in later without another
rename.

**What made this buildable is §4c.** It was blocked before, because
`candidate_cursor` was documented as "the entry whose text the preedit is
showing", so publishing candidates without previewing broke the invariant. That
invariant is gone: the cursor now previews only where the user asked for
conversion, and pyzy demonstrates candidates-with-no-preview in shipping code.
The strip is the same shape — candidates from the first keystroke, preedit left
as kana, cursor browsing without rewriting anything.

**The rulings, all made by the API owner (2026-07-28):**

- **Optional.** The tempting criterion — optional only if a user can type
  significant Japanese without meeting candidates — answers the wrong
  question, because no option controls that: real Japanese needs kanji, kanji
  needs conversion, and Space produces the list regardless. What the option
  chooses between is two shipping paradigms — desktop convert-on-request
  (every desktop Japanese IME) and the phone strip (Gboard, iOS). Nor does
  pyzy's always-on bind anthy: it is structural rather than chosen — pyzy
  converts unbidden and its Return commits the raw Latin, so candidates are
  the only route to Chinese text — whereas anthy is fully usable without
  eager candidates.
- **Default true** (`src/options.cc`). Cross-engine uniformity — candidates
  as you type wherever candidates exist — plus the phone-keyboard tiebreaker.
  The desktop paradigm is one `pathime_context_set_option_bool()` away, and
  the `open_classic_context()` tests in `api.engine_anthy` hold it still.
- **Space adopts the browsed cursor**: conversion begins previewing the
  hovered entry, not candidate 0. A moved highlight is the user's most recent
  expression of interest — and on gamepad-style modalities moving it is
  deliberate work, so discarding it would confuse exactly there. With an
  untouched cursor the two rules are indistinguishable, which is why the
  desktop habit is unaffected. pyzy's Space already selected the hover.
  Stated in the header at `PATHIME_KEY_SPACE` and `candidate_cursor`, and in
  `docs/CONCEPTS.md` *Candidate cursor*, which now carries the per-moment
  browse/preview rule in place of the per-engine wording.
- **One option covers table too**, reworded rather than split: it toggles
  "offerings the user did not ask to convert" — pre-conversion on anthy,
  post-commit suggestion mode on table (`docs/ibus-table-mapping.md` §11.3) —
  and a phone strip consumes both, which is why shipping IMEs give them one
  name. Table's as-you-type candidates are structural (spec §7.2) and are not
  what the option governs there. The user's settings flexibility was part of
  the ruling: a client unhappy with the pairing can set it per context.
- **The §8 active-span gap is accepted, no new field.** pyzy has shipped the
  shape all along, and one pyzy list mixes candidates covering different
  spans (你 vs 你好), so a composition-level span could not even be honest.
  Additive later as a trailing `pathime_composition_t` member if a real
  client needs one.

**What building it settled beyond the rulings** — the strip has no reference
implementation (ibus-anthy has no such mode), so pyzy's unbidden list was the
model throughout:

- **A strip selection settles greedily and typing continues.** The chosen
  text stays preedit — pyzy's partial `selectCandidate()` shape, not a
  commit — the composer is re-seeded with the readings of the unconsumed
  segments (`RomajiComposer::assign_kana()`), and the conversion re-runs on
  the remainder; when nothing remains the composition commits whole. This is
  the case the converting machinery cannot express, because a printable key
  mid-conversion commits everything, and it is why the split of the old
  `converting_` flag was the bulk of the work: `converting_` now means only
  "the user asked", and the eager state — what feeds a client's strip — is
  `segment_count_ > 0 && !converting_`.
- **Un-settling exists.** Backspace deletes remainder kana first, then walks
  the most recent selection back to the reading it consumed; Escape
  un-settles them all at once, and a second press discards the buffer — the
  same stairs the converting state's cancel descends.
- **Learning re-stages each strip choice alone** (`learn_eager_choice()`):
  convert just the consumed reading, commit the matching candidate as the
  only — and therefore last — segment, which is what flushes anthy's record.
  The in-context alternative cannot work: the flush needs every segment
  committed before the remainder re-runs, so each partial selection's lesson
  would be lost, and committing the leftovers at `NTH_UNCONVERTED_CANDIDATE`
  to force it would teach anthy the user chose plain readings for text they
  were still typing — the §3 poison, measured and durable. Cost stated
  plainly: the solo commit loses anthy's view of the surrounding segments.
- Eager conversion feeds `anthy_set_string()` from `composer_.reading()` —
  the same resolved form the convert key always used — so the strip may show
  candidates for にほん while the preedit reads にほn: the split Return has
  always had, now visible before any key is finished.
- `options_changed()` rebuilds only when the strip's *presence* is wrong
  (`PATHIME_OPT_PREDICTION` toggled, effectively), so raising
  `PATHIME_OPT_MAX_CANDIDATES` mid-browse appends to the strip without
  resetting the hover — the append-only promise a converted list keeps.

Two costs measured and accepted stand: `anthy_set_string()` per keystroke,
and the active span churning on long input because anthy re-splits (§5's
きょうは → 今日は, きょうはい → 今日). Fine for phrase-at-a-time phone
typing, ugly for a whole sentence — which is the desktop paradigm's case, and
it turns the option off.

**Where it is pinned.** `test_prediction_strip()` in `api.engine_anthy`: the
preedit stays kana under the strip, browsing moves no text, Space adopts the
hover and then advances, any key regenerates the list, Return commits the
kana and never candidate 0, the toggle works mid-composition in both
directions, a cap raise keeps the hover, full and partial strip selections,
un-settling by both routes, and learning on and off. The off state is held
still by the `open_classic_context()` tests. Three guards were
mutation-tested — browse-no-preview, cursor adoption, strip learning — and
each mutation fails the suite.

**Two traps from §4c that did recur here**, kept because they will recur
again:

- `pathime_context_composition()` is invalidated by option setters, not just by
  key events. A test that reads the struct, sets the option, and re-reads
  without re-fetching will look right and be wrong.
- Candidate order is history-dependent (§3 of the model doc), and that holds
  *within* a run, not just across runs: `api.engine_anthy.clean` wipes the
  record per run, but a test that learns mid-suite reorders every later list.
  `test_prediction_strip()` captures candidate text at runtime instead of
  naming it, and puts the record back (re-selecting 漢字) before handing the
  suite on.

---

## 5. Loose ends

- **Two pyzy bugs, found by comparing the engines side by side (2026-07-28).**
  Both are ours, not pyzy's, and both are places the Chinese adapter is
  gratuitously unlike the Japanese one. Neither is a design question; the
  evidence is in `docs/japanese-input-model.md` §6.

  1. ~~**Space with nothing composing is handled by pyzy and unhandled by
     anthy.**~~ **Fixed (2026-07-28.)** Anthy now inserts a space at
     `PATHIME_OPT_LATIN_WIDTH`, which is what pyzy already did and what the
     header has always said that option governs ("Latin letters, digits and
     space"). The old adapter comment declined the key so the client could
     insert its own "rather than absorbed into a full-width 　 nobody asked
     for" — wrong twice, since the width was never forced (the option defaults
     to half and is per-context) and the disagreement meant no client could
     bind Space consistently.

     Two things deliberately not matched. ibus-anthy defaults to a **wide**
     space (`half-width-space` defaults false); we default half for every
     engine, because one default across engines beats matching each reference
     separately. And **hangul still declines**: it implements no width option
     (`kConverting` excludes it) so it has nothing to add that the client's own
     space does not, and ibus-hangul leaves plain Space alone too — only
     Shift+Space is bound there, as a mode hotkey this API excludes
     (`refs/ibus-hangul/src/engine.c:423`). The visible result is a space
     either way; only the `handled` flag differs.

     Worth knowing, since it was checked while deciding: `PATHIME_OPT_LATIN_WIDTH`
     is load-bearing beyond space. Measured, it governs digits on both engines
     and uppercase letters on pyzy — `"A1 "` at full width gives `"Ａ１　"` on
     Pinyin. So a client setting it to FULL for the space also gets `２０２４`
     for `2024`. That coupling is pre-existing and documented, not introduced
     here, but it is the reason ibus-anthy keeps a separate `half-width-space`.

  2. ~~**The `|` in pyzy's auxiliary text is dead weight.**~~ **Fixed
     (2026-07-28)**, as part of §4c. `strip_input_cursor()` in
     `pyzy_backend.cc`. It was pyzy's own input cursor, and this library never
     sends a cursor movement — Left/Right are declined while composing
     (`TODO.md`, the in-span cursor question) — so `cursor()` equalled the input length at every step of every
     run measured and the marker was always trailing.

- **pyzy's user-database save is handled — do not go looking for a save call.**
  This used to read as an open obligation: `Database` schedules its save with
  `g_timeout_add_seconds` (`engines/pyzy/src/Database.h:98-101`) and the timeout never
  fires, because nothing runs a `GMainLoop`. But the timeout id is still set,
  which is exactly what `~Database` tests before saving, so
  `Database::finalize()` at our global-shutdown does the save. Recorded because
  the next reader will re-derive the first half and conclude, wrongly, that
  learning is being dropped. `src/engines/pyzy/pyzy_backend.cc:816`.

- **The JIS ¥-vs-ろ distinction cannot be expressed.** `layout_key` is a
  US-QWERTY keysym and neither key exists on that layout, so anthy's one
  keycode-dependent case is lost. Accepted: it is kana-hardware-only and
  irrelevant to the phone-keyboard target.

  Kana entry landed since and it costs exactly one table row there too —
  `'¥' -> ー` is the single entry of `kana_typing_rule_static` that could not
  be transcribed. It costs nothing in practice: ー is still typeable from the
  US backslash position, which the same table maps to it.
- **Thread-safety is a documented requirement, not an enforced one.** Nothing
  detects overlapping calls. If that proves to be a common client bug, a debug
  build could catch it cheaply with a non-recursive in-call flag per context.

- **A backend's global init failing is per-engine, not fatal.** A hook that
  returns false marks that backend unavailable and `pathime_init()` still
  succeeds, because the header already has the right channel for it:
  `pathime_has_engine()` is documented false for an engine "whose runtime
  prerequisites, such as its dictionaries, are unavailable". `init.cc` records
  per-backend readiness and `engine_available()` consults it.
  `PATHIME_ERROR_BACKEND` from `pathime_init()` would mean the library itself is
  unusable, which is a different and much rarer claim — and one missing
  dictionary must not cost a client the engines that would have worked.

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

- ~~**`src/candidates.h` still does not exist, and the trigger has now
  fired.**~~ **Done (2026-07-28.)** The cursor work was the next change to touch
  the pump, so `src/candidates.h` landed with it and `context.cc` includes it
  instead of forward-declaring `materialize_candidates()` itself.

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

- ~~**Build a demo app based on CPP-Terminal**~~ **Done (2026-07-27.)**
  `demo/`, behind `LIBPATHIME_BUILD_DEMO` (OFF by default), with cpp-terminal as
  a submodule beneath it. Everything asked for is there — one text field, the
  preedit drawn into it with the settled boundary visible, auxiliary text,
  candidate selection and paging, engine selection, and every option the active
  engine implements editable at either level — plus a log of every callback the
  library makes, which turned out to be the panel worth watching. `demo/README.md`
  is what it shows and what to try in it.

  Three things it taught, none of them a library problem:

  **Digit keys cannot mean "pick a candidate" under Bopomofo.** 3, 4, 6 and 7
  are the tone keys there, and binding them to selection makes the engine's own
  layout untypable — `su3cl3` silently became a series of candidate picks. So
  the demo binds Alt+digit as well, and plain digits everywhere except
  Bopomofo. Which keys select is entirely the client's to decide, which is the
  point, but it is the first place that freedom has actually cost something.

  **A terminal cannot report a key's position, and two engines want it.**
  `layout_key` is derived from the character on a US-QWERTY assumption; on any
  other physical layout Hangul and `anthy-typing-method = kana` will produce the
  wrong jamo or kana. Not fixable from a terminal — a client on a windowing
  system reports the true key — but worth knowing that the demo is the one
  client in this tree that has to guess.

  **Raising `max-candidates` to page further needs a stop condition.** A list
  shorter than the cap was not truncated by it, so there is nothing more to be
  had; without that test the demo raised the option every time the user reached
  the end of a complete list. `App::grow_candidate_list()`.

  What it found about the *API* — as opposed to about terminals and key
  bindings — was §4b, and that round has since been held: all six are closed,
  and the demo was rewritten against the result. It now binds Up/Down to
  `pathime_context_set_candidate_cursor()` rather than forwarding them,
  highlights the hovered entry, keeps the page and the highlight together, and
  names every enum value and flags bit from `pathime_option_value_name()`

---

## 6. The table engine (2026-07-27 / 2026-07-28)

The fourth engine, and the only one written here rather than wrapped. `ibus-table`
is Python, so there was nothing to link against: it supplied a proven feature set,
`docs/ibus-table-mapping.md` was derived from it clean-room, and the implementation
answers to that spec. What follows is the decisions taken along the way, in two
groups — the ones about *shape*, taken while the engine was being built, and the
ones about *behaviour*, taken in a round of questions the finished engine raised.

### 6a. Shape

**One directory under `src/`, not a library under `engines/`.** The alternative
considered was a standalone `engines/table/` with its own clean surface, wrapped
by an adapter in `src/engines/table/` like the other three. Rejected: that
surface would have needed its own key-event type, its own composition
representation and its own option carrier, each translated by an adapter that did
nothing else — a second IME API invented purely so the first one could have
something to wrap. The three real adapters exist because upstream imposed a shape
on us; here nothing would have imposed it but the directory choice. `engines/`
also means something specific in this tree — vendored, never edited — and this
code is ours.

What replaces the library boundary is a *header* boundary, and it is load-bearing
rather than decorative: **`table_backend.cc` is the only file in the directory
that includes `backend.h`.** Nothing else names a `Composition`, a `KeyEvent`, an
`Output` or an `OptionReader`. That is what lets `tools/table-compile` build the
parser and the database writer without pulling in the engine, and what lets
`tests/core/table_test.cc` reach the awkward cases directly. Both link short,
explicit source lists, so a link error in either is the first sign something has
reached back across the seam. `src/engines/table/README.md` is the map.

**Tier 3 lives behind the seam, not in the option store.** A table declares the
behaviour its author chose — `AUTO_COMMIT`, the wildcards, `LANGUAGE_FILTER` —
and those values apply wherever the client expressed no preference. The value
belongs to a data file only the backend can read, so `options.cc` consults
`EngineBackend::declared_number()` / `declared_text()` between the engine store
and the descriptor default, and the table's declaration never enters the store.
Two methods rather than one because the option kinds are two, and inventing a
variant for a seam with one implementor would cost more than the duplication.

**Tables are named, not pathed.** `PATHIME_OPT_TABLE_FILE` takes a bare name
resolved inside the resource directory. A client picking a table should not have
to know where the data landed.

**Compiled at build time, by a tool built from the engine's own sources.**
`ibus-table-chinese` distributes source `.txt` only; upstream compiles it with
`ibus-table-createdb`, which is part of ibus-table and therefore Python, through a
pipeline of sed, iconv and awk. `tools/table-compile` replaces all of that with a
C++ program sharing the data layer, which is what makes the table data build
identically on Windows.

**Enumeration reuses the option machinery rather than adding entry points.** The
installed tables are the legal values of `PATHIME_OPT_TABLE_FILE`:
`pathime_option_info_t::valid_value_count` says how many and
`pathime_option_value_name()` names each by index, exactly as for an enum. No
display names, icons or language lists — what comes back is the machine-readable
key the setter accepts, because presentation is the client's domain. Measured
first: opening all five tables and reading `ime` costs 1.1 ms, which killed the
build-time-manifest alternative.

### 6b. Behaviour

Six questions the finished engine raised, answered together.

**Char prompts stay in the preedit; the header's clause is what gave.** Spec §7.4
makes the essential operation "commit the literal input", so Cangjie `a`+`b`
commits `ab` while the preedit reads 日月 — against the header's promise that the
preedit "is the text that would be committed if the composition ended right now".
Resolved in favour of the display, because `BEGIN_CHAR_PROMPTS` is the *keycap
legend*: cangjie5 maps `a`→日, `b`→月 … `y`→卜, which is what is printed on a
Cangjie keyboard. The preedit is the physical keyboard rendered back at the user,
and that is the feature. Four of the five shipped tables carry prompts, so this is
the normal case, not an edge. Return dropping to the QWERTY letters is a
deliberate quirk and a useful one — quick access to Latin text out of a composing
state. The clause in `pathime_composition_t::preedit` and `docs/CONCEPTS.md` now
names key legends as a second permitted departure alongside §4c's commit-time
normalizations. **Cost:** a client reading the preedit as literal committable text
is wrong for four tables, and has to know it.

**The table loads when `PATHIME_OPT_TABLE_FILE` is set.** Previously
`declared_number()` read the *loaded* table and would not load one, so the same
query returned the descriptor default before the first keystroke and the table's
declaration after it. Now `EngineBackend::prepare_string()` runs before the store
and a name that does not resolve is `PATHIME_ERROR_BACKEND`. The deciding reason
was not predictability but attribution: the setter is the last point at which the
failure can still be blamed on the name that caused it. **Cost:** one setter in
this API does file I/O and can fail.

**Pinyin mode (§11.2) and suggestion mode (§11.3) are not implemented, and the
options stay.** Three findings closed this. The data ships with **ibus-table**,
not ibus-table-chinese, so taking it means a third GPL-3 dependency while the
licensing question is open. It was never reachable anyway: ibus-table binds
`toggle_pinyin_mode` to `Shift_R`, a bare modifier press this library's key model
cannot express. And the audience is thinner than it looks — Cangjie and Quick are
the Hong Kong methods, where the user speaks Cantonese and Mandarin pinyin is
close to useless to them. What *was* wrong was a lie rather than a gap: tier 3
reported the option on for four tables whose compiled `pinyin` table is empty.
`TableProperties::pinyin_data` now records whether the rows exist.

This also resolved the wider question of whether to reimplement input methods
pyzy already covers. §11.2 turns out not to be a second pinyin at all — it is a
lookup escape hatch *within* a table method, for finding a character whose table
code you do not know. So the duplication worry mostly dissolved; what remains is
the narrower ruling that **this library will not ship a table that is itself a
pinyin table**, which would be a genuine second pinyin.

**Full-width conversion (§11.4) is shared with pyzy, not transcribed from the
spec.** The two references disagree on four characters — `^`, `[`, `<`, and the
period, where ibus-table switches on sentence position while ibus-pinyin keeps
"1.5" intact. Following each per engine would make
`PATHIME_OPT_PUNCTUATION_WIDTH` mean two things depending on which Chinese engine
a client picked, against an API that presents one behaviour per concept. So
`src/engines/pyzy/punctuation.*` was hoisted to `src/punctuation.*` — no
`engines/common/`, because shared code does not get a subdirectory — and the
variant mapping widened from pyzy's two values to all five. Which table applies
comes from `PATHIME_OPT_CHINESE_VARIANT`, which for a table resolves through tier
3 to its own `LANGUAGE_FILTER`, so cangjie5 (`cm1`) punctuates the traditional way
and wubi-jidian86 (`cm2`) the simplified way with no client involvement.
**Cost:** ibus-table parity on those four characters.

One consequence had to be swallowed rather than chosen: a CJK table now claims
*every* printable ASCII key, including ones the conversion leaves unchanged. The
first attempt handled only keys the conversion changed, which is tidier — but the
documented default is full-width punctuation with half-width Latin, so a digit
converts to itself, and an engine that declined it would never see the `1` in
"1.5", never disarm the decimal-point rule, and would commit "1。5". A key the
client inserts itself is one the engine never saw.

**Glyph filtering: build-time, from a checked-in map.** A table method is not
really a candidate-driven input method — Cangjie's advantage is that it is
deterministic — but candidates get shown anyway, and the stock ones for a partial
code are frequently obscure. Measured: filtering cangjie5 and quick5 to Noto Sans
CJK removes about half of each, which is the empirical form of "twice as many
characters as the most capable font". Frequency augmentation keeps useful
characters at the front; this keeps unrenderable ones out.

Deliberately *not* the fork's approach of shelling out to `fc-query` during
preprocessing: that makes a compiled `.db` a function of which fonts the build
machine happens to have, so two builds of the same commit ship different data.
The map is generated once into `coverage_data.h` and checked in — the same bargain
`variants_data.h` makes with Unicode data — with regeneration behind a CMake
option defaulting OFF. The map comes from a deliberately *inclusive* font because
it is the upper bound: the deferred runtime half can only narrow it, and narrowing
only works if nothing has to be added back to a table that no longer carries it.
`coverage.*` is the one pair in `engines/table/` the **library does not link**;
only the compile tool does.

**The `z` wildcard is derived per table, not defaulted once.** Apple's Cangjie
makes `Z` stand for a part of a decomposition the user cannot recall, which is a
real ergonomic win on a method whose difficulty is precisely recalling
decompositions. But `z` is not free everywhere: erbi, scj6, yong, easy-big, wu,
cantonhk, cangjie3 and cangjie-big all use it inside a code — and so does the
shipped **wubi-jidian86**, whose punctuation codes are spelled `zzbd`. A tier-4
default in `options.cc` would have broken them silently and kept breaking them as
tables were added. So `tools/table-compile` checks the rows being compiled and
declares `SINGLE_WILDCARD_CHAR` only where `z` never appears after the first key.
cangjie5, quick5 and zhuyin get it; wubi-jidian86 is declined by the check, which
is the check earning its place rather than a hypothetical.

The leading occurrences survive because of a second rule: a wildcard that is *also*
one of the table's own input characters is literal at position 0 and a wildcard
everywhere after (`TableProperties::is_wildcard_at()`). Nothing else can
distinguish the two readings, and the literal one is the one that loses data —
cangjie5's 496 `z`-prefixed rows are its punctuation codes, reachable no other
way, whereas a search beginning with a wildcard is a search for everything.

### 6c. What was ruled out of scope, and what compatibility actually means

**User-derived phrases (§10.2) are out of scope for the first phase.** The
deciding argument is reach: only wubi-jidian86 can use the feature at all, and
that is the table this library has least reason to lead with. The investigation
is preserved in `TODO.md` because it found something worth not rediscovering —
the feature depends on **auxiliary text**, which §4c removed on purpose.
ibus-table shows the code it would derive as `#: <code>` while the phrase sits in
the preedit; without that channel the feature files vocabulary under codes the
user is never told. The same reach test now applies by default to anything else
reachable only through wubi.

**"Compatible with ibus-table" is true of the format and not of the tables.** The
schemas are identical and the user database interoperates, so reading a
distro-installed `.db` should work — untested, but a code reading against
`tabsqlitedb.py`. The reverse is where the claim has to be qualified: a table this
library compiles is *not* what `ibus-table-createdb` would produce from the same
source. Glyph filtering removes rows, frequency transfer rewrites frequencies, and
the derived `z` wildcard is a behaviour change rather than different data —
ibus-table's lookup does a plain `str.replace` with no position rule, so under
ibus-table a leading `z` would become a wildcard too and cangjie5's punctuation
codes would be unreachable there. The honest description is **"we read and write
the format, and we use their sources to make our own sauce"**. `TODO.md` carries
the detail and the cheap fix, should strict compatibility ever be wanted.
