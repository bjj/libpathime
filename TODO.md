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
- **The first client.** `demo/`, an interactive IME in a terminal, behind
  `LIBPATHIME_BUILD_DEMO`. It exists to be *used*, not to verify anything — but
  being the first program written against the header rather than to test it
  found six places where a client has to work around the API's shape. Those are
  §4b, and they are the best input the next design round has.
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
  `src/engines/pyzy/punctuation.*`, transcribed from ibus-pinyin's
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

- `src/init.cc` rejects a non-NULL but *empty* `data_dir` as
  `INVALID_ARGUMENT`. The behaviour is right — NULL and `""` must not both mean
  "use the default" — but the header documents only what NULL does.
- The descriptor reports `max-candidates`'s maximum as `INT64_MAX`. The header
  states a minimum of 1 and no maximum, leaving the representation open.
- ~~`Return` on pyzy commits the **raw** input.~~ **Done (2026-07-27.)** The
  behaviour is unchanged and matches ibus-pinyin; what was missing was the rule
  that makes it read as chosen. `pathime_context_process_key()` now states the
  pair — Space asks for conversion, Return ends the composition without
  applying any conversion the user has not explicitly chosen — and then states
  the consequence outright: Return may commit text differing from the last
  preedit shown, because an engine may *preview* an unchosen conversion, and
  Pinyin and Bopomofo do.

## 4b. What the first client found

`demo/` is the first program written *against* this API rather than to test it,
and being a client is a different exercise from being a test: a test knows what
it is checking, so it can reach for whatever the header offers and assert on the
answer, while a client has to build a whole interface out of what is there and
discovers the shape of the holes. These are the holes. None of them blocked the
demo and none is a bug; each cost it either a workaround in `demo/src/` or a
feature it does not have. They are the places where the next design round should
ask whether the client should have had to do that.

Roughly in the order of how much a client suffers without them.

- **A client cannot ask whether a candidate list is complete.** It must infer
  it: a list shorter than `PATHIME_OPT_MAX_CANDIDATES` was not truncated by the
  cap, so there is nothing more to be had; a list exactly as long as the cap
  might have more behind it. The header supports the inference — it says a lazy
  backend materializes up to the cap and stops — but it is an inference, and
  *every* client that pages a list has to rederive it. Without it the demo
  raised the cap each time the user reached the end of a list that was already
  complete, leaving the option permanently changed for nothing
  (`App::grow_candidate_list()`).

  Two fixes, and they are not equivalent. Stating the rule in the header beside
  `candidate_count` costs nothing and closes the question. A `bool
  candidates_complete` in `pathime_composition_t` costs a struct field and makes
  it unmissable — and it would also let the library answer honestly for a
  backend that *knows* it has more, which the length test cannot.

- **`pathime_engine_requirements()` has no context-level counterpart, and the
  header's advice in its place is the one thing a generic client cannot
  follow.** Requirements depend on the resolved configuration, and
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

- **The currently-shown candidate exists internally and is not exposed.**
  `Composition::cursor` (`src/composition.h:117`) is per active span, is reset
  when a span settles, and is fed to the backend on selection — the core tracks
  it because neither anthy nor pyzy durably records it (§2, Finding 2). The
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

- **Whether one dispatch may contain more than one `delete_surrounding_text` is
  unstated.** It matters because every deletion is expressed against the *same*
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

- **The inventory walk cannot produce a readable interface.** The header
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

- **An option setter invalidates the borrowed composition, and does not read
  like it does.** The lifetime rule is "valid until the next call that mutates
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

- **There is no `pathime_context_is_focused()`.** The demo does not need one —
  it focused the context itself — but the two accessors that do exist
  (`pathime_context_engine`, `pathime_context_user_data`) exist "for language
  bindings", and a binding handed a bare context handle is in exactly the
  position of not knowing.
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
  bindings — is §4b, and that is the part worth acting on.