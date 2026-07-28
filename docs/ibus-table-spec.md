# ibus-table: Data Format and Engine Behavior for a libpathime Engine

This document specifies everything needed to build a `libpathime` engine that reads the same table
data as `ibus-table` and exposes the input behavior of those tables through the concepts defined in
`docs/CONCEPTS.md`. It is a clean-room behavioral specification derived by studying the reference
implementation in `refs/ibus-table`; no code is copied.

This is a specification we implement, not a library we wrap. `ibus-table` is self-contained Python,
so there is nothing to link against — the table engine (`PATHIME_ENGINE_TABLE`) is written inside
`libpathime` as a peer of the vendored backends, in `src/engines/table/`. This document remains the
specification rather than a description of that code: where the two disagree, one of them is a bug,
and `TODO.md` records the sections not implemented yet.

The document has one organizing goal: **connect the data, the text-input behavior, and our
concepts.** Three kinds of information are kept distinct throughout:

- **Data contract** — the on-disk formats (`refs/ibus-table-chinese` source `.txt` tables and the
  compiled SQLite `.db`). A libpathime engine must honor these *exactly* to share table data with
  `ibus-table`. This is the whole reason the spec is detailed about parsing and schema.
- **Engine behavior** — how typed keys turn into a candidate list, preedit text, and commit text.
  This is described in the vocabulary of `CONCEPTS.md` (engine, input context, key event, handled,
  composition data, commit text, select candidate, reset), *not* in IBus terms.
- **Client / UI policy** — everything `ibus-table` does that `CONCEPTS.md` deliberately pushes to
  the client or excludes entirely (candidate paging, selection keys, orientation, property menus,
  sounds). A libpathime engine parses these fields where they live in the data but does not act on
  them; they are noted so an implementer can recognize and skip them.

`ibus-table-chinese` (Wubi, Cangjie, Stroke5, Zhuyin, …) is distributed as **source `.txt` tables**;
the `.db` files are compiled at build/install time. To "share ibus-table-chinese," a libpathime
engine must therefore be able to consume the source format (§3) and, for interoperating with an
already-installed ibus-table, the compiled schema (§4).

Structure names and C++ signatures are intentionally omitted — those are fixed later when the main
library architecture is defined. This spec describes behavior and data, not a class layout.

---

## 1. What ibus-table Is

`ibus-table` is a generic **table-driven engine**: arbitrary key sequences (`tabkeys`) map to output
phrases, and the mapping lives in a SQLite database compiled from a human-readable text source. One
database = one input method. The same engine logic drives Wubi, Cangjie, Stroke5, Zhuyin, and so on,
purely by loading a different database. In `CONCEPTS.md` terms, each compiled database parameterizes
the **engine** into a specific input method.

Data lives in two layers:

- **System database** — a read-only `.db` distributed with the table, holding the canonical
  key→phrase mappings and the method's configuration (§4).
- **User database** — a per-user `.db` (SQLite WAL mode) layered on top, holding learned
  frequencies and user-derived phrases (§5). This is the only mutable state; it is what makes
  candidate ordering adapt over time.

The reference is written in Python against IBus GObject bindings. A libpathime engine reuses the
*data* and the *text behavior* but replaces the IBus surface with the `CONCEPTS.md` interface.

---

## 2. Concept Mapping (the spine of this document)

This table is the primary connection between `ibus-table` and `CONCEPTS.md`. Every row is expanded
in a later section.

| `CONCEPTS.md` concept | ibus-table realization | Detail |
|---|---|---|
| **Engine** | Table-driven logic parameterized by one compiled database (§1). | §1, §3–§4 |
| **Input context** | One independent set of composition state per client destination: the current key run, any pre-committed segments, and the negotiated modes (§6.1). | §6 |
| **Key event** | Offered to the engine's input/editing state machine (§7). Only keys that build or edit the composition are consumed; the rest are reported unhandled. | §7 |
| **Handled / Unhandled** | Handled = the key changed composition state (input char, backspace, boundary commit). Unhandled = the engine does not use the key; the client processes it normally. | §7 |
| **Composition data** | Preedit text + candidate list, recomputed after each handled key (§6, §8). | §6, §8 |
| **Preedit text** | Concatenation of pre-committed segment phrases and the current key run, with optional per-key prompt substitution (§6.2). | §6.2 |
| **Preedit display position** | The end of the confirmed left segments — text before it is stable on commit; the current key run and any right segments after it are still provisional (§6.3). | §6.3 |
| ~~Auxiliary text~~ | Not in the model. ibus-table's only auxiliary content was a "position / total" candidate indicator, which is client UI (§6.4). | §6.4 |
| **Candidate list** | The full sorted result of the lookup query, capped at 100 (§8). Complete and unpaged. | §8 |
| **Select candidate** | Commit the phrase at an absolute index, apply learning, and continue or clear composition (§9). Paging and selection keys are client policy. | §9 |
| **Commit text** | A committed phrase, a literal key run committed verbatim, or an optionally full-width-converted character (§9, §11.4). | §9, §11 |
| **Reset** | Discard the current key run and pre-committed segments; produce empty composition data. Does not commit. | §7.6 |
| **Surrounding text / Delete surrounding text** | Unused. ibus-table performs no reconversion from client context. | §12 |
| **Forward key event** | Unused. ibus-table declines keys it does not use (unhandled) rather than forwarding. | §12 |
| **Focus / Activation** | Lifecycle hooks with no intrinsic commit or reset; commit-on-focus-out is a negotiated policy. | §12 |
| **Negotiation** | Engine options carried in the table's definition (Chinese variant mode, full-width modes, pinyin/suggestion modes) plus the client-policy fields the engine ignores (§3.1). | §3.1, §11 |

**Excluded as client / UI policy** (parsed where present, never acted on by the engine): candidate
selection keys, commit keys, page-up/down keys, candidate-list orientation, "always show lookup",
icons/symbols/status prompts, property menus for mode switching, and error sounds. These are called
out inline as they arise.

---

## 3. Source Table Format (data contract)

The source file is UTF-8 text. `###` begins a comment; blank lines are ignored. The file is a set of
named sections delimited by `BEGIN_XXX` / `END_XXX` markers on their own lines. The definition
section should precede the table data for reliable parsing. A file may begin with two legacy SCIM
header lines, which are ignored:

```
SCIM_Generic_Table_Phrase_Library_TEXT
VERSION_1_0
```

### 3.1 `BEGIN_DEFINITION` … `END_DEFINITION`

Each line is a key/value pair `KEY = VALUE` or `KEY == VALUE`. The key is case-insensitive; the value
keeps its case. Both separators are accepted; surrounding whitespace is stripped.

The **Role** column ties each field to the three information kinds from the intro: *Data/lookup*
drives compilation or the lookup query; *Engine option* is a behavior toggle (a negotiation value in
`CONCEPTS.md` terms); *Client policy* is parsed but ignored by a libpathime engine; *Metadata* is
descriptive.

| Key | Default | Role | Meaning for a libpathime engine |
|-----|---------|------|-------------------------------|
| `NAME`, `NAME.zh_CN/HK/TW` | *(NAME required)* | Metadata | Display name(s). |
| `UUID` | *(generated)* | Metadata | Method identity. |
| `SERIAL_NUMBER` | *(timestamp)*, `<2³²` | Data/lookup | Version; changing it invalidates any phrase cache (§5.4). |
| `LANGUAGES` | `""` | Data/lookup | BCP-47 locales. Determines `is_db_chinese` (a `zh` locale) and `is_db_cjk` (`zh`/`ja`/`ko`), which gate Chinese-variant filtering (§11.1) and full-width conversion (§11.4). |
| `LANGUAGE_FILTER` | *(none)* | Engine option | Default Chinese-variant mode `cm0`–`cm4` (§11.1). |
| `VALID_INPUT_CHARS` | `abc…z` | Data/lookup | The characters the engine accepts as key strokes. A key press not in this set (nor a wildcard/start char) is not composition input (§7.2). |
| `MAX_KEY_LENGTH` | `4` | Data/lookup | Longest key run per lookup; also an auto-commit boundary (§7.5). |
| `SINGLE_WILDCARD_CHAR` | `""` | Data/lookup | A char the user may type to match any one key; maps to SQL `_` (§8.1). |
| `MULTI_WILDCARD_CHAR` | `""` | Data/lookup | A char the user may type to match any key sequence; maps to SQL `%` (§8.1). |
| `AUTO_WILDCARD` | `TRUE` | Engine option | Append `%` to every query so a partial key run matches longer entries (§8.1). |
| `START_CHARS` | `""` | Data/lookup | If set, only these chars are valid as the *first* key stroke. |
| `RULES` | `""` | Data/lookup | Compound-phrase construction rules; also defines auto-commit boundaries (§3.5, §7.5). |
| `USER_CAN_DEFINE_PHRASE` | `FALSE` | Engine option | Enable learning of user-derived compound phrases; implies a `goucima` table exists (§4.3, §10.2). |
| `DYNAMIC_ADJUST` | `FALSE` | Engine option | Track selection frequency and reorder candidates (§10.1). |
| `NO_CHECK_CHARS` | `""` | Data/lookup | Characters excluded from frequency adjustment and phrase learning. |
| `LEAST_COMMIT_LENGTH` | `0` | Data/lookup | Alternative to `RULES` for auto-commit boundaries (§7.5). |
| `PINYIN_MODE` | `FALSE` | Engine option | Enable a pinyin fallback lookup (§11.2); implies a `pinyin` table. |
| `SUGGESTION_MODE` | `FALSE` | Engine option | Enable phrase prediction (§11.3); implies a `suggestion` table. |
| `AUTO_COMMIT` | `FALSE` | Engine option | Commit-to-preedit automatically when a key run reaches its boundary (§7.5). |
| `AUTO_SELECT` | `FALSE` | Engine option | Promote the first candidate automatically at boundaries and on ambiguity (§7.4). |
| `DEF_FULL_WIDTH_PUNCT` | `TRUE` | Engine option | Default full-width punctuation mode (§11.4). |
| `DEF_FULL_WIDTH_LETTER` | `FALSE` | Engine option | Default full-width letter mode (§11.4). |
| `LICENSE`, `AUTHOR`, `DESCRIPTION` | see below | Metadata | Descriptive strings (`LGPL`, `somebody`, `A IME under IBus Table`). |
| `ICON`, `SYMBOL`, `STATUS_PROMPT` | `ibus-table.svg`/`""`/`""` | Client policy | Presentation only; ignored. |
| `LAYOUT` | `us` | Client policy | Informational keyboard layout; ignored. |
| `COMMIT_KEYS` | `space` | Client policy | Which key commits the first candidate — a client binding onto *select candidate* (§9). Ignored by the engine. |
| `SELECT_KEYS` | `1,2,…,9,0` | Client policy | Keys that pick candidates 1–N of the visible page — client binding onto *select candidate*. Ignored. |
| `PAGE_UP_KEYS`, `PAGE_DOWN_KEYS` | see §3.1 note | Client policy | Candidate paging is client-side; ignored. |
| `ORIENTATION` | `TRUE` | Client policy | Candidate list layout; ignored. |
| `ALWAYS_SHOW_LOOKUP` | `TRUE` | Client policy | Whether to display candidates; a client display decision. |

Default paging keys are `Page_Up,KP_Page_Up,KP_Prior,minus` and
`Page_Down,KP_Page_Down,KP_Next,equal`. The `*_KEYS` values use IBus keyval names
(`space`, `Return`, `Page_Up`, `F1`, …). Because candidate navigation and selection are client
concerns in `CONCEPTS.md`, a libpathime engine parses these fields (to stay format-compatible) but
maps their *intent* onto the *select candidate* operation and its own commit behavior; it does not
implement key-specific selection or paging.

`BEGIN_CHAR_PROMPTS_DEFINITION` … `END_CHAR_PROMPTS_DEFINITION` may appear inside the definition
block (§3.4).

### 3.2 `BEGIN_TABLE` … `END_TABLE` and `BEGIN_TABLE_EXTRA` … `END_TABLE_EXTRA`

Each line is a phrase entry: three tab-separated columns plus an optional ignored fourth.

```
<tabkeys>\t<phrase>\t<freq>[\t<ignored>]
```

- `tabkeys` — the key sequence typed (case-sensitive), e.g. `aaaa`.
- `phrase` — the output text, e.g. `工`. The literal `NOSYMBOL` means "empty string".
- `freq` — a non-negative integer; higher ranks higher. `freq = 0` is valid ("lowest priority").

`TABLE_EXTRA` has identical format and is merged into the main phrase table during compilation; it
exists only to allow supplementary data in a separate region of the file.

### 3.3 `BEGIN_GOUCI` … `END_GOUCI` (word-formation codes)

Goucima (構詞碼) define the key component each character contributes when a compound phrase's lookup
key is derived (§10.2). Each line is two whitespace-separated fields:

```
<character>\t<goucima>
```

If the section is absent, goucima are derived automatically: for each single-character phrase, the
**longest** `tabkeys` sequence that produces exactly that character becomes its goucima.

### 3.4 `BEGIN_CHAR_PROMPTS_DEFINITION` … `END_CHAR_PROMPTS_DEFINITION`

Per-key visual prompts substituted into the preedit in place of the raw key character (§6.2). Each
line is `<key_char> <prompt_string>`, e.g. for Cangjie:

```
a 日
b 月
c 金
```

This changes the *content* of the plain preedit string the engine emits, so it is engine behavior,
not styling. The compiled form is stored as a `repr(dict)` string in the `char_prompts` attribute
(§4.1); a libpathime engine parses that literal back into a key→prompt map.

### 3.5 `RULES` Syntax (compound-phrase construction)

`RULES` is a semicolon-separated list. Each rule derives the lookup key of a compound phrase from the
goucima of its constituent characters.

```
RULES = ce2:p11+p12+p21+p22;ce3:p11+p21+p31+p32;ca4:p11+p21+p31+p-11
```

Each rule is `<condition>:<position-list>`:

- **Condition** — `ceN` applies to phrases of **exactly** N characters; `caN` applies to phrases of
  **N or more** characters. At most one `ca` rule, and it must carry the largest N (the catch-all).
- **Position list** — `+`-separated `pXY` tokens. `X` is the 1-based character index in the phrase
  (negative counts from the end; `-1` = last character); `Y` is the 1-based position within that
  character's goucima string.

A complete rule set covers every phrase length from 2 upward: a `ce2`, optional further `ceN`, and a
`caN` catch-all. Parsed form used internally:

```
{2: [(1,1),(1,2),(2,1),(2,2)],
 3: [(1,1),(2,1),(3,1),(3,2)],
 'above': 4,
 4: [(1,1),(2,1),(3,1),(-1,1)]}
```

`'above'` holds the `caN` threshold; that N's entry is also the rule for all longer phrases. Rules
serve two purposes: deriving user-phrase lookup keys (§10.2) and defining the key-run lengths at
which the engine auto-commits a segment to preedit (§7.5).

---

## 4. Compiled Database Schema (data contract)

The compiled system database is SQLite with `PRAGMA encoding = "UTF-8"` and
`PRAGMA case_sensitive_like = true`. No indexes are created by default (benchmarking showed no
speedup and doubled size). This schema is the interoperability contract: a libpathime engine that
opens an installed ibus-table `.db` must read these tables, and one that compiles source `.txt`
itself must produce them identically.

### 4.1 `ime` — configuration

```sql
CREATE TABLE IF NOT EXISTS main.ime (attr TEXT, val TEXT);
```

One row per definition-section key, attribute names lowercased (`name`, `max_key_length`,
`valid_input_chars`, …). `char_prompts` holds a Python-syntax dict literal (§3.4).

### 4.2 `phrases` — the lookup table

```sql
CREATE TABLE IF NOT EXISTS main.phrases
    (id INTEGER PRIMARY KEY, tabkeys TEXT, phrase TEXT,
     freq INTEGER, user_freq INTEGER);
```

`tabkeys`/`phrase` as in the source; `freq` from the source; `user_freq` is a usage counter, always
`0` in the system database. Rows are stored sorted by
`tabkeys ASC, phrase ASC, user_freq DESC, freq DESC, id ASC`.

### 4.3 `goucima` — word-formation codes

```sql
CREATE TABLE IF NOT EXISTS main.goucima (zi TEXT PRIMARY KEY, goucima TEXT);
```

One row per character. Present only when `USER_CAN_DEFINE_PHRASE = TRUE`.

### 4.4 `pinyin` — pinyin fallback (only if `PINYIN_MODE`)

```sql
CREATE TABLE IF NOT EXISTS main.pinyin (pinyin TEXT, zi TEXT, freq INTEGER);
```

Pinyin→character rows for §11.2. Tone digits 1–5 in the source are stored as the characters `!@#$%`
so tone-bearing syllables are LIKE-matchable and distinguishable from unaccented ones (§11.2). Source
data file (`pinyin_table.txt` or `.bz2`), one row per space-separated syllable:

```
<character>\t<syllable1 syllable2 …>\t<freq>
```

### 4.5 `suggestion` — phrase prediction (only if `SUGGESTION_MODE`)

```sql
CREATE TABLE IF NOT EXISTS main.suggestion (phrase TEXT, freq INTEGER);
```

Prediction entries for §11.3. Source data file (`phrase.txt` or `.bz2`): `<phrase> <freq>`.

---

## 5. User Database and On-Disk Learning (data contract)

The user database is a separate SQLite file in WAL mode, attached as schema `user_db`. It is the only
mutable per-user state and the substrate for adaptation (§10).

### 5.1 `user_db.phrases`

Same columns as `main.phrases`, with these semantics:

- For a `(tabkeys, phrase)` that also exists in `main.phrases`: `freq = 0` and `user_freq` counts how
  often the user has selected it.
- For a user-derived phrase (built via `RULES`, §10.2): `freq = -1` and `user_freq` starts at `1`.

### 5.2 `user_db.desc`

```sql
CREATE TABLE IF NOT EXISTS user_db.desc (name PRIMARY KEY, value);
```

Holds at least `('version', '1.00')` and `('create-time', <ISO datetime>)`. If the version does not
match `'1.00'`, or the `phrases` table has the wrong column count, the user database is renamed to a
timestamped backup and recreated, migrating any recoverable phrases.

### 5.3 Write batching

User-database writes use WAL; a checkpoint (`PRAGMA wal_checkpoint`) fires after more than 16
accumulated phrase updates or more than 30 seconds since the last sync. This is an implementation
durability detail, not observable behavior.

### 5.4 Phrase cache

An optional JSON cache sits beside the system database (`.cache` extension), mapping `tabkeys` to
serialized candidate lists. Its top-level `serial_number` must equal the database's `serial_number`
(§3.1); otherwise it is discarded. Written atomically (`.tmp` then rename). Purely a performance
optimization; a libpathime engine may implement, replace, or omit it.

---

## 6. Composition State and Composition Data

This section defines the per-input-context state and how it renders into the three `CONCEPTS.md`
composition fields.

### 6.1 Per-input-context state

Each input context holds:

- **current key run** — the actively edited key sequence, split into the portion that matched at
  least one candidate (`chars_valid`) and any trailing portion typed past the last match
  (`chars_invalid`).
- **pre-committed segments** — an ordered list of already-chosen `(key run, phrase)` pairs that have
  been staged into the preedit but not yet committed to the client, plus a cursor marking where new
  input is inserted among them (`cursor_precommit`).
- **negotiated modes** — Chinese-variant mode, full-width letter/punct flags, pinyin/suggestion mode
  on/off. These persist across compositions but belong to the input context (§11).

Because all of this depends on partially entered text, it is per-input-context state, exactly as
`CONCEPTS.md` requires — never global to the engine.

### 6.2 Preedit text

The preedit string is assembled as:

```
(phrases of pre-committed segments left of the cursor)
  + (current key run, displayed)
  + (phrases of pre-committed segments right of the cursor)
```

The "current key run, displayed" is `chars_valid` — with each character optionally replaced by its
`char_prompts` prompt (§3.4) — followed by `chars_invalid` unchanged. The result is a single plain
Unicode string, as `CONCEPTS.md` mandates; there is no styling, and the invalid-character coloring
ibus-table applies is a client decision.

### 6.3 Preedit display position

`CONCEPTS.md` gives preedit an internal display position: text before it is not expected to change on
commit; text after it is still provisional. The faithful mapping here is:

> **display position = end of the pre-committed segments to the left of the cursor.**

Everything before that point is a confirmed selection; the current key run (which is still being
composed and will be replaced by whichever candidate is chosen) and any pre-committed segments to the
right of the cursor sit after the position, as provisional text. In the common case — typing a fresh
key run with no staged segments — the position is 0, meaning the entire preedit is provisional, which
is correct: any candidate can replace it.

*Impedance note:* ibus-table additionally lets the user move a caret among the pre-committed segments
and edit in the middle. `CONCEPTS.md` models only a single display position and no editing caret, so
that mid-preedit caret is flattened away; a libpathime engine may keep the internal cursor to
reproduce editing behavior but exposes only the single display position.

### 6.4 Auxiliary text — not in the model

`libpathime` has **no auxiliary text field**. `pathime_composition_t` carries a preedit and a
candidate list, and nothing else. This section stays to record that the table engine was checked
before the field was removed, and needs nothing from it.

ibus-table's auxiliary content is `get_aux_strings()` (`refs/ibus-table/engine/table.py:1732`),
which is the raw input characters mapped through `char_prompts` (§3.4), plus a `current / total`
candidate-position indicator. Both are already accounted for elsewhere:

- The key run **is preedit text**, with its prompt substitution, exactly as §6.2 already specifies.
  That is the general rule `docs/CONCEPTS.md` now states for every engine — the preedit is what the
  user typed in the script they are composing in — and a table method's prompt characters are that
  script, the same way zhuyin is Bopomofo's.
- The position indicator is client UI, derived from the candidate cursor and the candidate count,
  both of which are published as numbers.

Should a table ever warrant a genuine composition-level hint with nowhere else to go,
`pathime_composition_t` carries a `struct_size` and the field can return as a trailing member
without breaking a compiled client.

---

## 7. Key-Event Behavior (engine-owned)

The engine offers each **key event** to its input/editing state machine and reports **handled** or
**unhandled**. Only keys that build or edit the composition are handled here; candidate selection,
paging, and the choice of which key commits are client policy delivered through the *select candidate*
operation and the engine's commit operations (§9), not through this state machine.

Key *release* events are always unhandled.

The transitions below assume the engine is active for the input context (`CONCEPTS.md` activation). A
table that also supports a pass-through "direct" mode (full-width-only) is treating that as a mode
toggle; the toggle itself is negotiation, and in pass-through the engine simply reports keys unhandled
after the optional full-width conversion of §11.4.

### 7.1 Empty-composition pass-through

If there is no composition and the key is not a valid input char (nor a wildcard char, nor a start
char when `START_CHARS` is set) and no modifiers are held: the engine does not compose. It either
reports the key **unhandled** (client inserts it normally) or, if a full-width mode applies to it,
commits the converted character (§11.4) and reports **handled**.

### 7.2 Valid input character (the core transition)

When the key is in `VALID_INPUT_CHARS` (or is a configured wildcard char), the engine:

1. If the current `chars_valid` has reached its commit boundary (§7.5), first auto-commit the current
   segment to preedit (staging it as a pre-committed segment) before taking the new character.
2. Append the character to the current key run and re-run the lookup (§8) to produce a fresh
   candidate list.
3. If the run now matches nothing: if `AUTO_SELECT` was in effect and a previous match existed, drop
   the just-typed character, commit the previous first candidate, and reprocess the character as the
   start of a new run. Otherwise the character becomes part of `chars_invalid`.
4. If exactly one candidate remains, `AUTO_COMMIT` is on, and it is an exact match: commit it
   immediately (§9).

The key is **handled**; composition data is updated (new preedit + candidate list).

### 7.3 Backspace

- Empty composition, suggestion mode active: leave suggestion mode; **handled**.
- Empty composition otherwise: **unhandled** (client deletes normally).
- Otherwise: remove the last character — from `chars_invalid` first, else from `chars_valid`,
  pulling the most recent pre-committed segment back into the current run if the run empties.
  Re-run the lookup; **handled**.

Forward `Delete` and the segment-editing motions (`Left`/`Right`, `Ctrl+Left/Right`,
`Ctrl+Backspace`, `Ctrl+Delete`) move or delete among the pre-committed segments and the internal
edit caret (§6.3). They are **handled** when a composition exists and update composition data. These
exist to support editing a multi-segment preedit; a libpathime engine may implement them but they are
not required for basic table input.

### 7.4 Commit-the-literal-run keys (`Return`)

- Empty composition: **unhandled**.
- `AUTO_SELECT` on: commit the first candidate, then let `Return` through (report unhandled so the
  client also gets the newline).
- Otherwise: commit the current key run **verbatim as typed** (not a candidate) — this is how a user
  emits the raw letters when no conversion is wanted. **Handled**.

ibus-table binds this to `Return`/`KP_Enter`; a libpathime client may bind any key. The essential
engine operation is "commit the literal input."

### 7.5 Auto-commit to preedit (segment boundaries)

When a new input character arrives and the current key run has reached a boundary, the engine stages
the current first candidate: it appends `(current key run, first-candidate phrase)` as a pre-committed
segment and clears the current run. This is *committing to preedit*, distinct from committing to the
client — the text moves into the confirmed-left portion of the preedit (§6.3) but is not yet sent to
the application.

The boundary is reached when the run length equals `MAX_KEY_LENGTH`, or equals one of the
`RULES`-derived segment lengths. Those lengths come from `RULES`: the output length of each `ceN`
rule for N from 2 up to (but not including) the `caN` threshold. With no rules, the boundaries are
every length from `LEAST_COMMIT_LENGTH` through `MAX_KEY_LENGTH` when `LEAST_COMMIT_LENGTH > 0`;
otherwise there are none. `AUTO_COMMIT` controls whether reaching `MAX_KEY_LENGTH` stages to preedit
automatically.

### 7.6 Trailing non-input characters and reset

- **Other Unicode character** (not an input/editing key): commit any pending composition — the
  current candidate, or the literal run, depending on the table's commit-on-invalid policy — then
  commit the character itself (full-width converted per §11.4 if applicable). **Handled**.
- **Key with no Unicode representation** (function keys, etc.): **unhandled**.
- **Reset** (`CONCEPTS.md`): discard the current run and all pre-committed segments and emit empty
  composition data. Reset does **not** commit; a client that wants to keep the preedit must commit
  first (this matches `CONCEPTS.md` reset semantics).

---

## 8. Candidate List and Lookup

The **candidate list** is the complete, ordered, unpaged list `CONCEPTS.md` describes — the engine
never divides it into pages. It is the full result of one lookup query, sorted (§8.2), and capped at
**100** entries.

### 8.1 Lookup query and pattern

Candidates come from:

```sql
SELECT tabkeys, phrase, freq, user_freq FROM main.phrases
WHERE tabkeys LIKE :pattern ESCAPE :escapechar
```

When `USER_CAN_DEFINE_PHRASE` or `DYNAMIC_ADJUST` is on, `user_db.phrases` rows are `UNION ALL`'d in;
a `(tabkeys, phrase)` present in both databases is merged by taking the max of each of `freq` and
`user_freq`.

Pattern construction from the current key run:

1. Start from the typed key run.
2. Double the escape char where it appears literally.
3. Escape literal `%` and `_` unless they are the configured wildcard chars.
4. Replace `SINGLE_WILDCARD_CHAR` with SQL `_`; replace `MULTI_WILDCARD_CHAR` with SQL `%`.
5. If `AUTO_WILDCARD`, append `%`.

The escape char is `!` (or `@`/`#` if `!`/`@` collide with the wildcard config).

### 8.2 Candidate ordering (`best_candidates`)

Sort by this key (descending unless noted); this order *is* the meaning of the candidate list's
positions, which the client uses for *select candidate*:

1. Exact `tabkeys` match (typed run equals the candidate's `tabkeys`) — first.
2. Chinese tables only: penalize pinyin tone-suffixed entries when the tone-stripped typed string
   matches (§11.2).
3. `user_freq` descending (learned preference).
4. Chinese variant boost: `cm2` prefers simplified, `cm3` prefers traditional (§11.1).
5. `freq` descending.
6. Key-run length ascending (shorter `tabkeys` first).
7. `tabkeys` alphabetical.
8. Big5 code of the first character (Cangjie/Quick tables only).
9. Unicode code point of the first character.

Truncate to 100.

---

## 9. Select Candidate and Commit

**Select candidate** (`CONCEPTS.md`): the client identifies a candidate by its **absolute index** in
the current list (§8) and asks the engine to choose it. Which key the client bound to that — `space`
for index 0, digit keys for a page offset, a mouse click — is client policy; the engine sees only the
completed selection.

On selection the engine:

1. **Commits** the chosen phrase as **commit text** to the client (or, in the phrase-building flow,
   stages it — see below).
2. Applies **learning**: `DYNAMIC_ADJUST` bumps the `(tabkeys, phrase)` `user_freq` (§10.1), and
   `USER_CAN_DEFINE_PHRASE` may derive a compound phrase from this selection plus the previous one
   (§10.2).
3. Produces fresh composition data for whatever key run remains — commonly empty composition after a
   full commit, or a continued composition when a partial candidate was chosen.

**Phrase-building variant.** ibus-table also supports selecting a candidate *into the preedit* (bound
to `Ctrl`+selection) rather than committing it — this stages a segment so several character
selections compose one phrase before a final commit. In `CONCEPTS.md` terms both are *select
candidate*; whether the selection finalizes (commit text) or continues the composition (adds a
pre-committed segment) is engine behavior driven by the table's phrase-building configuration, not a
separate concept. The `Ctrl` binding is client policy.

The engine must ignore a selection whose index refers to a superseded candidate list (`CONCEPTS.md`
requires the client not to send one, but the engine should be defensive).

---

## 10. Adaptation (learning)

### 10.1 Dynamic adjustment

With `DYNAMIC_ADJUST = TRUE`, each selection of a `(tabkeys, phrase)` present in `main.phrases`
increments its `user_freq` in `user_db.phrases` (inserting with `user_freq = 1` if absent). Higher
`user_freq` ranks it earlier next time (§8.2, key 3). This is the mechanism behind the candidate list
reordering to match usage.

### 10.2 User-derived phrases

With `USER_CAN_DEFINE_PHRASE = TRUE` on a Chinese table, consecutive single-character selections are
combined into a compound phrase:

1. Take the previous committed phrase plus the current candidate.
2. Derive the compound's lookup key by applying the parsed `RULES` (§3.5) against each character's
   goucima (§4.3).
3. If the compound is not already in `main.phrases`, insert it into `user_db.phrases` with
   `freq = -1`, `user_freq = 1`.

Characters in `NO_CHECK_CHARS` (and the built-in Chinese no-check set) are excluded. This is why the
`goucima` table and `RULES` are part of the data contract even though they never appear in a basic
lookup.

---

## 11. Engine Options (negotiated behaviors)

These map to `CONCEPTS.md` **negotiation** — engine-specific options exchanged as data, never a
menu or property UI. Their *defaults* come from the definition section (§3.1); their live values live
in the input context (§6.1).

### 11.1 Chinese variant mode

Applies only when `is_db_chinese`. Modes `cm0`–`cm4` filter or reorder by the simplified/traditional
classification of a candidate's first character:

| Mode | Behavior |
|------|----------|
| `cm0` | Keep only simplified. |
| `cm1` | Keep only traditional. |
| `cm2` | Keep all; boost simplified in the sort (§8.2 key 4). |
| `cm3` | Keep all; boost traditional. |
| `cm4` | No filter or boost. |

Classification maps a code point to a bitmask (bit 0 = simplified, bit 1 = traditional) via a
Unicode-range table derived from `tools/Unihan_Variants.txt`. A clean-room engine must regenerate an
equivalent table from the same Unicode data (§13.4), not copy the reference's generated table.

### 11.2 Pinyin mode

Only when `PINYIN_MODE = TRUE`. When active, the engine queries the `pinyin` table instead of
`phrases`, with `MAX_KEY_LENGTH` fixed at 7 and `%%` appended to the typed keys. Tone digits are
encoded `!@#$%` for tones 1–5 (a syllable `zhi1` is stored `zhi!`); a tone-suffixed entry sorts below
an exact unaccented match (§8.2 key 2).

### 11.3 Suggestion mode

Only when `SUGGESTION_MODE = TRUE`. When active, candidates come from the `suggestion` table, matched
as a prefix of the most recently committed character or phrase, sorted longest-match first, then
`freq` descending, then Big5 (Cangjie/Quick), then code point.

### 11.4 Full-width conversion

Applies only when `is_db_cjk` and the relevant mode (`DEF_FULL_WIDTH_LETTER` / `DEF_FULL_WIDTH_PUNCT`,
as toggled in the context) is on. ASCII characters the engine would otherwise pass through or commit
are mapped to fullwidth equivalents, with these punctuation overrides:

| ASCII | Output | Note |
|-------|--------|------|
| `<` `>` | `《` `》` | Double angle brackets |
| `[` `]` | `「` `」` | Corner brackets |
| `{` `}` | `『` `』` | White corner brackets |
| `\` | `、` | Ideographic comma |
| `^` | `…` | Ellipsis |
| `_` | `——` | Em-dash pair |
| `` ` `` | `` ` `` | Unchanged |
| `~` | `～` | Fullwidth tilde |
| `.` | `。` / `．` | `。` at sentence start, `．` otherwise |
| `"` | `"` / `"` | Alternates open/close |
| `'` | `'` / `'` | Alternates open/close |

`CONCEPTS.md` treats full-width conversion as an engine option, not model machinery. A libpathime
engine may offer it as a negotiated transform on committed characters; it is not required for table
lookup and can be omitted from a minimal engine.

---

## 12. Unused and Excluded Concepts

Relative to `CONCEPTS.md`, ibus-table exercises only part of the interface:

- **Surrounding text / delete surrounding text** — unused. ibus-table never reads client context or
  requests deletion; there is no reconversion-from-context. A libpathime table engine can report
  surrounding text unsupported.
- **Forward key event** — unused. Keys the engine does not use are reported **unhandled**; ibus-table
  does not synthesize forwarded events.
- **Focus / Activation** — lifecycle hooks with no intrinsic commit or reset. Commit-on-focus-out is
  a negotiated policy, not implied by focus.
- **Auxiliary text** — not in the model at all; the table engine needs nothing from it (§6.4).

Excluded as client/UI policy (parsed where they appear in data, never acted on): candidate selection
keys, commit keys, page keys, candidate orientation, "always show lookup", icon/symbol/status prompt,
property menus for switching modes, and error sounds.

---

## 13. Clean-Room Implementation Checklist

### 13.1 Data path (to share ibus-table-chinese)

1. **Parse source `.txt`** — sections of §3, including `char_prompts`, `goucima`, and `RULES`; skip
   SCIM headers and `###` comments; categorize each definition key by its Role (§3.1).
2. **Compile to / read the SQLite schema of §4** — `ime`, `phrases`, `goucima`, `pinyin`,
   `suggestion`, sorted as specified, with the exact pragmas (§13.3). Reading an installed `.db`
   directly is the fast path to interop; compiling `.txt` covers `ibus-table-chinese`'s source-only
   distribution.
3. **Attach the user database** (§5) as a second schema for learning and merged lookups.

### 13.2 Behavior path (to expose CONCEPTS.md)

1. **Lookup** — build the LIKE pattern (§8.1), union system + user rows, merge duplicates.
2. **Order** — the multi-key sort of §8.2, capped at 100 → the candidate list.
3. **Compose** — the key-event state machine of §7 maintaining the state of §6.1, emitting preedit
   (§6.2), display position (§6.3), and candidate list after each handled key.
4. **Select / commit** — §9, including learning (§10) and, for phrase tables, the `RULES`-based
   compound derivation (§3.5, §10.2).
5. **Options** — Chinese mode, pinyin/suggestion modes, and (optionally) full-width conversion as
   negotiated values (§11).

### 13.3 SQLite pragmas

```sql
PRAGMA encoding = "UTF-8";
PRAGMA case_sensitive_like = true;
PRAGMA page_size = 4096;
PRAGMA cache_size = 20000;
PRAGMA temp_store = MEMORY;
PRAGMA synchronous = NORMAL;
PRAGMA busy_timeout = 5000;
```

Attach the user database as its own schema and set `journal_mode = WAL` on it.

### 13.4 Data to regenerate, not copy

- **Chinese variant table** (§11.1) — regenerate the code-point→bitmask table from
  `tools/Unihan_Variants.txt` / upstream Unicode data.
- **Full-width table** (§11.4) — regenerate the half↔full mapping from Unicode data.
- **Pinyin tone encoding** (§11.2) — the `1..5 → !@#$%` substitution is a fixed convention, not
  copyrightable data; reproduce it directly.

---

## 14. Reference Tables (test data in `refs/ibus-table-chinese`)

`ibus-table-chinese` ships source `.txt` only; compile them (§13.1) to exercise the engine. Useful
shapes:

| Table | Path | Exercises |
|-------|------|-----------|
| Wubi-Jidian | `tables/wubi-jidian/wubi-jidian86.txt` | `RULES`, `USER_CAN_DEFINE_PHRASE`, `PINYIN_MODE`, `SUGGESTION_MODE`, `DYNAMIC_ADJUST`, auto-derived goucima, `MAX_KEY_LENGTH = 4`. |
| Cangjie 5 | `tables/cangjie/cangjie5.txt` | Char prompts (the 24 radicals as printed on a Cangjie keyboard), `MAX_KEY_LENGTH = 5`, `LANGUAGE_FILTER = cm1`, no rules, **no** wildcard declarations. |
| Stroke5 | `tables/stroke5/stroke5.txt` | `AUTO_WILDCARD` with explicit `SINGLE_WILDCARD_CHAR`/`MULTI_WILDCARD_CHAR`, `AUTO_COMMIT` with `AUTO_SELECT = FALSE`, non-alpha `VALID_INPUT_CHARS` (`nm,./`), stroke-name char prompts. |
| Zhuyin | `tables/zhuyin.txt`, `tables/zhuyin-big.txt` | Phonetic keys, larger phrase set, `USER_CAN_DEFINE_PHRASE` **without** `RULES`. |

### Minimal synthetic table (no goucima, no rules)

```
BEGIN_DEFINITION
NAME = Example
UUID = 00000000-0000-0000-0000-000000000000
SERIAL_NUMBER = 20240101
LANGUAGES = en
VALID_INPUT_CHARS = abc
MAX_KEY_LENGTH = 2
AUTO_COMMIT = FALSE
END_DEFINITION

BEGIN_TABLE
a	α	1000
b	β	1000
c	γ	1000
ab	αβ	500
END_TABLE
```

### Deployment paths (for locating installed test data)

| Path | Contents |
|------|----------|
| `/usr/share/ibus-table/tables/*.db` | Compiled system databases |
| `~/.local/share/ibus-table/tables/*.db` | User databases |
| `~/.local/share/ibus-table/tables/*.cache` | Phrase caches |
| `/usr/share/ibus-table/data/pinyin_table.txt.bz2` | Pinyin source data |
| `/usr/share/ibus-table/data/phrase.txt.bz2` | Suggestion source data |
</content>
</invoke>
