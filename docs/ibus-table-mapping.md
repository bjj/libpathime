# ibus-table — mapping

The per-backend mapping for the table engine, the peer of `docs/libhangul-mapping.md`,
`docs/anthy-mapping.md` and `docs/pyzy-mapping.md`. Like those it describes how a
living external thing connects to this library, and like those it is what to read
before upgrading the submodule it describes.

Unlike those three it maps a **data format** rather than a C API, because there is
no library to link: `ibus-table` is Python, and what this engine shares with it is
`engines/ibus-table-chinese`'s source tables and the compiled SQLite databases both
programs read and write. So §3, §4 and §5 below are a contract in the literal
sense — change them and a table this library compiles stops being one ibus-table
can open, or vice versa.

**The section numbers here are frozen.** Many comments under `src/engines/table/`
cite them by number — `§8.1` at the lookup query, `§3.1` at the declaration,
`§11.1` at variant classification, and so on — so a section is never renumbered
and a number is never reused. The sequence has gaps; they stay gaps.

What the format specifies is below. Why the engine behaves as it does, where that
is a choice this library made rather than something the format dictates, is stated
at the point it arises — inline here where it is a property of the mapping, and in
`src/engines/table/README.md` where it is a property of the implementation.

---

## 2. Concept Mapping

This table is the primary connection between `ibus-table` and `CONCEPTS.md`.
The **Detail** column points at whichever is authoritative for that row: a section
below where this document describes the format, and `src/engines/table/` where the
answer is the implementation.

| `CONCEPTS.md` concept | ibus-table realization | Detail |
|---|---|---|
| **Engine** | Table-driven logic parameterized by one compiled database. One engine id covers every method, because they differ only in the table loaded. | §3–§4 |
| **Input context** | One independent set of composition state per client destination: the current key run, any pre-committed segments, and the negotiated modes. | `table_backend.cc` |
| **Key event** | Offered to the engine's input/editing state machine. Only keys that build or edit the composition are consumed; the rest are reported unhandled. | `table_backend.cc`, `process_key()` |
| **Handled / Unhandled** | Handled = the key changed composition state (input char, backspace, boundary commit). Unhandled = the engine does not use the key; the client processes it normally. | `table_backend.cc` |
| **Composition data** | Preedit text + candidate list, recomputed after each handled key. | §8, `table_backend.cc` |
| **Preedit text** | Concatenation of pre-committed segment phrases and the current key run, with optional per-key prompt substitution (§3.4). The prompts are keycap legends, so the substitution is what the preedit clause permits under "key legends"; the cost is that the preedit is then not the literal text a commit produces. | §3.4 |
| **Preedit display position** | The end of the confirmed left segments — text before it is stable on commit, the current key run after it is still provisional. | `table_backend.cc`, `publish()` |
| ~~Auxiliary text~~ | Not in the model: this API has no auxiliary text. ibus-table's only auxiliary content is a "position / total" candidate indicator — published here as the candidate cursor and count — and the `#: <code>` display that makes user-defined phrases discoverable, a feature this library does not implement (§3.1, `USER_CAN_DEFINE_PHRASE`). | §12 |
| **Candidate list** | The full sorted result of the lookup query, capped at 100. Complete and unpaged. | §8 |
| **Select candidate** | Commit the phrase at an absolute index, apply learning, and continue or clear composition. Paging and selection keys are client policy. | `table_backend.cc`, `select_candidate()` |
| **Commit text** | A committed phrase, a literal key run committed verbatim, or an optionally full-width-converted character. | §11.4, `src/punctuation.h` |
| **Reset** | Discard the current key run and pre-committed segments; produce empty composition data. Does not commit. | `table_backend.cc`, `reset()` |
| **Surrounding text / Delete surrounding text** | Unused. ibus-table performs no reconversion from client context. | §12 |
| **Forward key event** | Unused. ibus-table declines keys it does not use (unhandled) rather than forwarding. | §12 |
| **Focus / Activation** | Not in the model. ibus-table's lifecycle hooks carry no intrinsic commit or reset. | §12 |
| **Negotiation** | Engine options carried in the table's definition (Chinese variant mode, full-width modes, pinyin/suggestion modes) plus the client-policy fields the engine ignores. | §3.1, §11 |

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
| `VALID_INPUT_CHARS` | `abc…z` | Data/lookup | The characters the engine accepts as key strokes. A key press not in this set (nor a wildcard/start char) is not composition input. |
| `MAX_KEY_LENGTH` | `4` | Data/lookup | Longest key run per lookup; also a segment boundary. |
| `SINGLE_WILDCARD_CHAR` | `""` | Data/lookup | A char the user may type to match any one key; maps to SQL `_` (§8.1). |
| `MULTI_WILDCARD_CHAR` | `""` | Data/lookup | A char the user may type to match any key sequence; maps to SQL `%` (§8.1). |
| `AUTO_WILDCARD` | `TRUE` | Engine option | Append `%` to every query so a partial key run matches longer entries (§8.1). |
| `START_CHARS` | `""` | Data/lookup | If set, only these chars are valid as the *first* key stroke. |
| `RULES` | `""` | Data/lookup | Compound-phrase construction rules; also defines segment boundaries (§3.5). |
| `USER_CAN_DEFINE_PHRASE` | `FALSE` | Engine option | Enable learning of user-derived compound phrases; implies a `goucima` table exists (§4.3). Not implemented. |
| `DYNAMIC_ADJUST` | `FALSE` | Engine option | Track selection frequency and reorder candidates. |
| `NO_CHECK_CHARS` | `""` | Data/lookup | Characters excluded from frequency adjustment and phrase learning. |
| `LEAST_COMMIT_LENGTH` | `0` | Data/lookup | Alternative to `RULES` for segment boundaries. |
| `PINYIN_MODE` | `FALSE` | Engine option | Enable a pinyin fallback lookup (§11.2); implies a `pinyin` table. |
| `SUGGESTION_MODE` | `FALSE` | Engine option | Enable phrase prediction (§11.3); implies a `suggestion` table. |
| `AUTO_COMMIT` | `FALSE` | Engine option | Commit-to-preedit automatically when a key run reaches its boundary. |
| `AUTO_SELECT` | `FALSE` | Engine option | Promote the first candidate automatically at boundaries and on ambiguity. |
| `DEF_FULL_WIDTH_PUNCT` | `TRUE` | Engine option | Default full-width punctuation mode (§11.4). |
| `DEF_FULL_WIDTH_LETTER` | `FALSE` | Engine option | Default full-width letter mode (§11.4). |
| `LICENSE`, `AUTHOR`, `DESCRIPTION` | see below | Metadata | Descriptive strings (`LGPL`, `somebody`, `A IME under IBus Table`). |
| `ICON`, `SYMBOL`, `STATUS_PROMPT` | `ibus-table.svg`/`""`/`""` | Client policy | Presentation only; ignored. |
| `LAYOUT` | `us` | Client policy | Informational keyboard layout; ignored. |
| `COMMIT_KEYS` | `space` | Client policy | Which key commits the first candidate — a client binding onto *select candidate*. Ignored by the engine. |
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
key is derived — see `USER_CAN_DEFINE_PHRASE` in §3.1. Each line is two whitespace-separated fields:

```
<character>\t<goucima>
```

If the section is absent, goucima are derived automatically: for each single-character phrase, the
**longest** `tabkeys` sequence that produces exactly that character becomes its goucima.

### 3.4 `BEGIN_CHAR_PROMPTS_DEFINITION` … `END_CHAR_PROMPTS_DEFINITION`

Per-key visual prompts substituted into the preedit in place of the raw key character. Each
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
serve two purposes: deriving user-phrase lookup keys and defining the key-run lengths at
which the engine stages a segment into the preedit.

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
mutable per-user state and the substrate for adaptation.

### 5.1 `user_db.phrases`

Same columns as `main.phrases`, with these semantics:

- For a `(tabkeys, phrase)` that also exists in `main.phrases`: `freq = 0` and `user_freq` counts how
  often the user has selected it.
- For a user-derived phrase (built via `RULES`): `freq = -1` and `user_freq` starts at `1`.

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

## 11. Engine Options (negotiated behaviors)

These map to `CONCEPTS.md` **negotiation** — engine-specific options exchanged as data, never a
menu or property UI. Their *defaults* come from the definition section (§3.1); their live values live
in the input context.

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
Unicode-range table derived from `refs/ibus-table/tools/Unihan_Variants.txt`. A clean-room engine must regenerate an
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
- **Focus / Activation** — not in the model, and nothing is lost by their absence: ibus-table's
  lifecycle hooks carry no intrinsic commit or reset, and what `do_focus_in_id()` does record —
  which application holds focus — is used only to label debug output (`table.py:3149`).
- **Auxiliary text** — not in the model at all: this API has no auxiliary-text
  field. ibus-table's `get_aux_strings()` carries the current key run, which this
  document already specifies as preedit text (§3.4, §8), plus a "position / total"
  candidate counter, which `CONCEPTS.md` publishes as the candidate cursor and the
  candidate count. Nothing is left for the field to hold.

Excluded as client/UI policy (parsed where they appear in data, never acted on): candidate selection
keys, commit keys, page keys, candidate orientation, "always show lookup", icon/symbol/status prompt,
property menus for switching modes, and error sounds.

---

## 13.3 SQLite pragmas

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

---

## 13.4 Data to regenerate, not copy

- **Chinese variant table** (§11.1) — regenerate the code-point→bitmask table from
  `refs/ibus-table/tools/Unihan_Variants.txt` / upstream Unicode data.
- **Full-width table** (§11.4) — regenerate the half↔full mapping from Unicode data.
- **Pinyin tone encoding** (§11.2) — the `1..5 → !@#$%` substitution is a fixed convention, not
  copyrightable data; reproduce it directly.

---

## Impedance mismatches

What this library does *not* reproduce, and what it costs. The other three mapping
docs end the same way.

- **The mid-preedit caret is flattened away.** ibus-table lets the user move a
  caret among pre-committed segments and edit in the middle; `cursor_precommit` is
  what that is for. `docs/CONCEPTS.md` has no cursor inside a span, so Left, Right,
  Home and End are declined while composing and `Composition::tail` is always empty
  for this engine. This is the largest thing given up, and unlike the same
  flattening in anthy and pyzy it removes a *documented ibus-table feature* rather
  than an incidental capability.

- **Client-policy declarations are parsed and ignored.** `SELECT_KEYS`,
  `PAGE_UP_KEYS`, `ORIENTATION`, "always show lookup", icons, status prompts and
  error sounds are read (so compilation can write them back) and never acted on.
  Candidate presentation and key binding are the client's domain in this API, not
  the engine's. A table author's choices about them are preserved in the `.db` and
  have no effect here.

- **Committing a run and displaying it are two renderings, not one.** Where a table
  declares char prompts, the preedit shows the key legends (Cangjie `a` reads 日)
  and Return commits the letters. Four of the five shipped tables do this. The
  header's preedit clause permits it, because `BEGIN_CHAR_PROMPTS` is the *keycap
  legend* — cangjie5 maps `a`→日, `b`→月 … `y`→卜, which is what is printed on a
  Cangjie keyboard — so the preedit is the physical keyboard rendered back at the
  user, and that is the feature rather than a defect in it. What it costs: a client
  reading the preedit as literal committable text is wrong for those four tables
  and has to know it.

- **Full-width punctuation follows ibus-pinyin, not §11.4.** The two references
  disagree on `^`, `[`, `<` and the period. One option cannot mean two things
  across the two Chinese engines, so `src/punctuation.*` is shared with pyzy and
  §11.4's table is not implemented as written. Named here because §11.4 is still
  the accurate description of *ibus-table*, which is what this document is for.

- **Compiled tables are not byte-comparable with `ibus-table-createdb` output.**
  The schema is identical and either program can open the other's databases, but
  this library's compiler additionally trims entries to a font's glyph coverage,
  transfers frequencies from a usage-ranked table, and derives a `z` wildcard where
  the source declared none. The last is a behaviour difference rather than a data
  one — ibus-table's lookup has no position rule, so under ibus-table a *leading*
  `z` would become a wildcard too.

- **Pinyin mode (§11.2) and suggestion mode (§11.3) are not implemented.** Their
  source data ships with ibus-table rather than with ibus-table-chinese, so the
  tables are created empty when a table declares the mode.
  `PATHIME_OPT_TABLE_PINYIN_FALLBACK` reports itself unsupported unless the opened
  database really carries the rows, which an ibus-table-compiled one would.
