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
classification (§11.1). `tools/table-compile` compiles tables at build time,
and five of them ship: cangjie5, quick5, wubi-jidian86, stroke5, zhuyin.

`src/engines/table/README.md` is the map. What follows is only what is missing.

### Not implemented, in rough priority order

- **Learning (§10).** The user database is created, attached as `user_db` and
  merged into lookups, and `user_freq` is already the third key of the
  candidate sort — but nothing ever writes a row. So `DYNAMIC_ADJUST` and
  `USER_CAN_DEFINE_PHRASE` are honoured as *declarations* (they decide whether
  the union runs, and what `PATHIME_OPT_LEARNING` resolves to at tier 3) and
  ignored as behaviour. This is the largest gap and the one with the machinery
  already under it: §10.1 is an UPSERT on selection, and the WAL batching of
  §5.3 is a durability detail on top.
- **User-derived phrases (§10.2).** Needs learning first. `RULES` is parsed,
  goucima are compiled into the database and `TableDatabase::goucima()` reads
  them back, so the data is all present; what is missing is applying the parsed
  rules to consecutive single-character selections to derive a compound key.
- **Full-width conversion (§11.4).** Only the space is converted today
  (`PATHIME_OPT_LATIN_WIDTH`, in the Space branch of `process_key`).
  `PATHIME_OPT_PUNCTUATION_WIDTH` does nothing for this engine. The table is
  per-language content like `engines/pyzy/punctuation.*`, and §11.4 lists the
  punctuation overrides in full.
- **Pinyin mode (§11.2) and suggestion mode (§11.3).** Blocked on data, not on
  code: the `pinyin` and `suggestion` tables are created when a table declares
  the mode, but their source (`pinyin_table.txt.bz2`, `phrase.txt.bz2`) ships
  with **ibus-table**, not with ibus-table-chinese, so this repository has
  nothing to compile into them. `PATHIME_OPT_TABLE_PINYIN_FALLBACK` and the
  table meaning of `PATHIME_OPT_PREDICTION` are unimplemented until that data
  has a source. Decide whether to vendor it or drop both options.
- **Two of the nine sort keys (§8.2).** Key 2 (the pinyin tone-suffix penalty)
  follows pinyin mode. Key 8 (Big5 code of the first character, Cangjie and
  Quick only) needs a Big5 mapping this library does not carry — the same
  regenerate-don't-copy situation as the variant table, and
  `tools/generate-variants.py` is the pattern to follow. Both affect only ties
  the remaining seven keys already order plausibly.
- **The phrase cache (§5.4).** Explicitly optional in the spec. Not started,
  and worth measuring before writing: a lookup is one statement against one
  unindexed table, and it has not been profiled.

### Open questions the implementation raised

- **Return commits the typed keys, not the preedit that was displayed.**
  Spec §7.4 says the essential operation is "commit the literal input", so
  Cangjie `a`+`b` commits `ab`. But the preedit showed 日月, and the header
  promises at `pathime_composition_t::preedit` that the preedit "is the text
  that would be committed if the composition ended right now", with only
  commit-time normalizations departing from it. Prompt substitution is a bigger
  departure than the Japanese trailing-`n` case that clause was written for.
  Three readings are available: commit the keys (current, pinned by
  `api.engine_table`), commit the prompts, or treat prompts as display-only and
  keep the preedit raw. **This one wants a decision** — it is visible to any
  Cangjie or Stroke5 user, and it is the only place this engine knowingly bends
  the header.
- **Tier 3 answers nothing until a context has typed.**
  `EngineBackend::declared_number()` reads the *loaded* table and deliberately
  does not load one, because resolving an option must not open a database — a
  client calling `pathime_engine_option_get_int()` has asked a question, not
  asked for work. The consequence is that the same call returns the descriptor
  default before the first keystroke and the table's declaration after it.
  Defensible, and possibly surprising. The alternative is loading eagerly when
  `PATHIME_OPT_TABLE_FILE` is *set* rather than when it is first used, which
  moves the cost to a place the client can predict.
- **Two behaviours in §7.2 that the spec does not pin down.** At
  `MAX_KEY_LENGTH` with `AUTO_COMMIT` off the key is absorbed and discarded
  (letting it through would drop a latin letter into the middle of a
  composition); and the `AUTO_SELECT` retry recurses one level to reprocess the
  character that broke the match. Both are choices, both are commented at the
  code, and neither has a table in the shipped set that exercises it hard.
- **The demo cannot reach the table engine, and says so confusingly.** The
  status line reports `needs a table file`, but tabbing into the options panel
  and pressing Enter on `table-file` answers "engine does not implement this
  operation". That is `demo/src/options_view.cc:162`: `adjust_option()` declines
  every `PATHIME_OPTION_STRING` outright. The message is doubly misleading —
  nothing is unimplemented, and the engine named is not the one refusing.

  The fix needs a decision, because the demo edits options by *stepping through
  values* and a table name has no value list to step through. Either the demo
  carries its own list of names to cycle (matching how it already hardcodes
  engine names, but going stale against a build with a different
  `LIBPATHIME_TABLES`), or it scans the table directory — which it cannot do,
  because it never learns where `resource_dir` resolved to. The second option is
  the one that would argue for a table-enumeration API, which was considered and
  declined at the bare-name decision.

  Minimum worth doing regardless: make the refusal say what is actually wrong.
- **`ibus-table-chinese` is GPL-3, and its compiled tables now ship inside
  `pathime-data/`.** Flagged at the decision round and still open: it is a
  licensing question about what libpathime distributes, not a technical one.
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

  So the work is to make it optionally possible rather than to decide it away.
  Open: whether it belongs at compile time (a `--font` flag on
  `tools/table-compile`, needing fontconfig at build time, filtering baked into
  the shipped `.db`) or at runtime (a coverage set the embedder supplies, so one
  shipped table serves targets with different fonts). Compile time is what the
  fork does and is far cheaper; runtime is what an embedder shipping one library
  to several targets would actually want.

- **Is it worth implementing the input methods the other backends already
  cover?** `PINYIN_MODE` and `SUGGESTION_MODE` are the immediate cases, but the
  question is general: pyzy already gives us Pinyin and Bopomofo, and a table
  can be built to do the same job.

  For ibus-table the duplication is harmless — it is one engine among many in a
  world that already offers several pinyin implementations, and a user picks
  one. For libpathime it may not be. Two code paths reaching the same input
  method is two places to be inconsistent about candidate order, about what
  `PATHIME_OPT_CHINESE_VARIANT` does, about learning — and the API deliberately
  presents one behaviour per concept rather than an engine-dependent one.

  So this is not only "how much work", though that matters too: §11.2 needs the
  pinyin table, its source data, the tone encoding and sort key 2, none of which
  is large. It is whether shipping a second pinyin makes the library worse. Answer
  it before writing any of it — the cost of deciding late is a feature that has to
  be taken away.

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
