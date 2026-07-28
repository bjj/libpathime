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
  section numbers (§1, §3, §4a–§4c, §5, §6a–§6c) are the ones code comments
  cite. **Read it before reopening anything that looks undecided** — most
  things that look open were closed there, on purpose, with reasons. §6 is the
  table engine, and is where most of this file's former bulk went.

Status in one paragraph: the build (Linux and Windows), the core (all 44
public entry points), **all four** adapters — hangul, anthy, pyzy and the
table engine — options and negotiation including tier 3, the terminal demo
client, the preedit rule, and the eager candidate strip are built and tested:
34 suites, all passing on Linux with every backend enabled
(`docs/testing.md`). The table engine types real Chinese against tables
compiled out of `ibus-table-chinese`, trimmed at build time to a font's glyph
coverage. The detailed ledger is at the top of `docs/design-history.md`.

---

## The table engine: what landed, and what did not

The engine is real. `src/engines/table/` implements the source `.txt` parser
(spec §3), the compiled SQLite schema (§4), the user-database schema (§5), the
composition mapping (§6), the key-event state machine (§7), lookup and
candidate ordering (§8), select and commit (§9), and Chinese variant
classification (§11.1) and frequency learning (§10.1). `tools/table-compile`
compiles tables at build time,
and five of them ship: cangjie5, quick5, wubi-jidian86, stroke5, zhuyin.

Table enumeration is settled and done. The installed tables are the legal values
of `PATHIME_OPT_TABLE_FILE`, reported through the introspection already there —
`pathime_option_info_t::valid_value_count` says how many, and
`pathime_option_value_name()` names each by index. No new entry points, and no
display names, icons or language lists: what comes back is the machine-readable
key the setter accepts, because presentation is the client's domain. The demo
picks a table with the same Left/Right that steps an enum, which is what it was
missing.

`src/engines/table/README.md` is the map. What follows is only what is missing.

Two smaller items from the same round are closed and need no entry of their
own: `LIBPATHIME_WITH_TABLE` already defaults to `ON` with auto-disable when
SQLite is missing (`cmake/LibpathimeOptions.cmake:77`), and the demo's "engine
does not implement this operation" on Enter over `table-file` went away with
table enumeration — `adjust_option()` returned `PATHIME_ERROR_UNSUPPORTED` only
because `valid_value_count` was 0, and it no longer is.

The behaviour questions this engine raised were answered in the round of
2026-07-28 and are **built**: char prompts, load-on-set, the pinyin-fallback
honesty fix, shared full-width conversion, compile-time glyph filtering, and the
derived `z` wildcard. The reasoning is `docs/design-history.md` §6 — read that
before reopening any of them; the shape decisions taken while the engine was
written (the single directory, the header boundary, tier 3 behind the seam,
build-time compilation, enumeration through the option machinery) are §6a.

### Not implemented, in rough priority order

- **User-derived phrases (§10.2). OUT OF SCOPE for the first phase**, decided
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
  (reordering by use, §10.1, already implemented). It *creates vocabulary*, under
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

  **libpathime has no auxiliary text.** That was a deliberate removal — the
  model says what a user typed is the composition, not something supplemental to
  it (`docs/CONCEPTS.md`; spec §6.4 and §12 both record it as unused). So the
  channel this feature depends on for its usability is one we do not have and
  removed on purpose. Adding it back for one feature of one shipped table is a
  large decision; shipping the feature without it is shipping the silent half.

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
- **Pinyin mode (§11.2) and suggestion mode (§11.3).** **Decided against**, and
  the reasoning is `docs/design-history.md` §6b — the data would be a third
  GPL-3 dependency, the modes were never reachable through our key model
  anyway, and the audience is thinner than it looks. The options remain in the
  header and now report themselves honestly.

  One live note kept here rather than in the history, because it is an
  alternative rather than a decision: if this is ever revisited, the fallback
  could be satisfied from **pyzy** instead of vendored data. Rejected for now
  on two grounds independent of effort — it would make table input depend on
  `LIBPATHIME_WITH_PYZY`, so disabling an unrelated engine would silently
  remove a table feature, and it would put pyzy's candidate ordering and
  learning inside a table candidate list.
- **One of the nine sort keys (§8.2).** Key 2 (the pinyin tone-suffix penalty)
  is now moot: pinyin mode is not being implemented. Key 8 (Big5 code of the
  first character, Cangjie and Quick only) needs a Big5 mapping this library
  does not carry — the same regenerate-don't-copy situation as the variant
  table, and `tools/generate-variants.py` is the pattern to follow. It affects
  only ties the remaining seven keys already order plausibly.
- **The phrase cache (§5.4).** Explicitly optional in the spec. Not started,
  and worth measuring before writing: a lookup is one statement against one
  unindexed table, and it has not been profiled.

### Open questions the implementation raised

- **Two behaviours in §7.2 that the spec does not pin down.** At
  `MAX_KEY_LENGTH` with `AUTO_COMMIT` off the key is absorbed and discarded
  (letting it through would drop a latin letter into the middle of a
  composition); and the `AUTO_SELECT` retry recurses one level to reprocess the
  character that broke the match. Both are choices, both are commented at the
  code, and neither has a table in the shipped set that exercises it hard.
- **How compatible are we with a distro `ibus-table-*` package, really?**
  Raised 2026-07-28. No action proposed — the point is to have the answer
  written down, because "we read ibus-table's format" is currently claimed more
  broadly than it is true.

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

- **`ibus-table-chinese` is GPL-3, and its compiled tables now ship inside
  `pathime-data/`.** Still open, and now with a standing consequence: **no
  further GPL data gets vendored until it is resolved.** That is what closed the
  pinyin/suggestion question above — the data exists in `refs/ibus-table` and
  taking it would have added a third GPL-3 source. A licensing question about
  what libpathime distributes, not a technical one, and to be pursued
  separately from the engine work.
- **Glyph-coverage filtering: the runtime half, and Windows.** The compile-time
  half is built and `docs/design-history.md` §6b records why it takes the shape
  it does. Two things remain.

  **Windows has no generator.** `read_charset()` in
  `tools/generate-coverage.py` is the only platform-specific piece and knows
  only fontconfig. That function's docstring is the brief for the session that
  closes it — `GetFontUnicodeRanges` is the equivalent call, the UTF-16
  surrogate trap is named, and the instruction is to ship a *second* generated
  header rather than overwrite the Linux one, because a single map generated on
  whichever machine ran last is exactly the non-reproducibility the design
  avoids. `docs/windows-port.md` carries the same note. Nothing is blocked on
  it.

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

### Not verified

- **Windows.** Everything above was built and tested on Linux only. The engine
  needs SQLite from vcpkg (`vcpkg install sqlite3`, already required for pyzy)
  and the compile tool runs at build time, so nothing structurally blocks it —
  but `docs/windows-port.md` has not been revisited and no Windows build of the
  table engine has been run.

## Queued work

- **The header self-explanation pass.** Much of the commentary in
  `include/pathime/pathime.h` justifies decisions by naming backend behaviour
  (anthy's write-once personality, pyzy's creation-time `InputType`,
  libhangul's nine built-in layouts). That was right while the implementation
  did not exist; now that it does, those notes belong in `docs/*-mapping.md`
  and `docs/design-history.md`, and the header should keep only the contract.
  The two tests for whether a passage stays: does a client's behaviour depend
  on it, and would removing it let someone reopen a question the design
  closed. The table engine has landed, so this is now unblocked — and it should
  pick up `PATHIME_OPT_TABLE_*`, whose doc comments were written before there
  was an implementation to check them against.
- **`docs/ibus-table-mapping.md` does not exist.** Every other backend has a
  source-verified API-to-concepts mapping ending in "Impedance mismatches".
  This engine has the spec instead, which is a different document: it describes
  ibus-table, not the adapter. The mismatches are real — the mid-preedit caret,
  the commit-key policy, the char-prompt commit — and are currently split
  between code comments and `docs/design-history.md` §6, neither of which is
  the per-backend mapping a reader of the other three would expect to find.

## Open questions

- **The composition model has no cursor inside a span, and now three adapters
  have paid for it.** anthy and pyzy decline Left/Right/Home/End while
  composing, because there is no position for them to move and nothing the
  client could be told about the result. The table engine now declines them for
  the same reason, and gives up more than the other two in doing so: spec §6.3
  and §7.3 describe a caret that moves among pre-committed segments and edits in
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
