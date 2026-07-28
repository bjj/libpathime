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
public entry points), **all four** adapters — hangul, anthy, pyzy and the
table engine — options and negotiation including tier 3, the terminal demo
client, the preedit rule, and the eager candidate strip are built and tested:
33 suites, all passing on Linux with every backend enabled
(`docs/testing.md`). The table engine types real Chinese against tables
compiled out of `ibus-table-chinese`. The detailed ledger is at the top of
`docs/design-history.md`.

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

Four questions this engine raised were answered in the round of 2026-07-28 and
are **built**: char prompts stay in the preedit (the header clause was widened
to cover key legends rather than the engine changed); the table loads when
`PATHIME_OPT_TABLE_FILE` is *set*, so a bad name fails at the setter with
`PATHIME_ERROR_BACKEND`; `PATHIME_OPT_TABLE_PINYIN_FALLBACK` reports itself off
unless the compiled database really carries pinyin rows; and full-width
conversion (§11.4) now runs through `src/punctuation.*`, shared with the pyzy
adapter rather than transcribed from §11.4, so one option means one thing across
both Chinese engines. The reasoning belongs in `docs/design-history.md` once that
file grows a table round (queued below).

### Not implemented, in rough priority order

- **User-derived phrases (§10.2).** The remaining half of learning. `RULES` is
  parsed, goucima are compiled into the database and `TableDatabase::goucima()`
  reads them back, so the data is all present; what is missing is applying the
  parsed rules to consecutive single-character selections to derive a compound
  key and insert it with `freq = -1`, `user_freq = 1`.

  Two shipped tables declare `USER_CAN_DEFINE_PHRASE`, not one:
  wubi-jidian86, which also carries `RULES`, and **zhuyin, which does not**.
  Derivation is defined entirely as applying rules to goucima, so a table
  without rules derives nothing — zhuyin gets a `goucima` table it never uses.
  That is the only coherent reading and needs no option, but it does mean the
  feature lands for exactly one shipped table.
- **Write batching (§5.3).** Not implemented, and possibly not worth it: the
  spec's checkpoint-after-16-updates is a durability detail, and the user
  database is in WAL mode where SQLite checkpoints on its own. Measure before
  writing anything.
- **Pinyin mode (§11.2) and suggestion mode (§11.3).** **Decided: not
  implemented, and the options stay.** Three findings closed this.

  *The data will not be vendored.* Its source (`pinyin_table.txt.bz2`,
  `phrase.txt.bz2`) ships with **ibus-table**, not ibus-table-chinese, so
  taking it means a third GPL-3 dependency. Not while the licensing question
  below is open.

  *It was never reachable anyway.* ibus-table binds `toggle_pinyin_mode` to
  `Shift_R` (`org.freedesktop.ibus.engine.table.gschema.xml:50`) — a bare
  modifier press, which this library's key model cannot express and
  deliberately never could. `toggle_suggestion_mode` is `Super+Mod4+F6`.

  *And the audience is thinner than it looks.* Cangjie and Quick are the Hong
  Kong methods, where the user speaks Cantonese and Mandarin pinyin is close to
  useless to them; the Cantonese analogue would be Jyutping, which ibus-table
  does not offer. Only wubi-jidian86 targets a Mandarin-speaking audience for
  whom the fallback is genuinely worth something — and it declares `PINYIN_MODE
  = TRUE` alongside cangjie5, quick5 and stroke5, which do not benefit.

  What was left was a **bug, not a feature gap**, and it is fixed: tier 3
  reported `PATHIME_OPT_TABLE_PINYIN_FALLBACK` *on* for those four tables while
  their compiled `pinyin` table was empty, so the option read as enabled and did
  nothing. `TableProperties::pinyin_data` now records whether the database
  actually carries rows, tier 3 consults it, and turning the option on without
  them is `PATHIME_ERROR_UNSUPPORTED` at the setter as the header always
  promised.

  *The alternative, if this is ever revisited:* satisfy the fallback from
  **pyzy** rather than from vendored data. Rejected for now on two grounds, both
  independent of effort. It would make table input's behaviour depend on
  `LIBPATHIME_WITH_PYZY`, so turning off an unrelated engine silently removes a
  table feature; and it would put pyzy's candidate ordering and learning inside
  a table candidate list, which is exactly the cross-engine inconsistency the
  "second pinyin" question was asking about.
- **`z` as a Cangjie wildcard.** Apple's Cangjie makes `Z` stand for an unknown
  part of a decomposition, and it turns out this is nearly free. In cangjie5
  `z` occurs in 496 of 68,632 rows and **only ever as the first key** — no row
  uses it in a non-initial position. Those 496 are the punctuation codes (`za`
  → `'` `'` `"` `"` `〔`).

  So setting `PATHIME_OPT_TABLE_SINGLE_WILDCARD` to `"z"` already gives the
  behaviour, with no code change, and it is unambiguous everywhere except a
  leading `z`. To have both, the rule is: `z` is a wildcard in non-initial
  position and literal at position 1. Small, well-defined, and it needs a
  decision only about whether the position rule is worth the special case.
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
- **`ibus-table-chinese` is GPL-3, and its compiled tables now ship inside
  `pathime-data/`.** Still open, and now with a standing consequence: **no
  further GPL data gets vendored until it is resolved.** That is what closed the
  pinyin/suggestion question above — the data exists in `refs/ibus-table` and
  taking it would have added a third GPL-3 source. A licensing question about
  what libpathime distributes, not a technical one, and to be pursued
  separately from the engine work.
- **Glyph-coverage filtering should be available, and the reasoning behind it
  is not the one this implementation assumed.** Frequency transfer and font
  trimming are two halves of one purpose, and only the first is implemented.

  The purpose: a table method is not really a candidate-driven input method.
  Its whole advantage over pinyin is determinism — Cangjie can be typed with
  your eyes closed, and unlike pinyin it produces text without ever consulting a
  completion. Candidates are shown anyway, because we have them. But the stock
  candidates for a partial code are frequently *obscure*, and that costs twice
  over: the stock Cangjie table carries roughly twice as many characters as the
  most capable font (Google Noto CJK), and vastly more than a typical font with
  nothing like 30,000 glyphs. So a user one keystroke into the weeds sees a
  candidate list of tofu.

  Frequency augmentation keeps useful characters at the front; coverage
  filtering keeps unrenderable ones out entirely. Skipping the filter works, but
  an embedder should be able to *guarantee* their candidate list stays
  renderable on their target — that is a promise about their UI, not a
  preference about their table.

  The shape it should take is **both, with runtime strictly narrowing compile
  time**: a table is filtered once at build time against an inclusive font, and
  an embedder may additionally supply a coverage set at runtime that can only
  remove more. Runtime never widens, so the shipped `.db` stays the upper bound
  and nothing has to be re-added from a table that no longer carries it.

  **In scope now: the compile-time half only**, and the shape is settled. Not
  the fork's `--font` resolved at build time — that would make the shipped `.db`
  a function of which fonts happen to be installed on the build machine, so two
  builds of the same commit would produce different tables.

  Instead: **bake a coverage map for Noto CJK into a generated header**, exactly
  the pattern `variants_data.h` and `tools/generate-variants.py` already
  establish for the variant table. The build is then deterministic and needs no
  fontconfig at all. A regeneration tool sits behind a CMake option defaulting
  **OFF**, so a future maintainer can rebuild the map against a newer Noto
  without it being a build dependency for everyone else. `tools/table-compile`
  keeps a `--font` override for anyone targeting something narrower.

  Windows gets the same treatment against a standard Windows font, generated by
  its own enumeration path (the Windows font APIs rather than fontconfig).
  **Deferred to a session running on Windows** — it cannot be written or checked
  here, and nothing else is blocked on it.

  **The runtime half is deferred**, and not only for effort. Four things have to
  be answered first, and none of them is about tables:

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

  So: build the compile-time filter, and leave the runtime one until those four
  have answers.

- **Is it worth implementing the input methods the other backends already
  cover?** **Answered: no, and the question was narrower than it looked.**

  The worry was two code paths reaching the same input method — two places to
  be inconsistent about candidate order, about `PATHIME_OPT_CHINESE_VARIANT`,
  about learning — against an API that deliberately presents one behaviour per
  concept.

  §11.2 turns out not to be a second pinyin IME at all. It is a lookup escape
  hatch *within* a table method: type pinyin to find a character whose table
  code you do not know, against the same phrase set, and carry on. It competes
  with nothing pyzy does. So the general question mostly dissolves, and what is
  left is the specific one — **should this library ever ship a table that is
  itself a pinyin table?** ibus-table-chinese has several. The answer is no, for
  the original reason: that would be a genuine second pinyin, and the
  inconsistency would be real.

  Recorded here rather than dropped so the option inventory in
  `docs/ibus-table-options.md` stops reading as undecided.

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
- **`docs/design-history.md` has no round for the table engine.** The decisions
  taken while writing it — the single-directory layout and the data/behaviour
  header boundary, tier 3 living behind the seam rather than in the option
  store, bare-name table resolution, compiling at build time with a tool built
  from the same sources — are recorded in code comments and in
  `src/engines/table/README.md` but not in the history. They should be, in the
  form the other rounds take.
- **`docs/ibus-table-mapping.md` does not exist.** Every other backend has a
  source-verified API-to-concepts mapping ending in "Impedance mismatches".
  This engine has the spec instead, which is a different document: it describes
  ibus-table, not the adapter. The mismatches are real (the mid-preedit caret,
  the commit-key policy, the Return conflict above) and are currently scattered
  across code comments.

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
