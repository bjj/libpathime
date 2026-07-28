# pyzy Configuration Options

This document catalogs every runtime-configurable option available at the API level of the
`pyzy` submodule (`/c/dev/libpathime/pyzy`) and every additional option its IBus wrapper
(`refs/ibus-pinyin`) layers on top. It complements `docs/pyzy-mapping.md`, which maps API
*shape* to the canonical concepts in `docs/CONCEPTS.md`; this document is purely a catalog of
*options* — what is configurable, at what scope/lifetime, and whether it looks like a natural
per-session (ephemeral) setting, a persisted user preference, or a case where either is
defensible. It does not propose libpathime's own options API.

Per `TODO.md` finding 3 ("Two-layer lifetime everywhere"), pyzy options fall into the same two
layers as pyzy's object lifetimes: **global/process** (tied to `InputContext::init()` /
`finalize()`) and **per-context** (tied to a `PyZy::InputContext*`), with per-context further
split into **creation-time-only** (the `InputType`) and **mutable-anytime** (`setProperty()`).

---

## 1. pyzy library-level options

### 1.1 Global / process-level options

These are set once, before any `InputContext` is created, and apply to every context for the
life of the process. They are not part of `PropertyName`/`setProperty()` — they are arguments to
the static `init()` call and are not re-settable afterward without a full `finalize()`/`init()`
cycle.

| Option | API | Values | Default | Citation |
|---|---|---|---|---|
| User cache directory | `InputContext::init(user_cache_dir, user_config_dir)` | Filesystem path | `$XDG_CACHE_HOME/pyzy` via the no-arg `init()` overload | `InputContext.h:379-400`, `InputContext.cc:34-59` |
| User config directory (special-phrase table location) | same call | Filesystem path | `$XDG_CONFIG_HOME/pyzy` via the no-arg overload | same |

Both directories are consumed to construct the process-global singletons: `Database::init()`
(shared SQLite phrase database + user-phrase history) and `SpecialPhraseTable::init()` (loads
`phrases.txt` from the config dir) — `InputContext.cc:57-58`, `Database.h:66,80-86`,
`SpecialPhraseTable.h:48-49`. There is no API to reconfigure these paths after `init()`, swap the
phrase database, or point at a different bundled dictionary at runtime; the only "dictionary
selection" pyzy offers is *which* directory holds the user's own `phrases.txt` and cache. Where
the *shipped* data is read from is `pyzy_set_data_dir()`, which the port adds (`DataDir.h`) and
libpathime calls from `pyzy_global_init()`; that too is set once, before `init()`.
`Database::instance()` hard-errors (`g_error`) if queried before `init()` — this is a lifecycle
contract, not an option.

**Classification:** **Could be either**, but leans **user preference / deployment configuration**.
The paths themselves are the kind of thing a host application decides once at install/profile
setup (analogous to an XDG data-dir choice), not something that varies per input session. A
library like libpathime would likely fix a policy for where per-user data lives rather than
exposing raw paths as a live "option," but the underlying directories are inherently
process-lifetime, not context-lifetime, so they cannot be ephemeral in the way a per-context
setting can.

### 1.2 Per-context, creation-time-only option

| Option | API | Values | Citation |
|---|---|---|---|
| Phonetic input type | `InputContext::create(InputType, Observer*)` | `FULL_PINYIN`, `DOUBLE_PINYIN`, `BOPOMOFO` | `InputContext.h:152-159, 410-419`, `InputContext.cc:67-81` |

There is no double-pinyin "scheme" *value* at this layer distinct from the double-pinyin table
(see 1.3) — `DOUBLE_PINYIN` is one `InputType` among three, and which double-pinyin *keyboard
mapping* it uses is a separate, mutable property. As `TODO.md` and `docs/pyzy-mapping.md` both
note, `InputType` is fixed for the life of the context (`InputContext.cc:67-81`); pyzy has no
`setInputType()`. Switching between Pinyin and Bopomofo, or between full and double pinyin,
requires destroying the context and calling `create()` again.

**Classification:** **User preference**, with a hard constraint. Users pick one phonetic scheme
(Pinyin vs. Bopomofo, and full vs. double pinyin) as a durable setting — this is exactly the kind
of thing that should be read from persisted configuration when a new context is created. It is
*not* naturally ephemeral (nobody wants to re-choose their phonetic scheme every session), but it
also cannot be a live per-context toggle the way `PROPERTY_MODE_SIMP` is — the binding must
recreate the context on change. This is a case where the "natural" preference-like scope of the
option conflicts with the API's context-recreation requirement; a wrapper (see 2.3) can paper over
this by transparently swapping the active `InputContext*`, which is what ibus-pinyin does.

### 1.3 Per-context, mutable-anytime options (`PropertyName` / `setProperty()`)

All five properties live on `InputContext::PropertyName` (`InputContext.h:176-209`) and are
readable via `getProperty()` / writable via `setProperty(name, Variant)` at any point in a
context's life, not just at creation. `setProperty` dispatch is split across the class hierarchy:
`PhoneticContext::setProperty` (`PhoneticContext.cc:304-333`) handles
`PROPERTY_CONVERSION_OPTION`, `PROPERTY_SPECIAL_PHRASE`, and `PROPERTY_MODE_SIMP` and returns
`false` for the two schema properties; `DoublePinyinContext::setProperty`
(`DoublePinyinContext.cc:501-518`) and `BopomofoContext::setProperty` (`BopomofoContext.cc:443-460`)
handle their respective schema property and delegate everything else upward.

| Property | Type | Meaning | Default | Where honored | Citation |
|---|---|---|---|---|---|
| `PROPERTY_CONVERSION_OPTION` | `unsigned int` bitmask | Incomplete-pinyin matching + 8 typo-correction rules + 20 fuzzy-pinyin pairs (see below) | `PINYIN_INCOMPLETE_PINYIN \| PINYIN_CORRECT_ALL \| PINYIN_FUZZY_ALL` | All phonetic context subclasses (`PhoneticContext`) | `InputContext.h:176-184`, `Config.h:29-40`, `PhoneticContext.cc:311-313` |
| `PROPERTY_DOUBLE_PINYIN_SCHEMA` | `unsigned int` (enum-like, 0–5) | Which of 6 double-pinyin keyboard layouts (MSPY, ZRM, ABC, ZGPY, PYJJ, XHE) to decode keystrokes with | `DOUBLE_PINYIN_KEYBOARD_MSPY` (0) | `DoublePinyinContext` only; rejected (returns `false`) by `PhoneticContext`/other subclasses | `Const.h:82-88`, `DoublePinyinContext.cc:47-49, 501-518` |
| `PROPERTY_BOPOMOFO_SCHEMA` | `unsigned int` (enum-like, 0–3) | Which of 4 Bopomofo keyboard layouts (Standard, Ching-Yeah/Eten-alt, Eten, IBM) to decode keystrokes with | `BOPOMOFO_KEYBOARD_STANDARD` (0) | `BopomofoContext` only; rejected by other subclasses | `Const.h:90-94`, `BopomofoContext.cc:32-35, 443-460` |
| `PROPERTY_SPECIAL_PHRASE` | `bool` | Whether special-phrase lookups (date/time macros etc. from `phrases.txt`) are included in candidates | `true` | All phonetic context subclasses | `InputContext.h:199-203`, `Config.h:34, 38`, `PhoneticContext.cc:321-323` |
| `PROPERTY_MODE_SIMP` | `bool` | Simplified (`true`) vs. traditional (`false`) Chinese output repertoire | `true` | All phonetic context subclasses | `InputContext.h:204-208`, `Config.h:35, 39`, `PhoneticContext.cc:324-326` |

`getProperty`/`setProperty` reject type mismatches (e.g. passing a bool `Variant` for
`PROPERTY_DOUBLE_PINYIN_SCHEMA`) and reject out-of-range schema indices
(`schema >= DOUBLE_PINYIN_KEYBOARD_LAST` / `BOPOMOFO_KEYBOARD_LAST`), returning `false` rather
than clamping (`DoublePinyinContext.cc:504-511`, `BopomofoContext.cc:446-453`).

#### `PROPERTY_CONVERSION_OPTION` bit inventory (`Const.h:26-80`)

| Group | Bits | Count | Notes |
|---|---|---|---|
| Incomplete pinyin | `PINYIN_INCOMPLETE_PINYIN` | 1 | Allows partial syllables to match (e.g. "nh" → 你好); also gates Bopomofo's equivalent incomplete-input check (`BopomofoContext.cc:56`, reusing the same bit) |
| Typo correction | `PINYIN_CORRECT_GN_TO_NG` … `PINYIN_CORRECT_ON_TO_ONG` | 8 | `PINYIN_CORRECT_ALL` (`0x1fe`) is an aggregate mask of all 8, not itself a distinct rule |
| Fuzzy consonant pairs | `PINYIN_FUZZY_C_CH` … `PINYIN_FUZZY_G_K` | 14 | 7 symmetric pairs (c/ch, z/zh, s/sh, l/n, f/h, l/r, k/g), each direction independently toggleable |
| Fuzzy vowel pairs | `PINYIN_FUZZY_AN_ANG` … `PINYIN_FUZZY_ING_IN` | 6 | `IAN_IANG`/`IANG_IAN`/`UAN_UANG`/`UANG_UAN` are `#define` aliases of `AN_ANG`/`ANG_AN`, so they are not independently controllable bits despite having distinct names |
| Fuzzy aggregate | `PINYIN_FUZZY_ALL` (`0x1ffffe00`) | — | Aggregate mask of all 20 fuzzy bits |

**Classification (each row below):**

- **Conversion option bitmask (incomplete pinyin, typo correction, fuzzy pinyin) —
  User preference.** These encode how forgiving the phonetic matcher is; users settle on a
  tolerance level (often tied to their regional accent — fuzzy c/ch, z/zh, etc. map to real
  Mandarin dialect mergers) and expect it to persist across sessions. Nothing about them is
  session-specific.
- **Double-pinyin / Bopomofo schema — User preference.** Keyboard layout choice is a durable
  per-user fact (which double-pinyin scheme they memorized, which physical Bopomofo keyboard they
  use), not something to reset each session. Note it is scoped to whichever context type is live;
  a libpathime design must decide whether this is one persisted value per phonetic mode or a
  single persisted value independent of mode.
- **`PROPERTY_SPECIAL_PHRASE` — Could be either.** Whether date/time macro expansion is desired is
  plausibly a stable preference (most users either want it always or never), but it is also
  low-stakes and cheap to leave at its library default; a design could reasonably treat it as
  either a persisted toggle or a fixed always-on capability.
- **`PROPERTY_MODE_SIMP` — Could be either, and pyzy itself signals this tension.** It is exposed
  as a `bool` property (suggesting per-context, changeable-anytime state) but in the wrapper it is
  *also* read from a persisted `InitSimplifiedChinese` default at engine-enable time (see 2.4) and
  additionally bound to a live keyboard shortcut and language-bar button (see 2.5). Both usages
  coexist: a persisted default users rarely change, and a same-session quick-toggle. Any design
  exposing this needs both a persisted default and a per-context override that does not
  overwrite the persisted default when toggled transiently.

### 1.4 Confirmed absent from pyzy (wrapper-only; see §2)

Per `docs/*-mapping.md`, the following are **not** pyzy options at all — pyzy has no
API surface for them, confirmed by reading the property enum and every `setProperty` override:

- Full-width vs. half-width Latin/digit output
- Full-width vs. half-width (Chinese) punctuation, and punctuation substitution itself
- Candidate list page size, orientation, or any paging/select-key layout
- Any "auto-commit" or English/Chinese mode-switch behavior
- Any shortcut-key configuration

### 1.5 Hardcoded, not configurable at all

For contrast with what *is* an option: `MAX_PINYIN_LEN` (64 characters, the raw-input buffer cap
for all three context types) is a compile-time `#define` (`PhoneticContext.h:38`, referenced by
`FullPinyinContext.cc:50`, `DoublePinyinContext.cc:74`, `BopomofoContext.cc:50`), not a runtime
option. Same for the internal `PhraseEditor` candidate-preparation batch size and prefetch
behavior in `Database` — these are implementation details, not exposed as `PropertyName` values or
`init()` arguments.

---

## 2. ibus-pinyin wrapper-level options

The wrapper's `Config`/`PinyinConfig`/`BopomofoConfig` classes (`PYConfig.h/.cc`) read IBus
GSettings-backed keys under `engine/Pinyin` and `engine/Bopomofo` sections and either (a) forward
the value into pyzy via `setProperty()` on every live context (`updateContext()`,
`PYConfig.cc:364-372`), or (b) keep it purely as wrapper state with no pyzy equivalent. The setup
UI (`setup/ibus-pinyin-preferences.ui`, driven by `setup/main.py`) exposes these as GTK widgets;
widget `id` attributes match the `CONFIG_*` GSettings key names.

### 2.1 Options that map directly onto a pyzy property

| GSettings key | Config accessor | pyzy target | Citation |
|---|---|---|---|
| `IncompletePinyin` | `m_option` bit | `PROPERTY_CONVERSION_OPTION` bit `PINYIN_INCOMPLETE_PINYIN` | `PYConfig.cc:126, 197-207` |
| `CorrectPinyin` (master switch) + 8 `CorrectPinyin_*` keys | `m_option` bits, gated by `m_option_mask` | `PROPERTY_CONVERSION_OPTION`, `PINYIN_CORRECT_*` bits | `PYConfig.cc:374-389, 437-452` |
| `FuzzyPinyin` (master switch) + 20 `FuzzyPinyin_*` keys | `m_option` bits, gated by `m_option_mask` | `PROPERTY_CONVERSION_OPTION`, `PINYIN_FUZZY_*` bits | `PYConfig.cc:122-154, 190-207` |
| `DoublePinyinSchema` | `m_double_pinyin_schema` | `PROPERTY_DOUBLE_PINYIN_SCHEMA` | `PYConfig.cc:412-418` |
| `BopomofoKeyboardMapping` | `m_bopomofo_keyboard_mapping` | `PROPERTY_BOPOMOFO_SCHEMA` | `PYConfig.cc:570-572` |
| `SpecialPhrases` | `m_special_phrases` | `PROPERTY_SPECIAL_PHRASE` | `PYConfig.cc:116-120, 427-429` |
| `InitSimplifiedChinese` | `m_init_simp_chinese` → seeds `PinyinProperties::m_mode_simp` | `PROPERTY_MODE_SIMP` (mirrored on every change, see 2.4/2.5) | `PYConfig.cc:425, 564`; `pyzy-mapping.md` §"PinyinProperties mode flags" |

Two wrapper-level bugs/quirks worth flagging (they affect what the *effective* option set is, even
though they are not part of the "what's configurable" catalog):

- `CONFIG_CORRECT_PINYIN`'s master-switch semantics only gate the *mask* (`m_option_mask`); the
  underlying `m_option` bits keep whatever they were last read as, so toggling the master switch
  off and back on does not restore per-rule state from before — it just stops masking it out.
- The `pinyin_options[]` table (`PYConfig.cc:374-389`) has a duplicate entry: both
  `"CorrectPinyin_V_U"` and `"CorrectPinyin_VE_UE"` GSettings keys map to the same
  `PINYIN_CORRECT_V_TO_U` bit, and `"CorrectPinyin_GN_NG"` is listed twice. There is no
  `PINYIN_CORRECT_UE_TO_VE`-controlling key distinct from `CorrectPinyin_UE_VE`, which does exist
  separately and correctly maps to `PINYIN_CORRECT_UE_TO_VE`. This is a latent wrapper bug, not a
  pyzy issue.

**Classification:** All rows in this table inherit the classification of the pyzy property they
map to (§1.3) — **user preference**, except `InitSimplifiedChinese`, discussed under §2.4/2.5 as
the concrete instance of "could be either."

### 2.2 Wrapper-only: candidate-list / lookup-table presentation

| GSettings key | Config accessor | Purpose | Default | Citation |
|---|---|---|---|---|
| `LookupTableOrientation` | `orientation()` | Horizontal vs. vertical candidate window | `IBUS_ORIENTATION_HORIZONTAL` | `PYConfig.cc:105, 178-183` |
| `LookupTablePageSize` | `pageSize()` | Candidates shown per page (clamped ≤ 10) | 5 | `PYConfig.cc:106, 184-188` |
| `ShiftSelectCandidate` | `shiftSelectCandidate()` | Whether Shift+number selects a candidate | `FALSE` | `PYConfig.cc:107, 432` |
| `MinusEqualPage` | `minusEqualPage()` | Whether `-`/`=` page up/down | `TRUE` | `PYConfig.cc:108, 433` |
| `CommaPeriodPage` | `commaPeriodPage()` | Whether `,`/`.` page up/down | `TRUE` | `PYConfig.cc:109, 434` |
| `SelectKeys` (Bopomofo only) | `selectKeys()` | Which of 9 candidate-label key rows (`bopomofo_select_keys[]`) is active | 0 (`"1234567890"`) | `PYConfig.cc:574-575`, `PYBopomofoEditor.cc:31-40` |
| `GuideKey` (Bopomofo only) | `guideKey()` | Whether an extra keypress is required before select-keys pick a candidate | `TRUE` | `PYConfig.cc:576`, `PYBopomofoEditor.cc:64, 229` |
| `AuxiliarySelectKey_F` (Bopomofo only) | `auxiliarySelectKeyF()` | Whether F1–F10 also act as select keys | `TRUE` | `PYConfig.cc:577`, `PYBopomofoEditor.cc:103` |
| `AuxiliarySelectKey_KP` (Bopomofo only) | `auxiliarySelectKeyKP()` | Whether the numeric keypad also acts as select keys | `TRUE` | `PYConfig.cc:578`, `PYBopomofoEditor.cc:93, 98` |
| `EnterKey` (Bopomofo only) | `enterKey()` | Present in `Config` and the setup UI (`CommitFirstCandidate`/`CommitOriginalText` radio group) but **not read by any reviewed engine code path** — Enter unconditionally calls `commit()` in `PhoneticEditor::processFunctionKey` (`PYPhoneticEditor.cc:98-101`) | `TRUE` | `PYConfig.cc:579`; no consuming call site found |

None of these have a pyzy equivalent — CONCEPTS.md explicitly places pagination, page size,
orientation, and candidate labeling on the client side, and pyzy has no concept of pages at all
(confirmed in `pyzy-mapping.md` §"Impedance mismatch 3").

**Classification:** **User preference**, with one exception. Page size, orientation, select-key
layout, guide-key, and the paging-key bindings (comma/period, minus/equal, Shift) are all
"how I like my candidate window to behave" — durable across sessions, akin to keyboard shortcut
preferences in any application. `EnterKey`/`CommitFirstCandidate` is unclassifiable as currently
implemented since it appears to be dead configuration (present in schema and UI, absent from
behavior) — flagged rather than classified.

### 2.3 Wrapper-only: which phonetic editor is active for `MODE_INIT`

| GSettings key | Config accessor | Effect | Citation |
|---|---|---|---|
| `DoublePinyin` | `doublePinyin()` | Selects whether `PinyinEngine`'s `MODE_INIT` editor is a `DoublePinyinEditor` or `FullPinyinEditor` — i.e., which pyzy `InputType` backs the live context | `FALSE` | `PYConfig.cc:117`, `PYPinyinEngine.cc:48-51, 182-191` |
| `DoublePinyinShowRaw` | `doublePinyinShowRaw()` | Whether the raw double-pinyin keystrokes are shown alongside the decoded full-pinyin form in the preedit/auxiliary text | `FALSE` | `PYConfig.cc:108, 419` |
| | | *Was `PATHIME_OPT_PINYIN_SHOW_RAW`; **cut** with the auxiliary text field, which was the only place it had to write. The decoded syllables are the preedit now, which is the half a user actually reads; the raw keys are an encoding of them the client already knows it sent.* | | |

`DoublePinyin` is the wrapper's concrete resolution of the tension flagged in §1.2: pyzy's
`InputType` is creation-time-only, so `PinyinEngine::updateProperty()` handles a live toggle of
this GSettings key by constructing a brand-new `DoublePinyinEditor`/`FullPinyinEditor` (and thus a
brand-new pyzy `InputContext`) in place, using `dynamic_cast` to detect the currently-installed
editor type before swapping (`PYPinyinEngine.cc:182-191`). This is exactly the
"destroy-and-recreate" pattern `TODO.md` calls out — the wrapper hides the recreation from the
user by doing it transparently on config change.

**Classification:** `DoublePinyin` — **user preference** (per §1.2's reasoning: durable scheme
choice). `DoublePinyinShowRaw` — **user preference**; it is a display/debugging aid some users like
permanently on, not a per-session toggle.

### 2.4 Wrapper-only: default mode flags at engine-enable time

| GSettings key | Config accessor | Seeds | Default (Pinyin / Bopomofo) | Citation |
|---|---|---|---|---|
| `InitChinese` | `initChinese()` | `PinyinProperties::m_mode_chinese` on `Engine::enable()` | `TRUE` / `TRUE` | `PYConfig.cc:112, 422, 561` |
| `InitFull` | `initFull()` | `PinyinProperties::m_mode_full` | `FALSE` / `FALSE` | `PYConfig.cc:113, 423, 562` |
| `InitFullPunct` | `initFullPunct()` | `PinyinProperties::m_mode_full_punct` | `TRUE` / `TRUE` | `PYConfig.cc:114, 424, 563` |
| `InitSimplifiedChinese` | `initSimpChinese()` | `PinyinProperties::m_mode_simp` (and, when changed live, `PROPERTY_MODE_SIMP` — see 2.5) | `TRUE` / `FALSE` | `PYConfig.cc:115, 425, 564` |

These four are read once when the engine is enabled/constructed (`PinyinProperties::reset()`,
called from `Engine::enable()` per `pyzy-mapping.md` §"IBus lifecycle hooks") to seed the *initial*
value of a session-mutable mode flag. They are the persisted half of the "default vs. live toggle"
split described for `PROPERTY_MODE_SIMP` in §1.3.

**Classification:** **User preference** for all four — they are explicitly named `Init*`, i.e. the
wrapper's own authors already modeled them as "the persisted starting state," distinct from the
live in-session flags they seed (§2.5). This is a clean precedent: pair a persisted default with an
ephemeral per-session override, rather than trying to make one field serve both roles.

### 2.5 Wrapper-only: live per-session mode flags (not GSettings-backed)

`PinyinProperties` (`PYPinyinProperties.h/.cc`) holds four `gboolean` flags that are *not*
themselves config keys — they are runtime state seeded from §2.4's defaults, toggled by shortcut
keys or language-bar clicks during a session, and reset back to the `Init*` defaults on
`Engine::enable()`:

| Flag | Toggled by | Effect | Feeds back into config/pyzy? | Citation |
|---|---|---|---|---|
| `m_mode_chinese` | Shift-release | Chinese input vs. raw English pass-through | No — wrapper-only routing state | `pyzy-mapping.md` §"mode-switching dispatcher"; `PYPinyinProperties.h:44` |
| `m_mode_full` | Language-bar property click | Full-width vs. half-width Latin output (via `HalfFullConverter`) | No — pure wrapper conversion, applied in `commitCallback()` | `PYPinyinProperties.h:45`; `pyzy-mapping.md` §"Full/half-width output conversion" |
| `m_mode_full_punct` | Language-bar property click | Full-width vs. half-width Chinese punctuation substitution | No — pure wrapper conversion (`FallbackEditor`) | `PYPinyinProperties.h:46`; `pyzy-mapping.md` §"Chinese punctuation substitution" |
| `m_mode_simp` | `Ctrl+Shift+F`, language-bar click | Simplified vs. traditional output | **Yes** — mirrored into the live context via `setProperty(PROPERTY_MODE_SIMP, ...)` whenever it changes, and reapplied to a freshly (re)created context in `setContext()` | `pyzy-mapping.md` §"PinyinProperties mode flags"; `PYPinyinProperties.h:47` |

**Classification:** **Ephemeral** for `m_mode_chinese`, `m_mode_full`, and `m_mode_full_punct` —
these are genuinely session/quick-toggle state: a user flips to English mode for one word, or
wants one message in full-width, without wanting that choice to outlive the moment (though the
*default* they return to, per §2.4, is a preference). `m_mode_simp` is the one flag in this group
that is simultaneously ephemeral state (a live per-context property) and tied to a persisted
default — see §1.3's discussion; it is the clearest example in the whole pyzy/ibus-pinyin surface
of "could be either," because the actual implementation deliberately keeps both.

### 2.6 Wrapper-invented actions that are not options at all

For completeness, two things that look option-like but are per-invocation actions, not settings:

- `resetCandidate` / "forget this candidate" (bound to Ctrl+number in `PinyinEditor`,
  `PYPinyinEditor.cc:79`) — a one-shot mutation of the user-phrase history via pyzy's
  `InputContext::resetCandidate()`, not a toggle with a persistent value of its own.
- The five-mode key-event dispatcher (`MODE_INIT`/`MODE_PUNCT`/`MODE_RAW`/`MODE_ENGLISH`/
  `MODE_EXTENSION`) and its entry keys (`v` for English, `i` for the Lua extension) are fixed
  wrapper behavior, not configurable — there is no GSettings key to remap or disable them.

---

## 3. Cross-cutting observations

### 3.1 Options common in spirit to other IME engines vs. Pinyin/Bopomofo-specific

| Category | Examples from this catalog | Likely also present in libhangul/anthy wrappers? |
|---|---|---|
| Candidate-list presentation | Page size, orientation, select-key layout, guide-key, paging-key bindings (§2.2) | Yes — every table-driven or candidate-list IME needs page size/orientation; select-key layout specifically echoes ibus-table's label-key configuration |
| Mode-switching | Chinese/English toggle, simplified/traditional toggle, their shortcut keys and language-bar properties (§2.3, §2.5) | Partially — an IME-active/direct-input toggle is universal; simplified/traditional is Chinese-specific, but the *pattern* (persisted default + live per-session override + shortcut key) generalizes to e.g. hiragana/katakana mode in Japanese IMEs |
| Encoding/width conversion | Full/half-width Latin and punctuation (§2.3's absence from pyzy, wrapper's `HalfFullConverter`/`FallbackEditor`) | Yes — full/half-width toggles are common across CJK IMEs generally, not Pinyin-specific |
| Special-phrase / macro expansion | `PROPERTY_SPECIAL_PHRASE`, `phrases.txt` (§1.3, §1.5) | Engine-family-specific naming, but "user-editable expansion/macro table" is a recognizable pattern other engines may have under different names |
| Phonetic-conversion tuning | Typo-correction rules, fuzzy-pinyin pairs, double-pinyin/Bopomofo keyboard schema (§1.3) | **Pinyin/Bopomofo-specific.** These exist because Pinyin input tolerates regional pronunciation variation and multiple keyboard-compression schemes; there is no equivalent concept in libhangul (single fixed jamo-per-key model) and anthy's romaji/kana input has no comparable "fuzzy matching" concept |
| Global init paths | Cache/config directory for `Database`/`SpecialPhraseTable` (§1.1) | Yes, in spirit — anthy's personality/dictionary files and libhangul's external keyboard registry are the same "process-global resource location" pattern, per `TODO.md` finding 3 |

### 3.2 Scope/lifetime summary

| Scope | Options |
|---|---|
| **Global process (init-time only)** | Cache dir, config dir (§1.1) |
| **Per-context, creation-time only** | `InputType` (Pinyin/double-pinyin/Bopomofo) (§1.2) — wrapper hides this behind live-looking `DoublePinyin` toggle by recreating the context (§2.3) |
| **Per-context, mutable anytime (pyzy `setProperty`)** | Conversion-option bitmask, double-pinyin schema, Bopomofo schema, special-phrase toggle, mode-simp (§1.3) |
| **Wrapper-only, persisted (GSettings), pushed to all live contexts on change** | Everything in §2.1 plus §2.2's candidate-list settings plus §2.4's `Init*` defaults |
| **Wrapper-only, session-local, not persisted** | `m_mode_chinese`, `m_mode_full`, `m_mode_full_punct` (§2.5); reset to the `Init*` default every time the engine is (re-)enabled |

### 3.3 Classification tally

- **Clearly ephemeral:** `m_mode_chinese`, `m_mode_full`, `m_mode_full_punct` (§2.5) — three
  flags, all wrapper-invented, all reset on enable.
- **Clearly user preference:** the great majority — conversion-option bitmask (incomplete pinyin,
  8 correction rules, 20 fuzzy pairs), double-pinyin schema, Bopomofo schema, phonetic `InputType`
  choice, all `LookupTable*`/select-key/paging-key settings, `Init*` default flags, global
  cache/config paths.
- **Could be either (with justification for the tension):** `PROPERTY_SPECIAL_PHRASE` (low-stakes
  toggle, plausibly fine as a fixed default rather than a persisted preference);
  `PROPERTY_MODE_SIMP` / `m_mode_simp` (the one option the reference implementation itself
  represents twice — once as a persisted `Init*` default, once as a live per-context flag — because
  neither treatment alone satisfies real usage: users want a durable default *and* a quick
  same-session override).
