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

## Ledger (as of 2026-07-29)

Built and tested: the build (Linux + Windows, both presets, both link modes),
all 44 public entry points, all four adapters — hangul, anthy, pyzy, and the
table engine, the last written here rather than wrapped — options and
negotiation including tier 3, the preedit rule, the eager candidate strip, and
the terminal demo. 40 suites pass on Linux with every backend enabled, 39 on
Windows (`hangul.vendored.unittest` needs the Check library). `docs/testing.md`
maps the suites; `docs/source-layout.md` maps the files.

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

## 10. Hangul does not resume a syllable found in the document (2026-07-29)

Under `PREEDIT_NONE`, a half-built syllable at the caret is not picked back up.
libhangul cannot seed a context (replaying jamo as keystrokes is layout-specific
reconstruction, not resumption); ibus-hangul itself abandons on mismatch
(`check_caret_pos_sanity()`) — current behaviour matches the reference; and the
document cannot distinguish a finished 하 from a half-built one, so the rule
could not expire — it would mean any syllable before the caret is resumable
forever (하 typed last week + ㄴ → 한). Cost, real: after the break, Backspace
deletes the whole syllable rather than a jamo. Reopening this needs a Korean
user, not another reading of libhangul.

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
