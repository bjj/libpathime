# anthy-unicode Configuration Options Catalog

This document catalogs every configurable option/setting available at the API level of the
`anthy-unicode` submodule and every option the `ibus-anthy` reference wrapper adds on top of it.
It is a companion to `docs/anthy-mapping.md` (which maps API *concepts*) — this document is
scoped specifically to *configuration options*, their lifetime/scope, and how they should be
classified for a future libpathime options API. It does not propose that API; see `TODO.md` for
the current design-stage thinking (in particular the two-layer global/process-init vs.
per-context lifetime model, which this document's "Scope" column is expressed against).

Sources read: `anthy-unicode/anthy/anthy.h`, `anthy-unicode/anthy/conf.h`,
`anthy-unicode/src-main/main.c`, `anthy-unicode/src-main/main.h`,
`anthy-unicode/src-main/context.c`, `anthy-unicode/src-diclib/conf.c`,
`anthy-unicode/src-worddic/priv_dic.c`, `anthy-unicode/src-worddic/dic_personality.h`,
`anthy-unicode/anthy/{ordering,prediction,record,logger}.h`,
`refs/ibus-anthy/data/org.freedesktop.ibus.engine.anthy.gschema.xml.in`,
`refs/ibus-anthy/engine/python3/engine.py`, `refs/ibus-anthy/engine/python3/jastring.py`,
`refs/ibus-anthy/setup/python3/anthyprefs.py`.

---

## Scope legend

| Scope | Meaning |
|---|---|
| **Global-process** | Affects the whole process; set once via `anthy_init()`/`anthy_conf_override()`/`anthy_set_personality()` before (or independent of) any context. Not context-specific. |
| **Per-context-at-creation** | Baked into a context when `anthy_create_context()` runs; cannot be changed for that context afterward. |
| **Per-context-mutable** | Can be changed on an existing `anthy_context_t` at any time via a setter. |
| **Wrapper-global** | Lives in `ibus-anthy`'s `Engine` class-level (not instance-level) state — shared across all input contexts the process serves, analogous to anthy's global-process scope but wrapper-owned. |
| **Wrapper-per-session** | Lives on the `Engine` instance (one per IBus input context). |

---

## 1. anthy-unicode library-level options

### 1.1 Process-global configuration (`anthy_conf_override`)

`anthy_conf_override(var, val)` (`anthy/anthy.h:55`, implemented in `src-main/main.c:130` →
`anthy_do_conf_override` in `src-diclib/conf.c:207`) writes into a single process-wide key/value
table (`ent_list` in `conf.c:49`) that is queried throughout the library via
`anthy_conf_get_str(var)`. There is no per-context variant — this is pure global process
configuration, and it is **not thread-safe** (a bare linked list with no locking).

Key behavior: the first call to any variable other than `CONFFILE` triggers
`anthy_do_conf_init()` if not already initialized, which pre-seeds `HOME`, `VERSION`, a generated
`SESSION-ID`, and reads a conf file (`CONFFILE`, default `$CONF_DIR/anthy-unicode.conf`) into the
same table (`conf.c:206-259`). Any variable in the table can be looked up with `${VAR}`
substitution syntax inside another variable's value (`conf.c:85-110`). If a variable was never set
via `anthy_conf_override` or the conf file, `anthy_conf_get_str` falls back to `getenv(var)`
(`conf.c:280-283`) — so every one of the variables below is *also* settable as a plain environment
variable, and `anthy_conf_override` is a program-level way to set the same variable without
touching the process environment.

Known variables actually consumed elsewhere in the codebase (found by grepping
`anthy_conf_get_str` call sites):

| Variable | Consumer | Purpose | Classification |
|---|---|---|---|
| `CONFFILE` | `conf.c:189,248` | Path to the `.conf` file read at init; special-cased in `anthy_do_conf_override` to re-trigger init if set before other vars. | **User preference** — a deployment/installation setting (where dictionaries and rc files live), not something that varies per keystroke or per session. |
| `ANTHYDIR` | `src-diclib/ruleparser.c:78`, `src-worddic/dic_util.c:570` | Directory containing the compiled system dictionary/rule data. | **User preference** — install-time/deployment path, effectively immutable for a given install. |
| `HOME` | `src-worddic/priv_dic.c:87,103`, seeded automatically from `getpwuid()` | Base directory for locating the personal-dictionary directory (`$HOME/.anthy` legacy, or XDG path). | **Could be either** — normally inherited from the OS environment (ephemeral, not something a UI exposes), but a sandboxed or multi-profile host embedding libpathime might want to override it deliberately as an install-level preference to isolate user data. |
| `XDG_CONFIG_HOME` | `src-worddic/priv_dic.c:95` | Preferred base for the personal-dictionary directory (`$XDG_CONFIG_HOME/anthy`) over the legacy `$HOME/.anthy`. | Same as `HOME` — **could be either**, environment-derived by default. |
| `DIC_FILE` | `src-diclib/file_dic.c:65` | Path to an auxiliary flat-file dictionary. | **User preference** — dictionary selection is inherently a persisted choice (see personality/dictionary discussion below). |
| `WORDS_FILE` | `src-worddic/dic_util.c:601` | Path to the system words dictionary. | **User preference** — deployment path. |
| `ZIPDICT_EUC` | `src-worddic/ext_ent.c:108` | Path to the postal-code (zip code) conversion dictionary. | **User preference** — optional dictionary feature toggle by presence/absence of the file path. |
| `SESSION-ID` | `src-worddic/record.c:1403,1420`, auto-generated in `conf.c:223-236` (`HOST-TIME-PID`) | Disambiguates concurrent processes' personal-dictionary sessions. | **Ephemeral** — generated fresh per process; not meaningful to persist. |
| `VERSION` | seeded automatically | Library version string available for `${VERSION}` substitution. | **Ephemeral** — informational, not user-settable in practice. |

**Important scope nuance:** because this is one flat table shared by the whole process with no
locking, calling `anthy_conf_override` from two contexts concurrently (e.g. two libpathime input
contexts on different threads) is a data race. If libpathime wants engine instances to have
independent dictionary/config paths, that is not possible with anthy-unicode as built — the
process only gets one anthy configuration.

### 1.2 Personality / dictionary selection (`anthy_set_personality`)

`anthy_set_personality(id)` (public, `anthy.h:56`) → `anthy_do_set_personality(id)`
(`src-main/context.c:717-733`, declared internal in `src-main/main.h:51` per the existing
`anthy-mapping.md` note). Selects which personal dictionary / learning-history "identity" new
contexts' dictionary sessions attach to (`anthy_dic_set_personality`, `src-worddic/word_dic.c:744`,
used for locating `$XDG_CONFIG_HOME/anthy/<personality>` or similar per-identity storage,
`priv_dic.c`).

**Critical, previously-undocumented scope constraint:** `anthy_do_set_personality` refuses to run
a second time — `if (current_personality) { return -1; }` (`context.c:720-722`). The personality
name is a **process-global static** (`current_personality`, `context.c:58`) that is set at most
**once** per process lifetime via the public API. If no personality is ever explicitly set, the
first call to `anthy_create_context()` lazily defaults it to `"default"` via `get_personality()`
(`context.c:71-77`), and that default then also locks in the same way — i.e. calling
`anthy_set_personality()` *after* the first context has been created will silently fail (return
`-1`) even though nothing about the call looks different.

- **Scope**: **Global-process, write-once.** Not per-context, and not re-settable through the
  public API without calling `anthy_quit()` then `anthy_init()` again (which also resets
  `current_personality` to `NULL` via `anthy_quit_personality()`/`anthy_init_personality()`,
  `context.c:736-748`) and destroying/recreating **every** context in the process, since contexts
  don't reference their creation-time personality independently — the dictionary session backing
  is shared.
- **Classification**: **User preference** in intent (which user's/domain's dictionary and learning
  history to use is exactly the kind of thing a settings UI would expose), but the write-once
  process-lifetime restriction makes it behave like a **global-process** option a client must
  decide *before* creating any input context, not a live-editable session toggle. A libpathime
  binding that wants to support switching personality mid-process (e.g. a "dictionary mode"
  property the user changes without restarting) cannot do so through the public API as documented
  in `anthy/anthy.h`.
- **What ibus-anthy actually does about this** (see §2.5 below): it does not use the public,
  write-once `anthy_set_personality()` at all. It calls the **internal** functions
  `anthy_init_personality()` (to reset the static guard back to `NULL`) and
  `anthy_do_set_personality()` directly through its GObject-introspection binding, every time the
  user picks a different dictionary mode from the property panel
  (`refs/ibus-anthy/engine/python3/engine.py:1024-1026`). This directly contradicts the "only
  `anthy_set_personality`/`anthy_conf_override` are public, don't bind
  `anthy_do_set_personality`/`anthy_init_personality`" guidance already recorded in
  `docs/anthy-mapping.md` and `TODO.md` — ibus-anthy's own reference implementation depends on the
  internal entry points to deliver a feature (live dictionary switching) that the public API
  cannot express. This is a fact worth flagging for the libpathime design step: supporting
  user-facing "switch dictionary without restart" either means reaching past the public header (as
  ibus-anthy does) or accepting the write-once restriction and requiring an engine-level
  reinitialize to change personality.

### 1.3 Encoding (`anthy_context_set_encoding`)

`anthy_context_set_encoding(ctx, encoding)` (`anthy.h:96`, `main.c:519-530`), values
`ANTHY_COMPILED_ENCODING` (0, whatever the library was built with), `ANTHY_EUC_JP_ENCODING` (1),
`ANTHY_UTF8_ENCODING` (2, per `anthy-mapping.md` this is what any UTF-8-based binding must set).

- **Default**: contexts are created with a process-global `default_encoding` static
  (`main.c:65,95`), hardcoded to `ANTHY_EUC_JP_ENCODING` inside `anthy_init()` — there is **no**
  public API to change this process-wide default; every new context starts as EUC-JP until told
  otherwise.
- **Scope**: **Per-context, mutable at any time.** `anthy_do_create_context(encoding)`
  (`context.c:244-275`) copies the global default into the new context's `ac->encoding` field at
  creation, but `anthy_context_set_encoding` can be called again afterward on the same context to
  change it (it only validates the value is one of the two real encodings and otherwise leaves the
  field untouched, `main.c:524-528`) — nothing in `anthy_set_string`/`anthy_get_segment`/etc.
  prevents changing it between conversions.
- **Classification**: **Could be either**, but leans **user preference at the binding layer, fixed
  at context-creation for the running system**. In practice a libpathime binding would set this
  exactly once right after `anthy_create_context()` (always to UTF-8) and never touch it again —
  it is not something an end user would toggle, but it is exactly the kind of "protocol negotiation
  option" CONCEPTS.md's negotiation section describes (declared once when the input context is
  created), not a runtime toggle.

### 1.4 Reconversion mode (`anthy_set_reconversion_mode`)

`anthy_set_reconversion_mode(ctx, mode)` (`anthy.h:98`, `main.c:533-545`). Values:
`ANTHY_RECONVERT_AUTO` (0, default — library inspects the string passed to `anthy_set_string` and
decides per-call whether it looks like text that needs reverse-conversion, `main.c:169-197`),
`ANTHY_RECONVERT_DISABLE` (1), `ANTHY_RECONVERT_ALWAYS` (2).

- **Scope**: **Per-context, mutable at any time.** Defaults to `ANTHY_RECONVERT_AUTO` at context
  creation (`context.c:272`); the setter validates and stores directly into `ac->reconversion_mode`
  with no other side effects, so it can be flipped between calls to `anthy_set_string`.
- **Classification**: **Ephemeral / could be either.** The *decision* of whether the current
  conversion call is a fresh composition or a reconversion of already-committed text is naturally
  a per-operation, transient choice (AUTO is almost always correct and most callers should never
  touch this). However, a client that always wants to force one behavior (e.g. an engine that
  never performs reconversion because its host client can't supply surrounding text) could
  reasonably set this once as a persistent per-installation preference and never change it again —
  hence "could be either" rather than a clean ephemeral/preference split.

### 1.5 Candidate ordering / sorting

`anthy/ordering.h` exposes `anthy_sort_candidate(segment_list*, nth)` and
`anthy_sort_metaword(segment_list*)` (`ordering.h:19-20`) plus process-global init hooks
`anthy_infosort_init()` / `anthy_relation_init()` called once from `anthy_init()`
(`main.c:91-92`). **There is no public, caller-settable option controlling *how* candidates are
ordered** — no flag for "frequency-based" vs "recency-based" vs "context-based" sorting is exposed
through `anthy/anthy.h`. Sorting behavior is internal to the library and driven by the personal
dictionary's learned statistics (via the `record`/`dic_personality` subsystem) and the
`ordering_context` built into each context automatically. The only externally visible lever over
candidate ordering is *indirect*: which personality/dictionary is active (§1.2) and what has been
learned via `anthy_commit_segment` (§1.6), both of which feed the same ordering machinery.

- **Classification**: N/A as a direct option — noted here only so the catalog is explicit that
  candidate-sort behavior is **not** independently configurable at the anthy-unicode API level,
  unlike e.g. libhangul's or pyzy's candidate list exposure (see the sibling mapping docs). A
  libpathime design that wants configurable candidate ordering for Japanese would have to either
  post-process the anthy-supplied order itself or accept anthy's built-in ordering as fixed.

### 1.6 Learning / personal-dictionary toggle

There is **no explicit "disable learning" flag**. Every call to `anthy_commit_segment` /
`anthy_commit_prediction` unconditionally records the selection into the active personality's
personal dictionary (`main.c:379-422`, `anthy_save_history` called once all segments of a
conversion are committed, `main.c:415-420`). The only environment-derived control is
`ANTHY_HISTORY_FILE` (read once via `getenv` inside `anthy_init()`, `main.c:98-101`, **not**
exposed through `anthy_conf_override`/the config table — it is a separate direct `getenv` call)
which names an additional flat-file history log; if unset, `history_file` stays `NULL` and
`anthy_save_history(NULL, ac)` is called (its behavior on a NULL filename is internal to
`context.c:569` and not investigated further here since it is outside the public API surface).

- **Scope**: **Global-process** (env-var read once at `anthy_init()` time, before any context
  exists) for the auxiliary history-file path; the personal-dictionary learning itself is
  **per-context implicitly** in the sense that every commit on every context writes through to the
  single active personality's storage — there is no per-context opt-out.
- **Classification**: **User preference** in spirit ("do you want this IME to learn from your
  choices" is a very standard IME setting — see ibus-anthy's dictionary-mode/admin exposure in
  §2.5) but anthy-unicode gives no direct API knob for it. A libpathime binding wanting a
  "disable learning" preference would have to simulate it by not calling `anthy_commit_segment`
  for the destructive/learning path, or by giving the user a throwaway personality name whose
  dictionary state is discarded.

### 1.7 Logging (`anthy_set_logger`)

`anthy_set_logger(anthy_logger callback, int level)` (`anthy.h:91-92`) installs a process-wide
callback (`void (*)(int level, const char *)`) used by `anthy_log()` throughout the library
(`logger.h:5-6`, implemented `src-diclib/logger.c:60`).

- **Scope**: **Global-process**, single callback slot, last writer wins, no per-context variant.
- **Classification**: **Ephemeral / developer-facing**, not a user preference — this is a
  diagnostics hook a host application wires up once at startup (comparable to a logging framework
  registration), not something end users configure or that persists as a saved value.

### 1.8 Romaji-kana table selection — explicitly *not* a library option

Per `TODO.md` finding 6 and `anthy-mapping.md`'s "What the library explicitly does not provide" section,
anthy-unicode has **no concept of keystrokes, romaji, or a kana keyboard table at all** — it only
accepts already-assembled hiragana strings via `anthy_set_string`. Any "romaji table" option
necessarily lives entirely in the caller (confirmed again while reading `main.c`/`anthy.h`: there
is no table-selection parameter anywhere in the public API). This is called out here explicitly
because it is one of the options most likely to be assumed as a library-level knob by analogy with
other IMEs — it is not. See §2.2 for where it actually lives.

---

## 2. ibus-anthy wrapper-level options

All settings in this section are read from `Gio.Settings` via the `AnthyPrefs` class
(`refs/ibus-anthy/setup/python3/anthyprefs.py:35`, `get_value` at line 89), backed by the schema
`refs/ibus-anthy/data/org.freedesktop.ibus.engine.anthy.gschema.xml.in`, and consumed inside
`refs/ibus-anthy/engine/python3/engine.py` via `self.__prefs.get_value(section, key)`. Being
GSettings-backed, **every key in this section is durably persisted (dconf) by construction** —
that is a property of the wrapper's chosen storage mechanism, not a statement about what each
value's *natural* lifetime should be, which is assessed independently per row below.

### 2.1 `common` schema — top-level behavior

| Key | Type | Default | What it controls | Maps to anthy API? | Classification |
|---|---|---|---|---|---|
| `input-mode` | int | `3` (Latin) | Which of `INPUT_MODE_{HIRAGANA,KATAKANA,HALF_WIDTH_KATAKANA,LATIN,WIDE_LATIN}` new text is interpreted as before any conversion (`engine.py:70-74,282-286`). | No — pure wrapper state; anthy never sees an input mode, only finished hiragana strings. | **Ephemeral for the live toggle, user preference for the startup default.** Users expect the *current* mode to be an in-session toggle (cycled with a hotkey) but also expect the engine to remember their last-used mode as the default for next time — genuinely dual-natured. |
| `typing-method` | int | `0` | Selects `TYPING_MODE_{ROMAJI,KANA,THUMB_SHIFT}` — which keystroke-to-kana state machine (`jastring.py`/`romaji.py`/`kana.py`/`thumb.py`) is active. | No — wrapper-only; see §1.8. | **User preference.** Typing method is a stable choice tied to the user's physical typing habit/keyboard, not something toggled per-composition. |
| `conversion-segment-mode` | int | `0` | Selects `SEGMENT_DEFAULT` / `SEGMENT_SINGLE` / `SEGMENT_IMMEDIATE` bit flags (`engine.py:91-93,443-447`) — multi- vs single-segment conversion, and whether conversion re-runs on every keystroke (immediate mode) vs. only on an explicit convert key. | No — a policy choice about *when* to call `anthy_set_string`/`anthy_resize_segment`, not an anthy setting itself. | **User preference.** A stable behavioral preference (some users strongly prefer immediate conversion); not naturally toggled mid-sentence. |
| `show-input-mode`, `show-typing-method`, `show-segment-mode`, `show-dict-mode`, `show-dict-config`, `show-preferences` | bool | mixed | Whether the corresponding item appears in the IBus property panel (language bar) at all (`engine.py:253,286,379,447,522,615`). | No — pure UI-surface toggle. | **User preference.** UI-density preferences, saved per user/desktop, no session-transient meaning. |
| `period-style` | int | `0` | Which full-width period glyph (`。`/`．`/etc.) `JaString._chk_text` substitutes into output text (`jastring.py:243-264`). | No — wrapper-side text post-processing before/after the hiragana ever reaches anthy. | **User preference.** Typographic/orthographic convention, essentially never changed mid-session. |
| `symbol-style` | int | `1` | Which bracket/symbol glyph set (`「`/`［`/etc.) is substituted, same mechanism as period-style. | No. | **User preference.** Same reasoning as `period-style`. |
| `ten-key-mode` | int (bool-like) | `1` | Whether numpad keys are remapped through `KP_Table` (full-width numerals) instead of passed through literally (`engine.py:1826-1827,1922-1923`). | No — pure keystroke-remapping policy. | **User preference.** Tied to whether the user wants numpad input treated as Japanese full-width digits; stable per user. |
| `behavior-on-focus-out` | int | `0` | Three-way policy for what happens to composition state when focus is lost/regained/reset: `0`=reset+invalidate, `1`=reset+invalidate but ask IBus to commit preedit via `PreeditFocusMode.COMMIT`, `2`=preserve state across focus-out (`engine.py:713-718,1046-1082`, `__update_preedit`). This directly implements one of the "Behavioral policies" CONCEPTS.md's negotiation section calls out ("what happens to preedit text when focus is lost"). | No — orchestrates *when* the wrapper calls its own `__reset()`/`__invalidate()`, not an anthy call. | **User preference**, matching CONCEPTS.md's own framing of focus-loss behavior as a negotiated policy rather than a fixed protocol rule — different users/clients legitimately want different answers. |
| `behavior-on-period` | int | `0` | What happens when a "trigger period" character (see `trigger-periods` below) is typed: `0`=nothing extra, `1`=auto-invoke convert, `2`=auto-commit (`engine.py:1786-1789,1957-1959`). | No — policy around when to call the wrapper's own convert/commit paths. | **User preference.** A workflow-automation preference (auto-convert-on-punctuation), not something meaningfully toggled per sentence. |
| `behavior-on-select-candidate` | int | `0` | What happens right after picking a candidate for a segment: `0`=advance focus to the next segment, `1`=immediately commit that segment (or the whole prediction) (`engine.py:2527-2534`). Per the ibus-anthy pinned-commit note in `anthy-mapping.md`, this is exactly the setting called out by the pinned `bjj/ibus-anthy@0962741` commit message. | Indirectly — mode `1` triggers `anthy_commit_segment`/`anthy_commit_prediction` immediately rather than deferring it, changing *when* the destructive/learning commit (§1.6) happens. | **User preference.** Changes a fairly fundamental interaction model (multi-segment stepping vs. commit-as-you-go); not a per-composition toggle. |
| `trigger-periods` | string | `',.、。，．'` | The literal set of characters treated as "period-like" for `behavior-on-period` (`engine.py:1785-1787`). | No. | **User preference.** Small, rarely-touched customization of the above. |
| `page-size` | int | `10` | Candidate-list page size fed into `IBus.LookupTable` (`engine.py:165,1051-1052`). Directly analogous to IBus's lookup-table page size that CONCEPTS.md explicitly excludes from the core model (candidate paging is a client concern) — this is exactly the kind of option CONCEPTS.md pushes to the client rather than the engine. | No — pure UI pagination, no anthy equivalent (anthy has no notion of pages, only a flat per-segment candidate array). | **User preference in ibus-anthy today, but per CONCEPTS.md this shouldn't be an engine option at all** — pagination is declared a client-side concern. Flagged here because it is presented as an engine setting in ibus-anthy's schema, illustrating a case where the reference wrapper's option surface is broader than what libpathime's boundary model should expose. |
| `show-lut-on-convert` | bool | `false` | Whether the candidate lookup table is shown immediately after conversion/prediction, vs. only after the user explicitly asks to see candidates (`engine.py:808-823,1132,2304,2369,2415,2495,2572`). | No. | **User preference.** Presentation-density preference. |
| `half-width-symbol`, `half-width-number`, `half-width-space` | bool | `false` | Additional `JaString._chk_text` / commit-time substitutions forcing half-width glyphs for symbols/digits/space (`jastring.py:243-247`, `engine.py:2218-2227`). | No. | **User preference.** Typographic convention, same family as period/symbol style. |
| `latin-with-shift` | bool | `true` | Whether holding Shift while in kana input mode temporarily types Latin characters (`engine.py:1690-1702`, propagated into `jastring.JaString.RESET`). | No — wrapper keystroke-interpretation policy. | **User preference.** A stable typing-habit setting; changing it live is supported (`CONFIG_VALUE_CHANGED` handler) but it is not something toggled per keystroke by the user themselves. |
| `shortcut-type` | string | `'default'` | Which named keybinding table (`default`/`atok`/`wnn`, see §2.6) is active. | No. | **User preference.** Directly a "which keymap do I use" choice. |
| `dict-admin-command`, `add-word-command`, `dict-config-icon` | array/string | Kasumi-related paths | External helper-program invocation for dictionary administration / adding a word (`engine.py:618-619,2833-2838`). | No — shells out to an external GUI tool; no anthy API involved beyond the dictionary files it edits. | **User preference** (deployment/integration configuration — which external tool to launch), effectively install-time. |
| `keyboard-layouts` | array of strings | list of XKB layout variants | Enumerates XKB layout choices surfaced for the user to pick a physical keyboard layout compatible with kana/thumb-shift input. | No — XKB/desktop integration, not anthy. | **User preference.** Hardware/layout binding, essentially install- or first-run configuration. |

### 2.2 `romaji-typing-rule` schema

`method` (string, default `'default'`) selects which named table in `list` (an `a{sv}` map of
rule-name → {input-fragment → kana-output} entries, e.g. `'default'`, with room for the
MS-IME/ATOK/Gairaigo/ANSI-BSI/historical-kana variants noted in the XML comments,
`org.freedesktop.ibus.engine.anthy.gschema.xml.in:151-450`) is used by `RomajiSegment`
(`romaji.py`) to resolve ASCII keystroke sequences to hiragana, entirely before anything is handed
to `anthy_set_string`. This is precisely the "romaji-kana conversion table selection" the task
description anticipated as possibly a library-level option — confirmed here to be **wrapper-only**,
consistent with `TODO.md` finding 6, that the backends take only finished input and anthy wants completed kana.

- **Scope**: **Wrapper-global** (`method` and `list` read once and cached at class level, refreshed
  via the `CONFIG_VALUE_CHANGED` GSettings-change callback which calls `jastring.JaString.RESET`,
  `engine.py:1694-1696`).
- **Classification**: **User preference.** The specific romanization convention (or a fully custom
  table) a user wants is a stable, persisted typing-style choice — not something that varies
  within a session.

### 2.3 `kana-typing-rule` schema

Same shape as §2.2 (`method`, default `'jp'`; `list` containing `'jp'` and `'us'` physical-keyboard
JIS-kana mappings) but for direct kana-keyboard input instead of romaji transliteration
(`kana.py`). Same scope/classification reasoning as §2.2 — **wrapper-global, user preference.**

### 2.4 `thumb` and `thumb-typing-rule` schemas

Nicola thumb-shift configuration: `keyboard-layout-mode`/`keyboard-layout` (which physical
thumb-shift keyboard layout), `fmv-extension` (FMV-extension level), `handakuten` (bool),
`rs`/`ls` (which physical keys act as the right/left "shift" keys, default `Henkan`/`Muhenkan`),
and `t1`/`t2` (integer **timing thresholds in milliseconds** used by the GLib timer in
`engine.py:__process_key_event_thumb` to disambiguate simultaneous vs. sequential key-release
timing — the one place this whole catalog touches release-timing behavior, which
`docs/CONCEPTS.md` explicitly excludes from its key-event model ("Key releases are not part of
this model... This excludes input methods that depend on release timing, such as thumb-shift
(Nicola) kana layouts" — CONCEPTS.md's own text calls this exact feature out as out-of-scope).
`thumb-typing-rule`'s `method`/`list` follow the same table-selection shape as §2.2/§2.3, with
several named hardware-specific tables (`base`, `nicola-j-table`, `nicola-a-table`,
`nicola-f-table`, `kb231-*-fmv-table`, `kb611-*-fmv-table`).

- **Scope**: **Wrapper-global**, all keystroke-interpretation, none of it touches anthy.
- **Classification**: **User preference** for the table/layout/key choices (tied to specific
  physical hardware a given user owns). The `t1`/`t2` timing thresholds are also **user
  preference** in the sense that they're tuned once for a user's typing speed and hardware, but
  are unusual among the options here in being numeric *tuning* values rather than mode selectors —
  worth flagging separately if libpathime ever supports thumb-shift, since CONCEPTS.md's
  synchronous, release-free key model cannot represent this feature at all without an extension.

### 2.5 `dict` schema — dictionary/personality selection

`template` (a `v`-typed tuple describing the shape of a dictionary entry), `list` (array of
`(id, short_label, description, ?, enabled, priority, ?, ?, ?, encoding)` tuples — the built-in
`embedded`/`zipcode`/`symbol`/`oldchar`/`era`/`emoji` dictionaries), `files` (map from dictionary
id to file path(s) on disk), `order` (user-customized dictionary priority order).

The `list`/`files`/`order` combination is surfaced as a set of checkable dictionary-mode entries in
the IBus property panel; picking one calls
`self.__context.init_personality(); self.__context.do_set_personality(str(dict_name))`
(`engine.py:1024-1026`) — i.e., every dictionary switch goes through the **internal**
`anthy_init_personality`/`anthy_do_set_personality` pair (§1.2), not the public
`anthy_set_personality`. This is the wrapper's way of working around the public API's write-once
restriction, at the cost of depending on non-public entry points.

- **Scope**: **Wrapper-global** for the persisted list/files/order (GSettings); the live personality
  switch itself inherits anthy's **global-process** scope (§1.2) — switching "personality" really
  means switching the one process-wide active personality, so it affects every context in the
  process simultaneously, not just the input context whose property panel triggered the change.
  This is a real cross-context coupling libpathime needs to be aware of: dictionary/personality is
  not an independent per-input-context setting in the underlying library no matter how it's
  presented in a UI.
- **Classification**: **User preference** for which dictionaries exist/are enabled and their
  priority order (`list`/`files`/`order` — persisted, rarely-changed). The live "which personality
  is active right now" switch is **could be either**: presented to the user as a live, instantly-
  effective toggle (like an input-mode switch) but implemented as a global, process-wide,
  identity-changing operation — a mismatch worth carrying into the libpathime design discussion.

### 2.6 `shortcut` schema — key bindings

Three named keybinding profiles (`default`, `atok`, `wnn`), each an `a{sv}` map from a logical
command name (`on_off`, `circle_input_mode`, `insert_space`, `backspace`, `commit`, `convert`,
`predict`, `cancel`, `reconvert`, `move_caret_*`, `select_*_segment`, `shrink_segment`,
`expand_segment`, `commit_*_segment`, `select_first/last/next/prev_candidate`,
`candidates_page_up/down`, `select_candidates_0`-`9`, `convert_to_*`, `dict_admin`, `add_word`,
`hiragana_for_latin_with_shift`, …) to a list of key-chord strings. `shortcut-type` (§2.1) selects
which profile is active; `__keybind` is rebuilt from it in `_mk_keybind` and consulted by
`__process_key_event_internal2`'s command dispatch (`engine.py:1708-1720`).

This is the concrete key-binding-table option the task asked about, and it directly maps onto
several of the "candidate navigation," "candidate paging," and "segment navigation" concerns that
`docs/CONCEPTS.md` explicitly excludes from the core engine/client boundary (candidate paging,
segment navigation and resizing, and candidate labels/shortcut keys are all named in CONCEPTS.md's
"Explicitly excluded concepts" list) — confirming that this entire options group is a client/UI
concern under the CONCEPTS.md model, fully absorbed into the wrapper here because ibus-anthy is
also acting as the client.

- **Scope**: **Wrapper-global** (class-level `__keybind`, shared by all `Engine` instances in the
  process; rebuilt on `shortcut-type`/`shortcut` GSettings changes, `engine.py:1685-1690`).
- **Classification**: **User preference.** Key bindings are a textbook persisted per-user setting;
  no plausible ephemeral interpretation.

---

## 3. Cross-cutting observations for the libpathime design step

- **Only two anthy-unicode options are genuinely per-context and mutable at any time**:
  `anthy_context_set_encoding` (§1.3) and `anthy_set_reconversion_mode` (§1.4). Everything else
  meaningful at the library level — personality/dictionary (§1.2), config-table variables (§1.1),
  history file (§1.6), logger (§1.7) — is process-global, and personality is additionally
  write-once through the public API. A libpathime engine that wants to expose "per-context"
  personality or dictionary selection as advertised by an IBus-style property panel is working
  against the grain of the underlying library exactly as ibus-anthy is (§2.5) and will face the
  same choice: use the internal write-many functions (breaking the "public API only" constraint
  recorded in `TODO.md`) or accept a single global personality shared by every input context the
  process serves.
- **The overwhelming majority of "options" a user would recognize as IME settings — input mode,
  typing method (romaji/kana/thumb), segment mode, focus-out behavior, candidate page size, key
  bindings, punctuation/half-width style — have no anthy-unicode counterpart whatsoever.** They are
  entirely invented and owned by ibus-anthy, because anthy-unicode's public surface is a pure
  string-conversion engine (per `anthy-mapping.md`'s "no key-event API" mismatch). This matches
  `TODO.md` finding 6: the key-event/romaji-kana/handled layer, and by extension almost this
  entire options catalog, is libpathime's to design, not anthy's to expose.
- **Options common in spirit to what other engines will need** (candidate-list-related,
  conversion-behavior-related, encoding-related — flagged per the task's request to distinguish
  common vs. Japanese-specific options): `page-size`/`show-lut-on-convert` (candidate-list
  presentation, analogous to what any candidate-producing engine's client would need — though per
  CONCEPTS.md this belongs to the client, not the engine, so arguably it shouldn't appear in
  libpathime's engine-options surface at all), `behavior-on-select-candidate` (commit-immediately
  vs. advance-focus is a pattern any segment/region-based conversion engine could have, compare
  pyzy's provisional focused-candidate tracking per `TODO.md` finding 2), `behavior-on-focus-out`
  (any engine with transient composition state needs a reset/preserve/commit policy — this is
  effectively CONCEPTS.md's own "what happens to preedit text when focus is lost" negotiated
  policy, named explicitly in `docs/CONCEPTS.md`'s Negotiation section), and
  `anthy_context_set_encoding`/text-encoding handling (every wrapped engine has *some* encoding
  story — compare `TODO.md` finding 4 on libhangul's UCS-4/UTF-8 split and pyzy's byte-offset
  cursor).
- **Options unique to Japanese kana-kanji conversion**: typing-method/romaji-table/kana-table/
  thumb-shift selection (§2.2-2.4, no counterpart in Hangul or Pinyin input — Hangul composes jamo
  directly from keystrokes with no transliteration table, and pyzy's Pinyin/Bopomofo table
  selection is fixed at context-creation per `docs/pyzy-mapping.md`, not a live-swappable table set),
  period/symbol half-width style (§2.1, Japanese-specific typographic conventions), the
  segment-based `conversion-segment-mode` / segment-resize keybindings (§2.6, a direct consequence
  of anthy's per-segment candidate model described in `anthy-mapping.md` mismatch #1), and the
  personal-dictionary "personality" concept itself (§1.2/§2.5) with its write-once/global quirk,
  which has no analog in libhangul (no persistent per-user learning surfaced at the API level) and
  only a partial analog in pyzy (`PROPERTY_MODE_SIMP` for simplified/traditional, not a learning
  identity).
