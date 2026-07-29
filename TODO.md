# libpathime — unfinished business

Start here in a fresh session. This file holds only what is *not* done: the
work queued, the questions genuinely open, and the deferrals a reader might
mistake for gaps. Everything settled lives elsewhere, and the split is
deliberate:

- `docs/CONCEPTS.md` and `include/pathime/pathime.h` — the model and the
  contract, kept in lockstep. The header carries no list of deviations from
  the concepts because there are none.
- `docs/design-history.md` — the settled design rounds, question by question,
  with the evidence each was answered against and what the answer cost.
  **Read it before reopening anything that looks undecided** — most things
  that look open were closed there, on purpose, with reasons.

Everything else in `docs/`, plus every code comment and the public header, is
written to stand on its own: the three development-only documents (this file,
`CLAUDE.md`, `docs/design-history.md`) could be deleted with nothing dangling.
Keep it that way — nothing outside those three should cite them.

Status in one paragraph: the build (Linux and Windows), the core (all 44
public entry points), **all four** adapters — hangul, anthy, pyzy and the
table engine — options and negotiation including tier 3, the terminal demo
client, the preedit rule, and the eager candidate strip are built and tested:
34 suites, all passing on Linux with every backend enabled
(`docs/testing.md`), and 33 on Windows under both presets. The table engine
types real Chinese against tables compiled out of `ibus-table-chinese`, trimmed
at build time to one of two checked-in glyph-coverage maps or to none.

---

## The table engine: what is missing

The engine itself is built — `src/engines/table/README.md` is the map, and
`docs/ibus-table-mapping.md` is the format it reads. What follows is only what
that engine does not do.

### Not implemented, in rough priority order

- **User-derived phrases. OUT OF SCOPE for the first phase**, decided
  2026-07-28. Kept here in full because the investigation behind the decision is
  worth not repeating, not because it is queued.

  The deciding argument is reach, not difficulty. Only **wubi-jidian86** can use
  the feature at all: cangjie5 and quick5 declare
  `USER_CAN_DEFINE_PHRASE = FALSE`, stroke5 declares nothing, and zhuyin
  declares the flag but carries no `RULES`, so it derives nothing. wubi is also
  the table this library has least reason to lead with — it was included to
  broaden the shipped set, not because anything depends on it. A feature that
  reaches one table, and that table the least central one, does not earn its way
  into a first implementation.

  **The same test applies to anything else wubi-only.** If a spec section turns
  out to be reachable only through wubi-jidian86, it is out of scope for this
  phase by default rather than by a fresh argument each time.

  **What it is.** With `USER_CAN_DEFINE_PHRASE = TRUE`, committing a
  multi-character string the system table does not already contain makes the
  engine invent a *new dictionary entry* for it. The key it files the entry under
  is not the keys the user typed: each character has a `goucima`
  ("word-formation code", §3.3/§4.3), and `RULES` (§3.5) says which positions of
  which characters' goucima to concatenate. wubi-jidian86's
  `ce2:p11+p12+p21+p22` reads "for a 2-character phrase, take characters 1 and 2
  of the first character's goucima, then characters 1 and 2 of the second". The
  compound goes into the user database with `freq = -1`, `user_freq = 1`
  (`refs/ibus-table/engine/tabsqlitedb.py:1527-1587`).

  So it is not learning in the sense `PATHIME_OPT_LEARNING` currently means
  (reordering by use, which is implemented). It *creates vocabulary*, under
  codes the user did not choose.

  **How it interacts with typing — and the problem that makes this a discussion
  rather than a task.** In ibus-table the feature is not silent. While a
  multi-character run is in the preedit, the engine shows the code it *would*
  derive in the **auxiliary text**, as `#: <code>`
  (`refs/ibus-table/engine/table.py:1788-1790`). That is the whole
  discoverability story: you build 天下大事 out of four separate selections, you
  watch `#: ggdg` appear beside it, you commit, and now `ggdg` types the phrase.
  Without that display the user gets vocabulary filed under codes they were never
  told, which they can only rediscover by accident.

  **libpathime has no auxiliary text**, by design: the model says what a user
  typed is the composition, not something supplemental to it
  (`docs/CONCEPTS.md`). So the channel this feature depends on for its
  usability is one the API does not have. Adding it for one feature of one
  shipped table is a large decision; shipping the feature without it is
  shipping the silent half.

  Two smaller things worth having in the conversation:

  - The derived entry gets `user_freq = 1`, and `user_freq` is the second sort
    key after exact match (`ranking.cc`). So a silently derived phrase does not
    land quietly at the bottom of a candidate list — it outranks every system
    entry that has never been chosen.
  - The derived code can collide with a real one. Nothing checks; the phrase
    simply becomes another candidate under that code.

  **State of the code:** `RULES` is parsed, goucima are compiled into the
  database and `TableDatabase::goucima()` reads them back, so the data is all
  present. Only the derive-and-insert step is missing — which is why the
  decision above is about reach and desirability, not effort.
- **Write batching (§5.3).** Not implemented, and possibly not worth it: the
  spec's checkpoint-after-16-updates is a durability detail, and the user
  database is in WAL mode where SQLite checkpoints on its own. Measure before
  writing anything.
- **Pinyin mode (§11.2) and suggestion mode (§11.3).** **Decided against**:
  the data would be a third GPL-3 dependency, the modes are not reachable
  through this library's key model anyway, and the audience is thinner than it
  looks. The options remain in the header and report themselves honestly.
  `docs/design-history.md` §6b has the full argument.

  One live note kept here rather than in the history, because it is an
  alternative rather than a decision: if this is ever revisited, the fallback
  could be satisfied from **pyzy** instead of vendored data. Rejected for now
  on two grounds independent of effort — it would make table input depend on
  `LIBPATHIME_WITH_PYZY`, so disabling an unrelated engine would silently
  remove a table feature, and it would put pyzy's candidate ordering and
  learning inside a table candidate list.
- **One of the nine sort keys (§8.2).** Key 2 (the pinyin tone-suffix penalty)
  is moot: pinyin mode is not being implemented. Key 8 (Big5 code of the
  first character, Cangjie and Quick only) needs a Big5 mapping this library
  does not carry — the same regenerate-don't-copy situation as the variant
  table, and `tools/generate-variants.py` is the pattern to follow. It affects
  only ties the remaining seven keys already order plausibly.
- **The phrase cache (§5.4).** Explicitly optional in the spec. Not started,
  and worth measuring before writing: a lookup is one statement against one
  unindexed table, and it has not been profiled.

### Open questions the implementation raised

- **Two behaviours in the key-event state machine that the format does not pin down.** At
  `MAX_KEY_LENGTH` with `AUTO_COMMIT` off the key is absorbed and discarded
  (letting it through would drop a latin letter into the middle of a
  composition); and the `AUTO_SELECT` retry recurses one level to reprocess the
  character that broke the match. Both are choices, both are commented at the
  code, and neither has a table in the shipped set that exercises it hard.
- **How compatible are we with a distro `ibus-table-*` package, really?**
  No action proposed — the point is to have the answer written down, because
  "we read ibus-table's format" is claimed more broadly than it is true.

  The question is sharp because distros ship **compiled databases**, not source:
  `engines/ibus-table-chinese/tables/CMakeLists.txt` runs `ibus-table-createdb`
  and installs `.db` files to `/usr/share/ibus-table/tables/`. So a client
  pointing `resource_dir` at an installed ibus-table tree is a real scenario, not
  a hypothetical one.

  *Reading their `.db` — believed to work, never tested.* The schemas are
  identical, checked side by side: `ime(attr, val)` and
  `phrases(id, tabkeys, phrase, freq, user_freq)`, plus `goucima(zi, goucima)`,
  `pinyin(pinyin, zi, freq)` and `suggestion(phrase, freq)`
  (`refs/ibus-table/engine/tabsqlitedb.py:605-630` against
  `src/engines/table/table_db.cc`). Nothing has ever been run against an actual
  `ibus-table-createdb` output, so this is a code reading rather than a result.
  It is also the direction that would *gain* something: their databases carry
  the `pinyin` and `suggestion` rows ours cannot (see above).

  *The user database — compatible.* Same `phrases` schema, and we write the
  `user_db.desc` row with `version = '1.00'` that ibus-table checks for
  (`tabsqlitedb.py:303-320`; a user database without it is treated as
  incompatible and rebuilt). The paths differ anyway — ours is
  `<data_dir>/table/` — so the two never actually meet unless a client points
  them at each other.

  *Them reading **our** `.db` — structurally yes, semantically no.* A table this
  library compiles is not what `ibus-table-createdb` would produce from the same
  source, in three ways, and the third is a behaviour change rather than
  different data:

  1. Glyph filtering removes about half the rows of cangjie5 and quick5.
  2. Frequency transfer rewrites every frequency at or above the threshold.
  3. We write `SINGLE_WILDCARD_CHAR = z` into `ime` for tables whose source
     declared no wildcard — and ibus-table has **no position rule**. Its lookup
     is a plain `str.replace(single_wildcard_char, '_')`
     (`tabsqlitedb.py:1121-1126` and `1191-1196`), so under ibus-table a
     *leading* `z` would become a wildcard too, and cangjie5's 496 `z`-prefixed
     punctuation codes would be unreachable there. Under this library they work,
     because `TableProperties::is_wildcard_at()` keeps position 0 literal.

  So the honest description of today's state is **"we read and write the format,
  and we use their sources to make our own sauce"** — not "our tables are
  drop-in replacements for theirs". Points 1 and 2 are deliberate improvements
  that any consumer benefits from. Point 3 is the only one that is actively
  wrong somewhere else, and it has a cheap fix if strict compatibility is ever
  wanted: record the derived wildcard under a private `ime` key that ibus-table
  ignores, instead of the format's own. Not done, because nothing today reads
  our tables but us, and the standard key is the more honest description of what
  the table means.

- **GPL data in `pathime-data/` is a packaging matter.** Two files are compiled
  from GPL sources: `table/*.db` from ibus-table-chinese (GPL-3), and
  `anthy/anthy.dic` from anthy's own dictionary (GPL-2 — `alt-cannadic/*` and
  the `mkworddic/*.t` that `dict.args` reads; anthy's `COPYING` says so).
  `THIRD-PARTY.md` records both.

  What remains is a habit rather than a question: the data stays separable and
  labelled. `LIBPATHIME_WITH_ANTHY=OFF -DLIBPATHIME_WITH_TABLE=OFF` builds and
  passes with only pyzy's data in `pathime-data/`, and that should stay true.

  The standing consequence survives: **no further GPL data gets vendored
  without a reason.** That is still what closed the pinyin/suggestion question
  above.
- **Glyph-coverage filtering: the runtime half.** The compile-time half is
  built on both platforms; `BUILD.md`, "Glyph coverage", has the shape and the
  measurements. One thing remains.

  **The runtime half is deferred**, and not only for effort. An embedder should
  be able to *guarantee* their candidate list stays renderable on their target —
  that is a promise about their UI, not a preference about their table — and the
  shape it should take is runtime strictly narrowing compile time, so the
  shipped `.db` stays the upper bound. Four things have to be answered first,
  and none of them is about tables:

  - *What does it mean for the other three backends?* A coverage set that
    filters table candidates but not anthy's or pyzy's would be a promise the
    library keeps in one place and breaks in another. Either it is a
    library-wide concept or it is a misleading one.
  - *What is the API surface?* A set of code points is not a shape the option
    system has — every option today is a number, an enum, a flag set or a short
    string, and a coverage set is none of those.
  - *Is it sane to let a client narrow this far at all?* Nothing would stop
    "here is ASCII, now do Pinyin", and the honest result is nonsense. Whether
    the library refuses that, degrades predictably, or simply lets the client
    have what it asked for is a real decision.
  - *Can it be done without touching the vendored trees?* pyzy and anthy choose
    their own output; filtering theirs means intercepting it at the adapter, or
    it means patching submodules, which is the rule this project does not break.

## Queued work

- **The header self-explanation pass — needs a decision before it is done.**
  The original plan was to strip from `include/pathime/pathime.h` every
  passage that justifies a decision by naming backend behaviour (anthy's
  write-once personality, pyzy's creation-time `InputType`, libhangul's nine
  built-in layouts), moving those notes into `docs/*-mapping.md` and
  `docs/design-history.md` and leaving the header with only the contract.

  **That plan is now partly self-defeating**, which is why it was not carried
  out. `docs/design-history.md` is a development-only document that must be
  deletable; moving contract rationale into it would recreate exactly the
  coupling the self-containment rule removes. And reading the header through
  the plan's own two tests — does a client's behaviour depend on this, and
  would removing it let someone reopen a closed question — almost every
  passage passes. "pyzy fixes the phonetic scheme at context creation" is why
  a client must make a new context to switch; "libhangul exposes only the
  syllable being assembled" is why word mode has the backspace granularity it
  has. Cutting those makes the header shorter and the client's job harder.

  What is genuinely left is narrower, and someone should decide whether it is
  worth doing at all:

  - A handful of passages name a vendored library and give a client nothing —
    `pathime_hangul_layout_t`'s "the nine layouts libhangul builds in", and
    the paragraph at `pathime_init_params_t::data_dir` explaining that the
    directory is what lets anthy's "personality" disappear.
  - The `PATHIME_OPT_TABLE_*` comments were written before the table engine
    existed. They have not been read back against the implementation.

### Three table options are accepted and do nothing

Found by reading `include/pathime/pathime.h`'s `PATHIME_OPT_TABLE_*` comments
back against `src/engines/table/`, which had never been done. The header has
been corrected to describe what actually happens; **the code side is a
decision, not a task**, because each fix is a behaviour change.

- **`PATHIME_OPT_PREDICTION` on the table engine.** Settable, reads back
  `true` by default, and appears nowhere under `src/engines/table/`. It was
  meant to be suggestion mode, which is decided against (above), and
  `TableProperties::declared_number()` has no case for it either, so there is
  no tier 3. `wubi-jidian86.txt` declares `SUGGESTION_MODE = TRUE` and ships,
  so a client that walks the inventory sees an option that promises something.
  Either drop `kTable` from the descriptor row in `src/options.cc` so it
  reports itself unsupported — matching what `PATHIME_OPT_TABLE_PINYIN_FALLBACK`
  already does — or implement the mode.
- **`PATHIME_OPT_INCOMPLETE_INPUT` on the table engine.** The wildcard *is*
  appended before searching (`table_db.cc`, `properties.auto_wildcard`), but on
  the table's own `AUTO_WILDCARD` declaration, never on the option. Setting it
  false changes nothing. The clean fix is tier 3: give `declared_number()` a
  case for it, so the table supplies the default and a client can still
  override. No shipped table declares `AUTO_WILDCARD = FALSE`, so nothing
  visibly misbehaves today.
- **`PATHIME_OPT_TABLE_SINGLE_WILDCARD` and `_MULTI_WILDCARD`.** Stored,
  readable, and never consulted: `build_like_pattern()` takes a
  `TableProperties` and never sees an `OptionReader`, and `is_input_char()`
  likewise, so a client-set wildcard is not even accepted as a keystroke.
  `src/options.cc` half-admits this already ("the table engine's wildcards are
  the obvious future exception").

One smaller thing from the same read, cheap:

- The comment at `src/engines/table/table_backend.cc:196-204` still says "until
  the first context exists, tier 3 therefore contributes nothing." Setting
  `PATHIME_OPT_TABLE_FILE` populates the cache, which is what makes the
  header's "resolves against the new table's declarations immediately" true.

## Decisions wanted

- **Does `docs/CONCEPTS.md` keep "Input purpose and hints"?** It is the one
  section of the model the library does not implement, and it is now labelled
  as such in place. Either it stays as a description of the concept space the
  library sits in, or it comes out and the deferral lives only here. Left in,
  labelled, pending a call.

## Open questions

- **The composition model has no cursor inside a span, and now three adapters
  have paid for it.** anthy and pyzy decline Left/Right/Home/End while
  composing, because there is no position for them to move and nothing the
  client could be told about the result. The table engine now declines them for
  the same reason, and gives up more than the other two in doing so: ibus-table
  has a caret that moves among pre-committed segments and edits in
  the middle, which is a *documented ibus-table feature* rather than an
  incidental capability. Flattening it away is why `Composition::tail` is
  always empty for this engine. For pyzy the decision was forced rather than
  chosen: routing the keys through made pyzy render its own input cursor as a
  literal `'|'` inside `conversionText()` (`PinyinContext.cc:129-142`), so
  typing "nihao" then Left made the preedit read `ni h|a` — a display marker
  inside a string the API promises is plain content text. Declining is right
  for the phone-keyboard target, which has no such key. It is wrong for a
  desktop client wanting to repair the middle of a long run without backspacing
  to it. Revisit with a real consumer — and note that the table engine is now
  the strongest argument for one.
- **Thread-safety is a documented requirement, not an enforced one.** Nothing
  detects overlapping calls. If that proves to be a common client bug, a debug
  build could catch it cheaply with a non-recursive in-call flag per context.
- **Is anthy's own prediction API worth exposing?** `PATHIME_OPT_PREDICTION`
  on anthy is the eager-conversion strip, not history completion. The
  completion API is separately reachable, and its prediction cache is entirely
  separate from `ac->seg_list`, so driving it cannot disturb conversion — the
  obstruction that pushed `PATHIME_OPT_LEARNING` to unsupported on pyzy does
  not exist here. What is in doubt is the value, not the feasibility: the
  completions are empty on a fresh profile and whenever learning is off. If
  they are ever merged into the same candidate list, the option does not
  change meaning; the header already says so.

## Build limitations, stated in BUILD.md as facts

Neither is a bug, and both are recorded here so they are tracked rather than
only described:

- **Cross-compiling is not supported.** anthy's dictionary is built by host
  tools at build time.
- **No CMake package config is installed**, so a consumer names the include
  directory and the library itself.

## Not verified

- **The sanitizers have only been run on Linux, and only over the suites.**
  `docs/testing.md` has the configuration and what a clean run looks like.
  MSVC's `/fsanitize=address` has no UBSan half and has not been tried, so a
  Windows-only memory error would not have been caught — nor would one on a
  path the suites do not walk, which is most of the demo.
- **The table engine has only been exercised on Linux** beyond the suites,
  which do pass on Windows under both presets.

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
