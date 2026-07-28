# ibus-table: Configuration Option Catalog

This document catalogs every *configuration option* exposed by `ibus-table` at the API level, as
input for deciding what `libpathime` should expose as options (common across engines vs.
engine-specific). It is pure fact-finding — no libpathime API or UI design is proposed here.

`libpathime` implements its own table engine rather than wrapping `ibus-table`, which is Python
(`docs/ibus-table-spec.md`, `TODO.md`). So this catalog is not a survey of a foreign
configuration surface we must accommodate: it is the option space of a first-class engine of ours,
and the options round is free to keep, rename, or drop any of it.

Sources: `refs/ibus-table` (engine source, GSettings schema) and `refs/ibus-table-chinese` (real
table data, used to see which options tables actually set). `docs/ibus-table-spec.md` already
documents the source `.txt` format, compiled SQLite schema, and engine behavior in full; this
document does not re-derive that — it pulls out only the option surface and adds the runtime
(GSettings) layer and the resolution order between the two, which the spec does not cover.

Two independent option surfaces exist:

- **§1 Table-defined options** — declared per-table, either in the source `.txt`
  `BEGIN_DEFINITION`/`END_DEFINITION` block or as rows in the compiled `main.ime` table (same data,
  two representations; see `ibus-table-spec.md` §3.1/§4.1). Read via `ImeProperties`
  (`refs/ibus-table/engine/tabsqlitedb.py:81-119`), a generic `attr → val` string cache with **no
  fixed schema** — any key/value pair round-trips through the compiler untouched, whether or not the
  engine ever reads it back by name (§4 documents fields that do not).
- **§2 GSettings runtime options** — declared in
  `refs/ibus-table/org.freedesktop.ibus.engine.table.gschema.xml`, one schema
  (`org.freedesktop.ibus.engine.table`) shared by every table, read via `Gio.Settings` in
  `refs/ibus-table/engine/table.py`.

### The resolution order (why this matters for classification)

For a sizeable subset of options, `table.py`'s constructor (`refs/ibus-table/engine/table.py:380-672`)
resolves a *single* effective value from **up to three sources**, checked in this order:

1. **User's GSettings value**, if the user has ever explicitly set it
   (`gsettings.get_user_value(key)`, non-`None`) — a per-user override that beats everything.
2. **The table's own definition-section value**, if the table declares one
   (`database.ime_properties.get(key)`, non-empty) — a per-table default.
3. **The GSettings schema's built-in default** (`gsettings.get_value(key)`) — a global fallback used
   when neither of the above is present.

Concretely, for `autocommit` (`table.py:509-518`):

```python
auto_commit = gsettings.get_user_value('autocommit')       # 1. user override
if auto_commit is None:
    if database.ime_properties.get('auto_commit'):
        auto_commit = database.ime_properties.get('auto_commit').lower() == 'true'  # 2. table default
    else:
        auto_commit = gsettings.get_value('autocommit')     # 3. schema default
```

This same three-tier pattern is used for `dynamicadjust`, `singlewildcardchar`,
`multiwildcardchar`, `autowildcard`, `autocommit`, `autoselect`, `alwaysshowlookup`,
`chinesemode` (via `it_util.get_default_chinese_mode`, `it_util.py:144-`, which checks the
database value, falling back to `LC_CTYPE`, before the GSettings layer even applies), the four
full-width flags (each stored as a `[user, table]` pair, `table.py:479-507`), and keybindings
(`get_default_keybindings`, `it_util.py:214-`, merges GSettings schema defaults with table
`SELECT_KEYS`/`COMMIT_KEYS`/`PAGE_*_KEYS`, then a full per-key user GSettings override is merged on
top in `set_keybindings`, `table.py:2138-2160`).

A second, smaller group of GSettings keys have **no table-side counterpart at all** — `onechar`,
`commitinvalidmode`, `rememberinputmode`/`inputmode`, `debuglevel`, sound settings, orientation,
page size, `inputmethodmenu` — these are pure user preferences with a single GSettings default and
no per-table override path (§2).

This three-way precedence is itself a fact worth carrying into the design discussion: it shows that
`ibus-table` already treats a subset of its options as **"table suggests, user overrides"** rather
than purely fixed-by-table or purely user-owned — directly relevant to the "could be either"
classification below.

---

## §1 Table-defined options (source `.txt` / compiled `ime` table)

Every key from `docs/ibus-table-spec.md` §3.1, reclassified here for the option-design question.
"GSettings counterpart" marks the three-tier keys described above (§2 gives that key's schema
default). Real-world usage counts referenced in the "Why" column (e.g. for `AUTO_COMMIT`,
`AUTO_SELECT`, `DYNAMIC_ADJUST`) come from a grep across every table definition under
`refs/ibus-table-chinese/tables/` — see §4 for the full breakdown.

| Key | Table default | GSettings counterpart | Classification | Why |
|---|---|---|---|---|
| `MAX_KEY_LENGTH` | `4` | — | **Table-defined** | Defines the shape of the lookup keys actually stored in `phrases.tabkeys`; changing it per-user would desync from the compiled data. Not negotiable at runtime. |
| `VALID_INPUT_CHARS` | `abc…z` | — | **Table-defined** | Same reason: which key characters exist is baked into every row of the compiled table. |
| `START_CHARS` | `""` | — | **Table-defined** | Same — a lookup-shape constraint, not a preference. |
| `LANGUAGES` | `""` | — | **Table-defined** | Determines `is_db_chinese`/`is_db_cjk`, which gate whole behavior families (Chinese-variant filtering, full-width conversion applicability). Metadata about the table's content, not a preference. |
| `SINGLE_WILDCARD_CHAR` | `""` | `singlewildcardchar` | **Could be either** | Three-tier resolved (table default, user override). It is a lookup-syntax choice (which literal char maps to SQL `_`) — changing it per-user only makes sense if the user types wildcards; ibus-table lets the user override it, which is defensible but blurs "fixed data-shape" against "input preference." |
| `MULTI_WILDCARD_CHAR` | `""` | `multiwildcardchar` | **Could be either** | Same as above (maps to SQL `%`). |
| `AUTO_WILDCARD` | `TRUE` | `autowildcard` | **Could be either** | Three-tier. A pure UX/latency trade-off (broader matches vs. more candidates) independent of the compiled data — reasonable as a user preference, but the table author's choice (e.g. Cangjie relies on it for prefix search) is also a legitimate per-table default. ibus-table's precedence (user beats table) reflects treating it as ultimately a preference. |
| `RULES` | `""` | — | **Table-defined** | Compound-phrase construction is derived from `goucima` data compiled into the table; it is meaningless to override outside the table's own character-decomposition model. |
| `USER_CAN_DEFINE_PHRASE` | `FALSE` | — | **Table-defined** | Implies the presence of a `goucima` table in the compiled schema (`ibus-table-spec.md` §4.3); cannot be toggled on for a table that wasn't compiled with goucima data. |
| `NO_CHECK_CHARS` | `""` | — | **Table-defined** | Exclusion list tied to the table's own character set and phrase-learning semantics. |
| `LEAST_COMMIT_LENGTH` | `0` | — | **Table-defined** | Alternative auto-commit-boundary derivation from `MAX_KEY_LENGTH`; a structural property of the lookup, not a preference. Unused by any table in `ibus-table-chinese` (§5) — `RULES` is preferred in practice. |
| `PINYIN_MODE` | `FALSE` | — | **Table-defined** (capability); toggled ephemerally at runtime | The *availability* of pinyin fallback implies a compiled `pinyin` table (`ibus-table-spec.md` §4.4) and is fixed. But whether it is *active* right now (`self._py_mode`, `table.py:2304-2333`) is a session-level on/off toggle bound to a hotkey (`toggle_pinyin_mode`) — not persisted to GSettings, reset each session. See §1a. |
| `SUGGESTION_MODE` | `FALSE` | — | **Table-defined** (capability); toggled ephemerally at runtime | Same pattern as `PINYIN_MODE` — implies a compiled `suggestion` table; live on/off state (`self._sg_mode`) is ephemeral, not a saved preference. |
| `LANGUAGE_FILTER` (default Chinese-variant mode `cm0`–`cm4`) | *(none)* | `chinesemode` | **Could be either** | Three-tier, but via a bespoke resolver (`get_default_chinese_mode`) that additionally falls back to the process locale (`LC_CTYPE`) when neither table nor user has an opinion. The table author's suggested default (e.g. Stroke5 ships `cm3`, traditional-first, matching its Taiwan/HK audience) is a reasonable starting point, but the *live* mode is unambiguously session/user state — the engine exposes `switch_to_next_chinese_mode` as a live toggle independent of any saved value. |
| `AUTO_COMMIT` | `FALSE` | `autocommit` | **Could be either** | Three-tier. Governs whether reaching a key-run boundary auto-stages to preedit — real tables split roughly evenly (Stroke5 `TRUE`, Cangjie5 `FALSE`; see §5), so it is a genuine per-table behavioral default, but ibus-table also exposes a live hotkey (`toggle_autocommit_mode`) and lets user GSettings override it outright. |
| `AUTO_SELECT` | `FALSE` | `autoselect` | **Could be either** | Three-tier. Same tension as `AUTO_COMMIT` — every table in `ibus-table-chinese` that sets `AUTO_COMMIT` also sets `AUTO_SELECT` to the same value (§5), suggesting the two are usually co-designed by the table author as one behavioral profile, yet still user-overridable. |
| `DEF_FULL_WIDTH_PUNCT` | `TRUE` | `tabdeffullwidthpunct` (table layer) / `endeffullwidthpunct` (user layer) | **Could be either** | Explicitly modeled as *two* GSettings keys — one carrying the table's suggested default, one the user's global override — plus the table's own definition value as a third fallback (`table.py:494-507`). ibus-table's own design already treats this as "table suggests, user decides," i.e. deliberately not table-fixed. |
| `DEF_FULL_WIDTH_LETTER` | `FALSE` | `tabdeffullwidthletter` / `endeffullwidthletter` | **Could be either** | Same two-tier-plus-table design as punctuation, immediately above. |
| `DYNAMIC_ADJUST` | `FALSE` | `dynamicadjust` | **Could be either**, leans **user preference** | Three-tier, but semantically it controls whether *learning* happens at all (`user_freq` bumps into the user DB, `ibus-table-spec.md` §10.1) — a behavior about the user's own adaptation, which argues for user-preference more than the other three-tier flags. Every table in `ibus-table-chinese` enables it (§5) except the handful that predate the feature, suggesting table authors treat "on" as the sane default and expect it to just work, not to be tuned. |
| `NAME`, `NAME.<locale>`, `UUID`, `SERIAL_NUMBER`, `LICENSE`, `AUTHOR`, `DESCRIPTION` | — | — | **Table-defined** (pure metadata) | Identity/versioning, not behavior; not applicable to a runtime-options catalog beyond noting they exist. |
| `ICON`, `SYMBOL`, `STATUS_PROMPT`, `LAYOUT` | — | — | **Table-defined** (client policy) | Presentation metadata the engine parses but never acts on (`ibus-table-spec.md` §3.1); not an engine option at all in `CONCEPTS.md` terms. |
| `COMMIT_KEYS`, `SELECT_KEYS`, `PAGE_UP_KEYS`, `PAGE_DOWN_KEYS` | see spec §3.1 | `keybindings` (`a{sv}` dict, one schema-wide default set) | **Could be either**, but resolved as **client/UI policy** here | Three-way merge: GSettings schema default → table's CSV override folded in by `get_default_keybindings` → full per-key user GSettings override on top (`table.py:2138-2160`). `docs/ibus-table-spec.md` (§3.1, §12) already classifies candidate selection/paging keys as client policy a libpathime engine parses but never acts on — that framing stands; only *which* CONCEPTS.md operation each key drives (select-candidate, page) is engine-relevant, not the literal keycap. |
| `ORIENTATION` | `TRUE` | `lookuptableorientation` | **Client policy** | Candidate-list layout; `ibus-table-spec.md` excludes this from engine behavior entirely (§12). Table default merges with a GSettings user override the same three-tier way, but it never reaches the engine's own logic — only UI. |
| `ALWAYS_SHOW_LOOKUP` | `TRUE` | `alwaysshowlookup` | **Client policy** | Three-tier resolved, but purely a display decision (show the candidate list unprompted vs. only on request) — `ibus-table-spec.md` §12 already excludes it from engine behavior. |

### §1a Pinyin / suggestion mode: capability vs. live toggle

`PINYIN_MODE` and `SUGGESTION_MODE` are worth calling out separately because they are really **two
different options wearing one name**:

1. A **table-defined capability flag** — does the compiled database even contain a `pinyin` /
   `suggestion` table (`ibus-table-spec.md` §4.4–§4.5)? Fixed at compile time; cannot be turned on
   for a table that lacks the data.
2. An **ephemeral live toggle** — `self._py_mode` / `self._sg_mode`
   (`refs/ibus-table/engine/table.py:474-477`, `2304-2354`), off by default every session, flipped
   by a hotkey (`toggle_pinyin_mode` = `Shift_R`, `toggle_suggestion_mode` = `Super+Mod4+F6`), and
   **not written back to GSettings** — confirmed by grep: `set_pinyin_mode`/`set_suggestion_mode`
   never call anything resembling `gsettings.set_value`. This is a clean example of an option that
   is capability-gated by the table but explicitly *not* persisted as a user preference by the
   reference implementation, even though nothing would prevent it technically.

---

## §2 GSettings runtime options (`org.freedesktop.ibus.engine.table` schema)

Source: `refs/ibus-table/org.freedesktop.ibus.engine.table.gschema.xml`. One schema shared by every
table (not per-table). Keys already covered by the three-tier resolution in §1 are listed here only
for their schema-default role; the rest are pure GSettings-only options with no table-side input.

| Key | Type | Default | Table-side counterpart? | Classification | Why |
|---|---|---|---|---|---|
| `keybindings` | `a{sv}` (dict of key-name → key-list) | see schema (space=commit, digits=select 1–10, `Control+digit`=commit-to-preedit, arrows/paging, mode-toggle hotkeys, `setup`) | Merged with per-table `*_KEYS` (§1) | **User preference** (client policy) | Pure input-binding preference; `ibus-table-spec.md` §12 already treats key bindings as client policy the engine never interprets directly — only the *operation* they trigger (select candidate, page, toggle mode) is engine-relevant. |
| `inputmethodmenu` | `as` | `['chinese_mode', 'letter_width', 'punctuation_width', 'pinyin_mode', 'suggestion_mode']` | none | **User preference** (client policy) | Which mode-toggle entries appear in the IBus property menu — pure UI surface control. |
| `darktheme` | `b` | `false` | none | **User preference** (client policy) | Icon theme selection for the panel; no engine behavior. |
| `autoselect` | `b` | `false` | table `AUTO_SELECT` | **Could be either** — see §1 | |
| `autocommit` | `b` | `false` | table `AUTO_COMMIT` | **Could be either** — see §1 | |
| `commitinvalidmode` | `i` (`0`/`1`) | `0` | none | **User preference** | Selects what commits when an invalid-input character arrives: `0` = commit the current candidate, `1` = commit the raw typed characters (`table.py:520-527`). No table-side field at all — purely a global user choice about invalid-input handling, independent of any particular table's data. |
| `inputmode` | `i` (`0`/`1`) | `1` | none | **Ephemeral**, conditionally **user preference** | `0` = direct/Latin passthrough, `1` = table input on. Only persisted/restored at all if `rememberinputmode` is true; otherwise reset to `1` (table mode) every session — i.e. its persistence is itself gated by another option (§2, `rememberinputmode`). |
| `rememberinputmode` | `b` | `true` | none | **User preference** | Meta-option: whether `inputmode` should persist across sessions at all. A preference *about* another option's lifetime — a useful pattern to note for libpathime's own design. |
| `chinesemode` | `i` (`0`–`4`) | `4` | table `LANGUAGE_FILTER` | **Could be either** — see §1 | |
| `endeffullwidthletter` | `b` | `false` | table `DEF_FULL_WIDTH_LETTER` (as `tabdeffullwidthletter`) | **User preference** (global override layer) | The "end-user" layer of the two-tier full-width design (§1). |
| `endeffullwidthpunct` | `b` | `false` | table `DEF_FULL_WIDTH_PUNCT` (as `tabdeffullwidthpunct`) | **User preference** (global override layer) | Same. |
| `tabdeffullwidthletter` | `b` | `false` | table `DEF_FULL_WIDTH_LETTER` | **Table-defined** (schema fallback layer) | The "table default" layer of the two-tier design — only consulted when neither user GSettings nor the table's own definition value is present; effectively a GSettings-hosted secondary default. |
| `tabdeffullwidthpunct` | `b` | `false` | table `DEF_FULL_WIDTH_PUNCT` | **Table-defined** (schema fallback layer) | Same. |
| `lookuptableorientation` | `i` | `1` | table `ORIENTATION` | **Client policy** | See §1. |
| `lookuptablepagesize` | `i` | `6` | none (capped by how many `commit_candidate_N` keybindings are bound, `table.py:646-655`) | **User preference** (client policy) | Pure UI paging size; also implicitly bounded by the keybinding configuration (another cross-option dependency worth noting). |
| `onechar` | `b` | `false` | none | **User preference** | Restrict selection to single-character candidates only (`table.py:605-607`, `2370-2385`). No table-side field — a pure user-side input-restriction preference, orthogonal to any table's own data. |
| `alwaysshowlookup` | `b` | `true` | table `ALWAYS_SHOW_LOOKUP` | **Client policy** | See §1. |
| `singlewildcardchar` / `multiwildcardchar` | `s` | `''` | table fields of the same purpose | **Could be either** — see §1 | |
| `autowildcard` | `b` | `true` | table `AUTO_WILDCARD` | **Could be either** — see §1 | |
| `dynamicadjust` | `b` | `true` (note: schema default is `true`, though the doc-derived table default in `ibus-table-spec.md` §3.1 is `FALSE` — the *effective* default a fresh install sees is the GSettings one unless a table opts out) | table `DYNAMIC_ADJUST` | **Could be either**, leans user preference — see §1 | |
| `errorsound` | `b` | `true` | none | **User preference** | Whether to play a sound on invalid input; pure client/UX preference. |
| `errorsoundfile` | `s` | `'/usr/share/ibus-table/data/coin9.wav'` | none | **User preference** | Which sound file; ditto. |
| `soundbackend` | `s` | `'automatic'` | none | **User preference** | Audio backend selection (`table.py:456-458`); a host/environment preference, arguably not even IME-specific. |
| `debuglevel` | `i` (`0`–`255`) | `0` | none | **User preference** (developer-facing) | Logging verbosity; not an IME behavior option at all, but included since it is a genuine configuration surface at the API level. |

---

## §3 Options declared in the source format but not read by the reference engine (vestigial)

Real `ibus-table-chinese` tables (e.g. `stroke5/stroke5.txt`) declare several `BEGIN_DEFINITION`
keys that `docs/ibus-table-spec.md` does not mention and that a repository-wide search of
`refs/ibus-table/engine/*.py` shows are **never read back by name** anywhere in the compiler or
engine (only `forward_keys` appears at all, and only inside a comment,
`refs/ibus-table/engine/tabsqlitedb.py:204`). Because `ImeProperties` (see intro) stores *any*
`attr = val` pair without validation, these round-trip into the compiled `main.ime` table and are
silently inert:

| Key | Seen in | Apparent intent (from the source comment) |
|---|---|---|
| `KEYBOARD_LAYOUT` | `stroke5.txt` | Restrict to a physical keyboard layout (`US_Default`); distinct from the client-policy `LAYOUT` key that *is* parsed (§1). |
| `AUTO_SPLIT` | `stroke5.txt` | "Automatically split the inputted string during input." |
| `AUTO_FILL` | `stroke5.txt` | "Fill the preedit area with the current candidate automatically." |
| `DISCARD_INVALID_KEY` | `stroke5.txt` (declared twice) | Discard a key that doesn't extend any valid match, rather than accumulating it as `chars_invalid`. |
| `SHOW_KEY_PROMPT` | `stroke5.txt` | Toggle for the `char_prompts` substitution (`ibus-table-spec.md` §3.4/§6.2) — but `char_prompts` are applied unconditionally by `table.py` regardless of this flag. |
| `SPLIT_KEYS`, `FORWARD_KEYS` | `stroke5.txt` (both commented out in that file) | Companions to `AUTO_SPLIT`; a key list to trigger the split, and keys that forward raw input to the client. |

**Classification: N/A / not a real option.** These are legacy carryovers (most likely from the
older SCIM table format `ibus-table` traces its lineage to — see the SCIM header lines
`docs/ibus-table-spec.md` §3 notes are skipped) that a table author can still write without error,
but which have no observable effect on current `ibus-table` behavior. Listed here so libpathime's
design does not mistake "appears in a real table file" for "is a live option" — cross-referencing
against actual engine source code, not just table files, was necessary to catch these.

---

## §4 Which options real tables actually set (`refs/ibus-table-chinese/tables/`)

`ibus-table-chinese` has **22 distinct table definitions**, not one-`.txt`-file-per-table: most
tables are a single self-contained `.txt` source (`cangjie5.txt`, `stroke5.txt`, …), but Array30 and
Wubi-Haifeng build their `BEGIN_DEFINITION` block from a separate `*.head.in`/`*.head` template that
`CMakeLists.txt` concatenates with pure phrase-data fragments (e.g. `array/array30_27489.txt`,
`array/array30_ExtB.txt` carry no header at all — they are `tabkeys`/`phrase`/`freq` rows only,
merged under one shared definition to produce both an `array30` and an `array30-big` variant from
the same option set). This is itself a relevant data point: **a table's option set is a property of
the compiled/build target, not necessarily of one source file** — worth keeping in mind if
libpathime ever wants to support composing one option profile across multiple phrase-data files.

Grep results across the 22 definitions:

| Option | Set by | Not set by (uses default) |
|---|---|---|
| `AUTO_COMMIT` | 14 (Cangjie-big, Cangjie3, Cangjie5, all 4 Cantonese romanizations, Easy-big, Erbi, Quick-classic, SCJ6, Stroke5, Wu, Zhuyin-big) | 8 (Array30, Erbi-qs, Quick3, Quick5, Wubi-Haifeng, Wubi-Jidian86, Yong, Zhuyin) |
| `AUTO_SELECT` | 12 — same list as `AUTO_COMMIT` minus Cangjie3/Cangjie5 | 10 |
| `AUTO_WILDCARD` | 12 — same 12 as `AUTO_SELECT` | 10 |
| `USER_CAN_DEFINE_PHRASE` | 11 (Array30, Cangjie3/5, Erbi-qs, Quick3/5, SCJ6, Wubi-Haifeng, Wubi-Jidian86, Yong, Zhuyin) | 11 |
| `DYNAMIC_ADJUST` | 22 — every table sets it explicitly (mixed `TRUE`/`FALSE`) | 0 |
| `PINYIN_MODE` | 21 | 1 (Zhuyin-big) |
| `SUGGESTION_MODE` | 20 | 2 (Zhuyin-big, Zhuyin) |
| `DEF_FULL_WIDTH_LETTER` / `DEF_FULL_WIDTH_PUNCT` | 22 / 22 — every table sets both explicitly (near-universally `LETTER=FALSE`, `PUNCT=TRUE`) | 0 / 0 |
| `LANGUAGE_FILTER` | 19, values span `cm1`–`cm3` by the table's regional target (Stroke5 → `cm3` traditional-first; Wubi-Jidian86 → `cm2` simplified-first; Array30 → `cm3`) | 3 |
| `SINGLE_WILDCARD_CHAR` | 1 (`stroke5.txt`, `?`) | 21 rely on `AUTO_WILDCARD` alone |
| `MULTI_WILDCARD_CHAR` | 11 — the `AUTO_COMMIT`-profile tables minus Quick-classic, `*` in every case | 11 |
| `ALWAYS_SHOW_LOOKUP` | 12 — identical set to `AUTO_WILDCARD` | 10 |
| `RULES` | 4 (Erbi-qs, Wubi-Haifeng, Wubi-Jidian86, Yong) | 18, including Array30 (present but commented out, `### RULES =` in `array30.head.in`) and most other `USER_CAN_DEFINE_PHRASE = TRUE` tables (Cangjie3/5, Quick3/5, SCJ6, Zhuyin), which instead rely on the format's "auto-derive from longest matching `tabkeys`" fallback (`docs/ibus-table-spec.md` §3.3) |
| `ORIENTATION`, `START_CHARS`, `LEAST_COMMIT_LENGTH`, `NO_CHECK_CHARS` | 0 | all 22 — confirms these four are rarely-used escape hatches, not part of the common table-authoring pattern |

**Observed pattern:** table authors cluster into two profiles. The **"character table" profile**
(Cangjie-big, Cantonese romanizations, Easy-big, Erbi, Quick-classic, SCJ6, Stroke5, Wu, Zhuyin-big)
sets `AUTO_COMMIT`/`AUTO_SELECT`/`AUTO_WILDCARD`/`ALWAYS_SHOW_LOOKUP`/`MULTI_WILDCARD_CHAR` together
and leaves `USER_CAN_DEFINE_PHRASE` off. The **"phrase-building" profile** (Array30, Cangjie3/5,
Erbi-qs, Quick3/5, Wubi-Haifeng, Wubi-Jidian86, Yong, Zhuyin) does the opposite:
`USER_CAN_DEFINE_PHRASE = TRUE`, `AUTO_COMMIT`/`AUTO_SELECT`/`AUTO_WILDCARD` off. This supports
treating `AUTO_COMMIT`/`AUTO_SELECT`/`AUTO_WILDCARD`/`ALWAYS_SHOW_LOOKUP` and
`USER_CAN_DEFINE_PHRASE` as a **coupled table-defined profile** rather than independent flags when
thinking about libpathime's own option surface — real tables are not observed mixing the two
profiles, even though the source format allows any combination. `DYNAMIC_ADJUST` and the full-width
flags being set explicitly by literally every table (100%) suggests those three are treated by table
authors as mandatory boilerplate to state outright rather than left to any default — worth noting
against their "could be either" classification in §1: authors clearly have an opinion per table, even
if the GSettings layer also lets a user override it.

---

## §5 Summary table (all options, one place)

Legend: **E** = Ephemeral, **U** = User preference, **T** = Table-defined (fixed), **C** = Could be
either (tension explained above), **CP** = Client policy (excluded from engine per
`docs/ibus-table-spec.md` §12, listed here only because it is a real config surface at the API
level).

| Option | Class | Option | Class | Option | Class |
|---|---|---|---|---|---|
| `MAX_KEY_LENGTH` | T | `AUTO_SELECT` | C | `commitinvalidmode` | U |
| `VALID_INPUT_CHARS` | T | `DEF_FULL_WIDTH_PUNCT`/`LETTER` | C | `inputmode` | E (conditionally U) |
| `START_CHARS` | T | `DYNAMIC_ADJUST` | C (leans U) | `rememberinputmode` | U |
| `LANGUAGES` | T | `NAME`/`UUID`/`SERIAL_NUMBER`/etc. | T (metadata) | `onechar` | U |
| `RULES` | T | `ICON`/`SYMBOL`/`STATUS_PROMPT`/`LAYOUT` | CP (metadata) | `errorsound`/`errorsoundfile`/`soundbackend` | U |
| `USER_CAN_DEFINE_PHRASE` | T | `COMMIT_KEYS`/`SELECT_KEYS`/`PAGE_*_KEYS`/`keybindings` | CP | `debuglevel` | U (dev-facing) |
| `NO_CHECK_CHARS` | T | `ORIENTATION`/`lookuptableorientation` | CP | `darktheme` | U |
| `LEAST_COMMIT_LENGTH` | T | `ALWAYS_SHOW_LOOKUP` | CP | `inputmethodmenu` | U |
| `PINYIN_MODE` (capability) | T | `lookuptablepagesize` | U | `PINYIN_MODE` (live toggle) | E |
| `SUGGESTION_MODE` (capability) | T | | | `SUGGESTION_MODE` (live toggle) | E |
| `SINGLE_WILDCARD_CHAR`/`MULTI_WILDCARD_CHAR` | C | | | | |
| `AUTO_WILDCARD` | C | | | | |
| `LANGUAGE_FILTER`/`chinesemode` (default) | C | | | | `chinesemode` (live toggle) | E |
| `AUTO_COMMIT` | C | | | | |

`KEYBOARD_LAYOUT`, `AUTO_SPLIT`, `AUTO_FILL`, `DISCARD_INVALID_KEY`, `SHOW_KEY_PROMPT`,
`SPLIT_KEYS`, `FORWARD_KEYS` are omitted from this summary — see §3 (vestigial, not live options).
