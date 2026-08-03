# libpathime — design history

Settled decisions only, kept terse so they are not reopened or re-derived.
Development-only leaf: it may cite anything; nothing outside `TODO.md` and
`CLAUDE.md` may cite it. Substance a reader of the code needs lives in the code
or the permanent doc that owns it; what is here is the ruling, the reason, the
cost, and what would legitimately reopen it. The full narrative of every round
— evidence, file:line citations, dead ends — is in this file's git history
(versions before 2026-07-30).

Nothing here is pending — that is `TODO.md`. Model: `docs/CONCEPTS.md`.
Contract: `include/pathime/pathime.h`.

## Ledger (as of 2026-08-01)

Built and tested: the build (Linux, macOS and Windows, both link modes), all
45 public entry points, all four adapters — hangul, anthy, pyzy, and the
table engine, the last written here rather than wrapped — options and
negotiation including tier 3, the preedit rule, the eager candidate strip, and
the terminal demo. 40 suites pass on Linux and macOS with every backend
enabled, 39 on Windows (`hangul.vendored.unittest` needs the Check library).
`docs/testing.md` maps the suites; `docs/source-layout.md` maps the files.
Public at `github.com/bjj/libpathime` with CI on every push; v0.1.0 released
2026-08-01 — §14 holds what going public settled.

## 1. Options round

- **Input purpose and hints** (text/name/email/URL/password, single/multiline,
  assistance toggles): deferred past v1 (2026-07-27). No backend consumes them;
  additive when a consumer appears (most plausibly the table engine — URL field
  wanting Latin passthrough). Not a gap.
- **~¾ of the catalogued backend options were cut**: internal plumbing,
  presentation (the client's), things earlier rounds settled, and options dead
  in their own reference implementation. The surviving inventory is the header.
- **Deferred with reasons**: anthy auxiliary dictionaries (only reachable via
  the process-global write-once "personality" — the trap `data_dir` exists to
  avoid); romaji/kana table variants (upstream ships exactly one romaji table;
  the variants its comments name were never written); hangul jamo output;
  user-defined phrases (the API has no defining operation); candidate
  annotations (candidates are text-only; additive `candidate_info()` struct
  later, when something has an annotation to carry).
- **Cut**: hanja conversion entirely — hangul produces no candidates and
  `PATHIME_OPT_MAX_CANDIDATES` reports unsupported there. `PATHIME_KEY_HANGUL`
  and `PATHIME_KEY_HIRAGANA_KATAKANA` — mode hotkeys, the client's side; the
  header states the rule (a key that changes a mode rather than the composition
  belongs to the client). `HENKAN`/`MUHENKAN` stay: real composition
  operations.
- **F6–F10 per-composition character-type conversion** (ibus-anthy): a real
  composition operation, out of scope — no F-keys on the phone target, and it
  would serve one and a half engines. Katakana is partly reachable as a
  conversion candidate; half-width katakana, wide latin and latin are reachable
  by **no** route — accepted. Cheap additive operation if a consumer appears
  (`RomajiComposer` already splits `display()` from `commit_text()`).
- **Misreading not to repeat**: nothing resolves pending romaji (`にほn`) short
  of commit or convert. ibus-anthy's Tab/predict does a *pure read*
  (`get_hiragana()` never assigns back). Routes to にほん: second `n`, Return,
  or convert — same as ibus-anthy.
- **Surrounding text stays**; its justification is
  `PATHIME_HANGUL_PREEDIT_NONE`, the only setter of `PATHIME_REQUIRES_*` bits.
- **Fuzzy is Pinyin + Bopomofo; correction is Pinyin-only** — traced through
  pyzy's `bopomofo_table` (61 rows carry fuzzy bits, zero reach correction).
  Both keep the `PATHIME_OPT_PINYIN_` prefix: bopomofo reaches them by being
  parsed into pinyin. Derivation at `src/options.cc:229`.

## 2. Adapter findings

Permanent home is `docs/source-layout.md`; kept here as one-liners because they
shape everything:

1. The internal model is richer than the API projection — keep the structured
   form, compute the flat value at the boundary; the candidate list always
   describes the leftmost unsettled span.
2. We own the hovered candidate; no backend records it durably.
3. Two-layer lifetime (process-global init vs per-context handles). hangul has
   **no** global init at all — `hangul_init()` exists only under
   `ENABLE_EXTERNAL_KEYBOARDS`, which the build turns off — so it is the one
   backend that cannot fail at runtime.
4. Encoding differs per backend; returned strings are borrowed and volatile —
   copy immediately.
5. pyzy pushes via synchronous Observer callbacks *during* mutation; anthy and
   hangul are pull-only. Dirty flags plus a post-call assembly step reconcile
   them; no event loop.
6. We own the whole key layer; backends take finished input only.

Obligation from the API round: **candidates are materialized eagerly up to the
cap before `composition_changed`** — the header promises
`pathime_context_candidate()` is callback-safe, and pyzy's `hasCandidate()` is
lazy and mutating. Per-backend gotchas live in `docs/*-mapping.md`; consult,
don't re-derive.

## 3. Core shape

- `Composition` = three strings (settled / active / tail) + candidates for the
  active span + a cursor. Deliberately **no** segment array or active index
  (anthy keeps both privately). `preedit_settled` = length of settled.
- Key layer: engine-agnostic part in `src/keys.*`; the romaji machine in
  `src/engines/anthy/romaji.*` (holds no anthy types).
- Candidates are active-region-only, greedy left-to-right, no segment
  navigation. Text is UTF-8; all positions are Unicode scalar counts.
- The in-span cursor question is the one still open — `TODO.md`.

## 4a. First-slice leftovers (all done; the traps)

- **`PATHIME_HANGUL_PREEDIT_NONE`**: builds the syllable in the client's
  document by delete-and-recommit. `ContextBackend::process_key()` takes a
  `const SurroundingTextView &` because stale-snapshot recovery must be decided
  *before* libhangul folds the key in. Ending the composition does not
  re-commit.
- **`PATHIME_ANTHY_TYPING_KANA`**: the 101kana table over US key *positions*
  (`position_key()`), 94 of 95 rows — `'¥' → ー` is unrepresentable (§5) and
  harmless (ー is on `|`). Verified exhaustively against ibus-anthy's
  `tables.py`; redo if tables are edited.
- **`PATHIME_OPT_LEARNING` unsupported on pyzy**: pyzy learns inside
  `selectCandidate()`/`commit()` with no switch, and the option is per-context
  while pyzy's user db is process-global. Do not "fix" by redirecting the cache
  directory — it does not solve the second mismatch.
- **pyzy availability** = `stat` of `<resource_dir>/pyzy/main.db` (the same
  predicate pyzy uses). A conversion probe is NOT the answer: with no db,
  `m_db` is NULL and the probe dereferences it.
- **`ContextBackend::options_changed()`** exists because a mid-composition
  option change may need to reach derived backend state. Ask of any new option:
  does the backend hold state that outlives the call?
- **Width options** are `src/punctuation.*`, shared with the table engine. Two
  deliberate departures from ibus-pinyin: punctuation width governs *all*
  punctuation (no per-row fallback to Latin width), and a punctuation key
  mid-composition ends the composition (taking the hover) rather than being
  swallowed. Cost accepted: these engines report every printable key handled,
  including pass-throughs — the only way width options can apply at all.
- **Return on pyzy commits the raw input** — deliberate, matches ibus-pinyin.
  Header states the pair: Space asks for conversion; Return ends without
  applying any conversion the user has not chosen.
- Empty-but-non-NULL `data_dir`/`resource_dir` → `INVALID_ARGUMENT` (in
  header). Options with no documented maximum report `INT64_MAX`.

## 4b. What the first client found (all six closed, 2026-07-28)

New API (40 → 44 entry points, all additive):
`pathime_context_set_candidate_cursor()` plus a `candidate_cursor` composition
field — the engine may move the cursor (Space advances it, settling drops it,
pyzy resets it on list regeneration), so it *is* composition data, re-read on
every change, and the setter is a request; `pathime_context_requirements()` —
reports the effective (capped) value, while the engine form stays uncapped so
`pathime_context_create()` can reject against the true value;
`pathime_option_value_name()` — names enum values and FLAGS bits, side table in
`src/options.cc`; `pathime_context_is_focused()` (since removed — §7).

Closed by stating in the header:

- **Candidate-list completeness**: a count below the resolved cap is complete;
  a count equal to it is undecidable. A `candidates_complete` field was
  **declined** — do not reopen without a backend whose list length genuinely
  misleads. (The cap exists for pyzy, which fills lazily twelve at a time;
  anthy knows its total; hangul has none.)
- **At most one `delete_surrounding_text` per dispatch** — `Output` holds a
  single deletion triple.
- **Option setters invalidate the borrowed composition.** The struct keeps its
  address, so a stale pointer hides the bug; a copied `pathime_str_t` dangles.

Considered, not gaps: a pasted string has nowhere to go (correct for the phone
model); the demo must guess `layout_key` (a terminal limitation, not an API
one — Hangul and kana typing are wrong off US-QWERTY there). Anthy's Up/Down
bindings were removed with the cursor work: a key the engine reports handled
never reaches the client's binding.

## 4c. The preedit rule, auxiliary text, and the strip (2026-07-28)

- **The preedit rule** (header + CONCEPTS): what the user settled, then what
  they typed and have not settled, in the script they are composing in; no
  engine rewrites it with an unchosen conversion. Corollary: the preedit is
  what a commit would produce. `docs/japanese-input-model.md` is the
  measurement behind it — read it before reopening anything here.
- pyzy adapter: active text is `auxiliaryText()`; `conversionText()` is read
  nowhere. Return commits from the *published preedit* (`commit_preedit()`),
  because pyzy's `commit(TYPE_CONVERTED)` commits raw keystrokes under double
  pinyin. Bopomofo needs the `restText()` fallback (pyzy suppresses its aux
  with no candidate — the first key of most syllables).
- **Auxiliary text is gone from the API.** All four engines were checked;
  nothing was left in the field. Revisit only for a table needing a genuine
  composition-level hint — re-add as a *trailing* `struct_size`-guarded member,
  never in the middle. `PATHIME_OPT_PINYIN_SHOW_RAW` died with it.
- **The candidate cursor previews on anthy and not on pyzy** — the rule
  working, not an inconsistency: preview only where the user asked to convert.
- **`PATHIME_OPT_PREDICTION` is the eager conversion strip, NOT anthy's
  prediction API** (that is history completion — empty on fresh profiles and
  whenever learning is off; eager conversion gives real candidates from the
  first keystroke at 130 µs–1.7 ms/key). The name is kept deliberately
  (予測入力; real history completions can merge in later without a rename).
- Rulings: **optional** (it chooses between desktop convert-on-request and the
  phone strip; pyzy's always-on is structural and does not bind anthy);
  **default true**; **Space adopts the browsed cursor** then advances
  (indistinguishable from candidate 0 when untouched); **one option covers
  table's suggestion mode** (table's as-you-type candidates are structural, not
  governed by it); the **active-span gap is accepted** — one pyzy list mixes
  candidates covering different spans, so a composition-level span field could
  not be honest. Additive later if a real client needs it.
- Strip mechanics worth knowing: a strip selection settles greedily and stays
  preedit (composer re-seeded from unconsumed readings, conversion re-runs);
  Backspace/Escape un-settle; `converting_` means only "the user asked".
  Learning re-stages each strip choice solo (`learn_eager_choice()`) — the
  in-context alternative teaches anthy poison; cost: the solo commit loses
  anthy's view of surrounding segments. Accepted costs: `anthy_set_string()`
  per keystroke; span churn on long input (the desktop paradigm turns the
  option off).
- **Two recurring test traps**: the composition is invalidated by option
  setters, not just keys — re-fetch after set; candidate order is
  history-dependent *within* a run — capture candidate text at runtime, and put
  the learning record back before handing the suite on.

## 5. Loose ends

- **Space with nothing composing**: anthy and pyzy insert it at
  `PATHIME_OPT_LATIN_WIDTH` (handled); hangul declines — it implements no width
  option, matching ibus-hangul. `LATIN_WIDTH` also governs digits (both) and
  uppercase (pyzy); pre-existing coupling, documented.
- pyzy's input-cursor `|` is stripped from the preedit — this library never
  moves pyzy's cursor, so it was always trailing.
- **pyzy's user-db save is handled** — the `g_timeout` never fires (no
  `GMainLoop`) but the timeout *id* being set is what `~Database` checks, so
  `Database::finalize()` saves at shutdown. Do not re-derive the first half and
  conclude learning is dropped. `src/engines/pyzy/pyzy_backend.cc:816`.
- **JIS ¥-vs-ろ cannot be expressed** (`layout_key` is US-QWERTY). Accepted:
  kana-hardware-only, and ー is still typeable from `|`.
- **A backend's global-init failure is per-engine, not fatal**:
  `pathime_has_engine()` goes false, `pathime_init()` still succeeds.
- `pathime_*_reset_option()` on a never-set option is a complete no-op (no
  composition reset, no dispatch); a context-level option set dispatches
  `composition_changed` symmetrically with the engine level.

## 6. The table engine

### 6a. Shape

- **A directory under `src/`, not a library under `engines/`**: a standalone
  surface would have needed its own key/composition/option types wrapped by an
  adapter that did nothing else. `engines/` means vendored; this code is ours.
- **The header boundary is load-bearing**: `table_backend.cc` is the only file
  in the directory that includes `backend.h`. That is what lets
  `tools/table-compile` and `tests/core` link the data layer without the
  engine; the short explicit source lists make a violation a link error.
- **Tier 3 lives behind the seam**: `options.cc` consults
  `EngineBackend::declared_number()/declared_text()` between the engine store
  and the descriptor default; a table's declaration never enters the store.
- **Tables are named, not pathed** (resolved inside the resource directory).
- **Compiled at build time** by `tools/table-compile` (C++, shares the data
  layer) — replaces ibus-table's Python/sed pipeline; what makes the data build
  identically on Windows.
- **Enumeration reuses the option machinery** (`valid_value_count` +
  `pathime_option_value_name()`); no build-time manifest — opening all five
  tables costs 1.1 ms, measured.

### 6b. Behaviour

- **Char prompts stay in the preedit** (Cangjie `ab` shows 日月, commits `ab`):
  `BEGIN_CHAR_PROMPTS` is the keycap legend, and the preedit is the keyboard
  rendered back at the user. The header's preedit clause names key legends as a
  permitted departure. Cost: the preedit is not literal committable text for
  four of the five shipped tables, and a client has to know it.
- **The table loads when `PATHIME_OPT_TABLE_FILE` is set** (`prepare_string()`
  before the store; unresolvable name = `PATHIME_ERROR_BACKEND`) — the setter
  is the last point the failure can be blamed on the name. Cost: one setter
  does file I/O and can fail.
- **Pinyin mode (§11.2) and suggestion mode (§11.3): decided against.** The
  data would be a third GPL-3 dependency; the trigger (a bare `Shift_R` press)
  is inexpressible in this key model; and the audience is thin (Cangjie/Quick
  users speak Cantonese). The options stay and report honestly
  (`TableProperties::pinyin_data` records whether rows exist). §11.2 is a
  lookup escape hatch *within* a table method, not a second pinyin — so the
  duplication worry dissolved; the surviving ruling is that **this library will
  not ship a table that is itself a pinyin table**.
- **Full-width conversion is shared with pyzy** (`src/punctuation.*`), not
  transcribed from ibus-table — the references disagree on four characters and
  one behaviour per concept won. The variant resolves
  `PATHIME_OPT_CHINESE_VARIANT` → tier 3 → `LANGUAGE_FILTER`. Swallowed: a CJK
  table claims *every* printable ASCII key, else it never sees the `1` in
  "1.5" and commits "1。5".
- **Table-carried punctuation is stripped at compile time** (2026-08-01).
  Upstream ibus-table-chinese 1.8.9+ gives cangjie/quick punctuation *entries*
  — `[` in `VALID_INPUT_CHARS`, rows offering 「〔［… — which under this engine
  would make those keys composition input and starve the shared layer that owns
  them (the "claims every printable ASCII key" swallow above). Under ibus-table
  they are a candidate menu; here punctuation is one determinate substitution.
  The rule is derived from the rows, like the `z` wildcard: an ASCII
  punctuation character no multi-key code spells with is stripped, with its
  single-key rows (stroke5's `,./` alphabet survives; non-CJK tables and
  declared wildcards are untouched; `--keep-punctuation` opts out). Cost: a CJK
  table whose punctuation char forms only single-key codes loses those entries.
  Reopen if the API ever grows punctuation candidate menus.
- **Glyph filtering is build-time, from a checked-in map** — never `fc-query`
  at build (a `.db` must not be a function of the build machine's fonts). Same
  commit + same map = byte-identical, checked across MSVC and clang-cl.
  `coverage.*` is linked by the compile tool only, never the library.
- **The `z` wildcard is derived per table** by the compile tool — declared only
  where `z` never appears after the first key (wubi-jidian86's `zzbd` declines
  it). A wildcard that is also an input character is **literal at position 0**
  and a wildcard after (`is_wildcard_at()`) — cangjie5's 496 `z`-prefixed
  punctuation codes are reachable no other way.

### 6c. Out of scope, and what compatibility means

- **User-derived phrases (`RULES`/goucima, spec §3.5): out of scope for the
  first phase** (2026-07-28). The deciding argument is reach, not effort: only
  wubi-jidian86 can use it (cangjie5/quick5 declare
  `USER_CAN_DEFINE_PHRASE = FALSE`, stroke5 declares nothing, zhuyin has the
  flag but no `RULES`), and wubi is the table this library has least reason to
  lead with. **Standing rule: anything reachable only through wubi-jidian86 is
  out of scope by default.**

  What it is: committing a multi-character string the system table lacks makes
  the engine file a *new dictionary entry* — not under the typed keys but under
  a code concatenated from each character's goucima per `RULES`, with
  `freq = -1, user_freq = 1` (`refs/ibus-table`,
  `tabsqlitedb.py:1527-1587`). It creates vocabulary under codes the user did
  not choose; it is not the reordering `PATHIME_OPT_LEARNING` means.

  Why it is a discussion, not a task: ibus-table's entire discoverability is
  showing the would-be code in the **auxiliary text** (`#: <code>`), a channel
  §4c removed on purpose. Shipping without it files vocabulary under codes the
  user is never told. Also: `user_freq = 1` outranks every never-chosen system
  entry (second sort key), and a derived code can collide with a real one,
  unchecked. The data layer is complete (`RULES` parsed, goucima compiled and
  readable); only derive-and-insert is missing.

- **Compatibility ruling**: *"we read and write the format, and we use their
  sources to make our own sauce."* Schemas are identical; the user database
  interoperates (we write the `version = '1.00'` desc row ibus-table checks);
  reading a distro-compiled `.db` is believed to work but untested (`TODO.md`
  holds that open question). Our `.db` is **not** what `ibus-table-createdb`
  would produce: glyph filtering, frequency transfer, and the derived `z`
  wildcard — the last behaviourally wrong under ibus-table (its lookup is a
  plain `str.replace` with no position rule, so cangjie5's punctuation would be
  unreachable there). Cheap fix if strict compatibility is ever wanted: record
  the derived wildcard under a private `ime` key.

### 6d. Glyph coverage on Windows (2026-07-28)

- **GDI's `GetFontUnicodeRanges` cannot report supplementary coverage** —
  measured against SimSun-ExtB (60,349 supplementary code points): 97 BMP code
  units, zero surrogates. A generator written to it would silently miss the one
  range the filter decides about. `read_charset()` parses the font's own `cmap`
  (formats 4 + 12) instead: one reader, no platform dispatch, font need not be
  installed. The Noto map was deliberately not re-derived with it (no Linux
  data change); `TODO.md` has the pending check.
- **Windows draws Extension B** (in-box ExtB faces) — the filter's premise is a
  Linux font-landscape fact. Hence `LIBPATHIME_TABLE_COVERAGE` ∈
  {`noto`, `windows`, `none`}; `none` is honoured via `--no-glyph-filter` so
  the map stays compiled and testable.
- Two maps, neither a superset; the default follows the platform. Accepted
  weakening of §6b's promise: the same commit on different platforms ships
  different tables; the same commit with the same map is byte-identical.

## 7. Focus removed (2026-07-29)

There is no focus concept in the API. It had become a bool gating three calls;
no backend was ever told about transitions; exclusivity was never enforced (and
the multicontext suite focused everything at once). The vendored libraries have
no focus concept — it lives in the IBus wrapper layer, and what those wrappers
do on focus (panel registration, config re-read, discard-on-focus-out, client
identity logging) is all either excluded from this model or already the
client's. Cost accepted: the startup tripwire (unfocused-by-default catching an
unwired client) is gone, and a gate cannot be re-added at full strength later —
it would have to default focused. The status enum was renumbered
(`_OUT_OF_MEMORY` = 7, `_BACKEND` = 8; nothing had shipped).

## 8. Reset and commit (2026-07-29)

- **`reset()` cannot commit — by signature.** It takes only a `Composition *`;
  the `Output *` escape hatch no adapter ever used was deleted. Under
  `PREEDIT_NONE`, reset also forgets the tracked in-document syllable, else the
  next key issues a deletion for settled text.
- **`pathime_context_commit()` routes to each adapter's Return path.** It is
  NOT reset-with-a-prior-commit: commit runs `punctuation_.note_commit()`,
  reset runs `clear()` — observably different (quote alternation, the "1.5"
  digit look-behind). Reset = the client is starting somewhere else; commit =
  continuing in the same place.
- **Empty commit returns `PATHIME_OK` and dispatches nothing** — not
  `UNSUPPORTED`, which would force a read-the-composition-first guard on every
  client leaving a field.
- pyzy's `commit()` must end with the same `harvest()` as `process_key()` —
  pyzy's reset speaks only through observer dirty flags.

## 9. Punctuation look-behinds read the document (2026-07-29)

The three bools (`quote_open`, `double_quote_open`, `prev_was_digit`) are
re-derived from surrounding text when the client supplies it, and the snapshot
wins where it shows something — the rules are claims about the *document*.
**The asymmetry runs one way**: finding a mark is proof; not finding one is not
proof of absence (the fragment may not reach it) — `observe_document()` never
*clears* tracked state. **Ordering**: `observe_document()` first, this
dispatch's `note_commit()` after — the snapshot predates the dispatch;
reversing them reintroduces the decimal-point bug. This is the library's first
*opportunistic* use of surrounding text (named as a category in CONCEPTS), and
deliberately not `PATHIME_REQUIRES_*`. Two sources of truth accepted; the
precedence rule is one sentence.

## 10. What Hangul may assume about the document (2026-07-29, extended 2026-07-31)

Under `PREEDIT_NONE`, a half-built syllable at the caret is not picked back up.
libhangul cannot seed a context (replaying jamo as keystrokes is layout-specific
reconstruction, not resumption); ibus-hangul itself abandons on mismatch
(`check_caret_pos_sanity()`) — current behaviour matches the reference; and the
document cannot distinguish a finished 하 from a half-built one, so the rule
could not expire — it would mean any syllable before the caret is resumable
forever (하 typed last week + ㄴ → 한). Cost, real: after the break, Backspace
deletes the whole syllable rather than a jamo. Reopening this needs a Korean
user, not another reading of libhangul.

**The converse, and the same answer: the mode does not run blind either
(2026-07-31).** ibus-hangul's `PREEDIT_MODE_NONE` keeps a one-syllable buffer
(`hangul->preedit`, `engine.c:889-891`) and it invites the reading that the mode
is forward-only and needs no surrounding text; our `in_document_` is that same
buffer. The reading is wrong. The buffer exists to *size* the
`delete_surrounding_text` call (`engine.c:879-884`), and ibus-hangul checks it
against the document on every key (`check_caret_pos_sanity()`,
`engine.c:738-785`) rather than trusting it. Trusting it is what is rejected
here: a caret the user moved turns the buffer into a description of somewhere
else, and issuing the deletion anyway deletes whatever now sits before the caret
— the only way this mode corrupts rather than degrades. CONCEPTS.md's "an engine
may only ask to delete text it can actually see" is the invariant that forbids
it, and it is core, not Hangul's. Cost, real: a client that can delete
surrounding text but cannot report it is locked out — it passes context creation
and then strands every jamo. Reopening this needs a client that can prove its
caret has not moved by some other means, not a tighter buffer. libhangul itself
offers nothing here: one syllable, no commit history, `hangul_ic_backspace`
returns false on an empty buffer (`hangulinputcontext.c:191-223`, `:453`).

## 11. Testing decisions (2026-07-29)

- **Allocation-failure injection: decided against.** `context.cc`'s
  `std::bad_alloc` recovery paths and `table_db.cc`'s SQLite failure arms stay
  — cheap and correct — but **deliberately unreached**: a process out of memory
  does not survive what comes next, so the tests would buy confidence in a
  scenario nobody recovers from. Do not file those lines as coverage
  oversights, and do not build the injection harness.
- **Fuzzing the table source parser: decided against.**
  `parse_table_source()` is 100% line-covered and its real-world input surface
  is thirteen fixed, checked-in tables that essentially nobody edits. A fuzzer
  would defend a corpus of thirteen known inputs.

## 12. What the first language binding taught (2026-07-31)

A Python ctypes binding — ~600 lines plus ~250 of declarations, all five
engines, written and tested in a day — needed **zero library changes**, which
was the point of asking. Its friction list drove one round here. What landed
lives where it belongs: the header states what `data_dir = NULL` selects,
`pathime_engine_name()` completes the introspection pattern beside
`pathime_option_name()`, the Windows install carries its runtime-DLL closure
(verification queued in `TODO.md`), and `pathime_context_isolate_options()`
was added. Recorded here is what was ruled, declined, or reverted:

- **Late resolution of engine options stays; isolation is the opt-out.** The
  trade had never been argued in a recorded round. Ruling: an engine-level set
  reaches every non-overriding context immediately, synchronously, inside the
  setter call. Reason: it is the reference behaviour (ibus-hangul and
  ibus-table both push a settings change into running engines; open fields
  follow), and the synchrony is inherent to having no event loop — elsewhere
  the API's most-praised property. Cost, stated in the header: engine setters
  are not callback-safe and invoke callbacks belonging to contexts they were
  never passed, so a binding's obvious per-call error slot is the wrong shape
  (the Python binding carries a weak-set of contexts per engine to surface
  those errors).
  - **Rejected: copy-on-create.** An engine set after context creation goes
    silently inert for open contexts — a trap aimed at exactly the confused
    one-context client it would help; contexts of one engine diverge by
    creation time with no getter revealing it; `reset_option` needs a new
    rule either way it is read, making the frozen copy a hidden fifth tier;
    and a client wanting the fan-out back must re-implement
    `commit_change`'s override-respecting loop outside the library.
  - **Rejected: lazy dispatch, and forbidding engine sets once contexts
    exist.** The former breaks "takes effect immediately" and cannot defer
    the four `resets_composition` options — keys would be read under a scheme
    the store no longer names. The latter forces recreate-to-reconfigure on
    long-lived engines.
  - **Tried and reverted: dispatching only where a context's effective value
    changed.** Implemented, tested green, popped off master the same day. The
    comparison must be per context and must run full resolution — tier 3 and
    the Hangul cap can absorb a change for one context and not its neighbour
    — so `options.cc` grew a capture/compare snapshot with its own string
    lifetimes and OOM rules; and the leverage was only ever the client that
    re-sets values already in effect. Reopen only with such a client in hand,
    and note that isolation serves that client better.
  - **Added instead: `pathime_context_isolate_options()`** — the header's
    contract is the authority. One call makes the per-option immunity an
    override has always had (the broadcast skips contexts where `is_set`
    answers) total: every supported option's effective value is copied into
    the context's own store, silently, because nothing resolves differently.
    Opt-in, visible to `is_set`, reversible per option through
    `reset_option`, no new tier, no new resolution rule. The verb was chosen
    over pin/adopt/detach — "adopt" already means candidate adoption in
    `docs/CONCEPTS.md`.
- **No static pre-init `pathime_option_type()`.** An option's type is
  option-static by construction — the descriptor owns type, bounds, defaults
  and `resets_composition`; an engine narrows only support and valid values —
  but the sole consumer of a static lookup would be a pre-init settings flow
  no client has, and that flow would want the whole descriptor, so the right
  future addition is a static info variant, waiting for its consumer. A
  binding's actual cost today is one info call per typed access, cacheable
  after the first.
- **No "writing a binding" document, deliberately.** Binding guidance lives
  in actual binding implementations, not in this repository. The one sentence
  such a document would have opened with is folded into the late-resolution
  ruling above.

## 13. Clearing the queue (2026-07-31)

A pass over everything `TODO.md` held as undecided. What became work is queued
there; what was closed is here.

- **The three inert table options, settled individually** — they had been
  filed as one item, and they do not share an answer.
  - `PATHIME_OPT_PREDICTION`: **drop `kTable` from the descriptor row.** It
    only ever meant suggestion mode, which §6b decided against, so an engine
    that reports it supported is promising something it will never do. Cost:
    a client that sets it on a table context now gets `UNSUPPORTED` instead of
    silence — the honest answer, and the one `PINYIN_FALLBACK` already gives.
  - `PATHIME_OPT_INCOMPLETE_INPUT`: **give it a tier 3 case.** The engine does
    the thing; it just reads the table's `AUTO_WILDCARD` rather than the
    option. Tier 3 is the mechanism that already reconciles exactly this — a
    table author's choice as the default, a client override on top — and
    `AUTO_COMMIT`, `AUTO_SELECT`, `LEARNING` and `CHINESE_VARIANT` all use it.
    Cost: none visible today, since no shipped table declares it false.
  - The two wildcards: **stay accepted-and-inert, queued.** Not wiring. Making
    a client value reach the search needs a rule for a character that is in the
    table's alphabet — the condition our own derivation is careful about — and
    a decision about what position-0-literal means for a character no table
    author reserved. Reopen with a consumer that wants to choose its own.
- **The header self-explanation pass: dropped, not deferred.** Its own two
  tests — does a client's behaviour depend on this, would removing it let a
  closed question reopen — are passed by almost every passage it targeted, and
  the rationale could not have moved into this file anyway without recreating
  the coupling the self-containment rule exists to remove. What survived was
  two passages naming a vendored library to no client's benefit, which is not
  worth a pass. `include/pathime/pathime.h` is right as it stands.
- **Thread-safety enforcement: declined, and there was no claim to weaken.**
  The contract has never said thread-safe. It says calls must not overlap, and
  says explicitly that this is about concurrency rather than thread identity —
  any thread, never two at once, handoff needs the usual happens-before. A
  debug-build in-call flag was considered and rejected as conditional on
  evidence that does not exist: no client has made the mistake. Reopen if one
  does.
- **Anthy's commit-history completions: decided against merging.**
  `anthy_set_prediction_string`/`anthy_get_prediction` answers a different
  question from the one the conversion path answers — what whole phrases has
  this user committed before that start like this, versus what does this
  reading convert to. Feasibility was never the obstacle (that cache is
  separate from `ac->seg_list`, so driving it cannot disturb conversion, unlike
  the pyzy obstruction behind `PATHIME_OPT_LEARNING`). Folding them into one
  strip would make `PATHIME_OPT_PREDICTION` mean two things, and the added half
  is empty on a fresh profile and whenever learning is off — invisible to the
  suites by construction. Reopen as a separate option, with a consumer.
- **`docs/CONCEPTS.md` keeps "Input purpose and hints".** It describes the
  concept space a CJK engine interface has to account for, and it is labelled
  in place as the one part of the model the library does not implement. §1
  holds the deferral itself.
- **clang-format: measured and rejected.** Eleven candidates against the tree;
  the floor is 18% of every non-vendored, non-generated line, and the plan's
  own rule — an enormous diff means the wrong config, not a mandate to reformat
  — decides it. The style resists because it is deliberate, not accidental:
  ~1,400 lines deliberately run 81–90 columns, and the settings that look like
  they would preserve hand alignment make the diff worse by aligning where the
  author did not. Two regressions are unfixable by configuration — the public
  header's status enum stops being a table, and `romaji.cc`'s 286-line
  one-row-per-line romaji table packs into 73. Cost: no mechanical guard
  against drift; the style stays a matter of reading the surrounding code, as
  `CLAUDE.md` already asks. The measurements (clang-format 19, changed lines
  of 36,890 non-vendored, non-generated): 80 columns 9,780; 90 columns 6,902;
  100 columns 7,737; 90 + `ReflowComments: false` 6,860; + `IndentPPDirectives`,
  `AlignEscapedNewlines: Left` 6,786; + `AlignConsecutive*` 7,986 and
  + `BinPack{Arguments,Parameters}: false` 7,452 — the last two *worsen* it.
  Re-run before reopening; they are cheap. If ever reopened, the honest form is
  a check over new files only. The one finding that outlives the decision is
  that the three generated headers must be excluded from any whole-tree text
  operation, being 64% of the raw diff and required to match their generator's
  output byte for byte.

## 14. Going public (2026-08-01)

The repository went public (`github.com/bjj/libpathime`), grew CI, and
released v0.1.0, all in one day; `.github/workflows/` carries the operational
reasoning as comments, `THIRD-PARTY.md` the licence consequences, and BUILD.md
the release mechanics. What was *decided* rather than merely done:

- **Two release packages per platform–arch pair, and only two.** A library
  package (headers, libraries, CMake + pkg-config, data) and a standalone
  demo package (binary, libraries, data) — no separate runtime package,
  because the runtime/devel split serves a distribution's dependency
  resolver and nothing plays that role on a release page; the whole layout
  is an application bundling the library beside itself. Cost: developers
  download headers they may not need. Reopen: a consumer for a runtime-only
  artifact materialises — the four install components (`runtime`, `devel`,
  `data`, `demo`) already express the split, so it is one more cpack line.
- **Per-arch archives, no combined SDK.** The generated
  `pathime-targets.cmake` describes one triplet; a `lib/x64`-style combined
  zip needs a hand-written dispatching config free to drift from the
  generated truth. Cost: the ~25 MB arch-independent data is duplicated
  across archives, on our side of the wire.
- **Every artifact ships all data and is therefore GPL-3 as a whole**, stated
  on the release rather than implied by `LICENSE`;
  `LIBPATHIME_WITH_ANTHY=OFF` / `LIBPATHIME_WITH_TABLE=OFF` are the escape
  hatches. The corresponding-source offer is a generated tarball with every
  submodule at its pin (`tools/make-source-tarball.sh`) — GitHub's
  auto-tarball omits submodules and does not build. release.yml proves the
  tarball by building and testing from it.
- **pyzy does not link glib; GlibLess provides the calls its sources make.**
  glib was pyzy's only external library besides sqlite3, and on Windows it
  dragged its whole vcpkg closure (libiconv, libintl, pcre2 — four DLLs, three
  of them LGPL with a relinking obligation) into every artifact for ~28
  shallow functions: asserts, printf helpers, UTF-8 walkers, file operations,
  a timer, and a timeout that never fires because no `GMainLoop` runs.
  `src/GlibLess.{h,cc}` on the fork's `libpathime` branch implements exactly
  that inventory (~600 lines carried; the glib names are macros over
  `pyzy_g_*` symbols, so a real glib in the same process never collides), and
  the port compiles it like any other pyzy source. Alternatives rejected: a
  fake `<glib.h>` in our compat layer (replaces a library, not an environment
  — the compat layer's charter — and its unprefixed symbols would collide
  with a consumer's real glib in a static build); vcpkg-closure shipping with
  MSYS2/gvsbuild/Conan/static-glib variants (each keeps glib itself); and
  upstreaming (openSUSE/pyzy is near-dormant and serves the glib-native ibus
  ecosystem — a removal offers it nothing). Cost: the branch owns ~600 lines
  of shim, and a submodule bump that introduces a new g_* call extends
  GlibLess by hand. Reopen only if a bump makes pyzy's glib surface deep —
  GObject, GIO, GRegex — where reimplementation stops being honest.
- **POSIX artifacts carry no external libraries; Windows artifacts carry the
  vcpkg sqlite3.** Windows has no system sqlite3, every other platform does.
- **No static release artifacts.** The LGPL relinking obligation attaches to
  a static artifact in a way it does not to the shared arrangement, and a
  static artifact pushes the vendored archives and the C++ runtime onto the
  consumer's link line — a support surface with no audience. Static stays a
  tested build (`BUILD_SHARED_LIBS=OFF` is in CI, and the consumer job runs
  against a static install); nothing ships it. The vcpkg port, when written,
  should offer no static feature until someone asks.
- **Releases are drafts from version-shaped tags.** `v[0-9]+.[0-9]+.[0-9]+`
  only; the workflow fails a tag that disagrees with the version macros in
  `pathime.h` (§15);
  publishing the draft is a human act. Provenance is attested
  (`gh attestation verify` answers authenticity); the binaries are unsigned,
  decided 2026-08-01 — SmartScreen's "Run anyway" is the accepted cost, and
  Azure Trusted Signing (~$10/mo) or SignPath's OSS program are the routes
  if the demo finds an audience.
- **Linux release binaries build on the oldest supported runner image**
  (ubuntu-22.04 → glibc 2.35 floor, stated in the notes), never
  `ubuntu-latest` — silence about the floor is how "doesn't run on Debian
  stable" becomes the first issue filed.
- **`CHANGELOG.md` exists as of the first release** — one dated entry per
  release, the only document that speaks in dates; everything else keeps the
  present-tense rule.
- **The macOS port (Phase 2 of the plan) cost three CI rounds** on a private
  spike repo, landed as PR #2: the `_NSGetExecutablePath` fallback, the
  uuid-sites' Darwin arms (SDK header, libSystem symbols, no `uuid.pc`,
  nothing to link), and the recorded choice that macOS inherits the Noto
  glyph map until a PingFang map is generated and measured (`TODO.md`).

## 15. The release review (2026-08-02)

An external review of this repository and both bindings, focused on the
distribution boundary; every claim was verified against the trees before
adoption. Most of what it changed is workflow mechanics whose reasoning
lives as comments in `.github/workflows/`. What was *decided*:

- **The public header is the version's single point of definition.** The
  review did not ask for this; verifying it flushed out v0.1.1 shipping with
  header macros still saying 0.1.0, because the version was hand-maintained
  in two files and only the CMake one was guarded. Ruling: `pathime.h` owns
  the `PATHIME_VERSION_*` macros — they are part of the documented contract —
  and CMakeLists.txt parses them, failing configure if the string and
  numeric macros disagree. The alternative, generating the macros into
  `pathime/config.h` from a CMake-owned version, was rejected because it
  moves part of the contract into a generated file, and `config.h` records
  build choices, which a release version is not. Cost: the parse is a small
  regex contract between the header's formatting and the build; a malformed
  edit fails configure, which is the guard doing its job.
- **Pre-1.0, SOVERSION tracks the minor** (`libpathime.so.0.1`). SOVERSION
  had been the major alone, so the loader would substitute any 0.x into a
  program built against any other 0.x, while the CMake package file said
  `SameMinorVersion` — the two halves of the same promise disagreed, and the
  loader's was the lie. Ruling: the SONAME says what the package file says;
  at 1.0 it becomes the major alone. Cost: every 0.x minor forces a relink
  even when the ABI happened to survive. Rejected alternatives: promising
  ABI stability across all of 0.x (gives up what 0.x is for), and an
  independent ABI integer (a third hand-maintained number, right after the
  version drift showed what hand-maintained numbers do; worth revisiting at
  1.0 alongside ABI-diff tooling).
- **Declined: an ABI-regression CI job (libabigail), and fuzzing the
  key-event/UTF-8 surfaces.** The ABI job is machinery for a promise the
  SONAME now scopes to a minor; revisit at 1.0. The fuzz targets extend §11's
  fuzzing decline: the sanitizer jobs already walk those paths over the
  suites, and no consumer feeds untrusted input to that surface. Reopen
  either with a consumer that does, or at 1.0.
  macOS Intel and Windows arm64 wait for a consumer to ask.
