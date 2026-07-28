# libhangul Configuration Options

This document catalogs every runtime-configurable option exposed at the API level by
`libhangul` (Korean Hangul syllable composition) and every option the reference wrapper
`refs/ibus-hangul` adds on top of it. It complements `docs/libhangul-mapping.md`, which maps
the libhangul API to `docs/CONCEPTS.md` terms; this document is specifically about
*configuration/options*, not the general API shape.

For each option we note: what it controls, its **scope** (global-process,
per-input-context-at-creation-only, or per-input-context-mutable-anytime), and a
**lifetime classification** — Ephemeral, User preference, or Could be either — with
justification. "Ephemeral" and "User preference" are used in the sense of
`docs/adapter-findings.md`'s two-layer lifetime model (finding 3) (global/process init vs. per-context state); an option's
classification here is about where its *value* naturally belongs (session-only vs.
persisted-across-sessions), which is a separate axis from where its *storage* lives (global
struct vs. per-context struct).

---

## 1. libhangul library-level options

Source consulted: `libhangul/hangul/hangul.h` (public API declarations),
`libhangul/hangul/hangulinputcontext.c` (implementation), `libhangul/hangul/hangulkeyboard.c`
(keyboard registry).

### 1.1 Summary table

| Option | API | Scope | Default | Classification |
|---|---|---|---|---|
| Keyboard registry init (external keyboard directories) | `hangul_init(path)` / `hangul_fini()` (`hangul.h:99-102`) | Global process, one-time | n/a (registry starts empty; built-ins always resolve) | User preference (of the *installation*, not the session) |
| Keyboard layout selection | `hangul_ic_new(keyboard)` (creation), `hangul_ic_select_keyboard(hic, id)` (mutable anytime) (`hangulinputcontext.c:1470-1482`, `1502-1536`) | Per-context, mutable anytime | whatever id the caller passes to `hangul_ic_new`; library falls back to `"2"` if `id == NULL` (`hangulinputcontext.c:1477-1478`) | User preference |
| Keyboard sub-table switch | `hangul_ic_switch_keyboard_table(hic, tableid)` (`hangulinputcontext.c:1437-1444`) | Per-context, mutable anytime | `tableid = 0`, reset to `0` by `hangul_ic_set_keyboard` (`hangulinputcontext.c:1433-1434`) | Ephemeral |
| `HANGUL_IC_OPTION_AUTO_REORDER` | `hangul_ic_set_option()` / `hangul_ic_get_option()` (`hangul.h:92-96`, `hangulinputcontext.c:1327-1378`) | Per-context, mutable anytime | `false` (`hangulinputcontext.c:1526`) | User preference |
| `HANGUL_IC_OPTION_COMBI_ON_DOUBLE_STROKE` | same | Per-context, mutable anytime | `false` (`hangulinputcontext.c:1527`) | User preference |
| `HANGUL_IC_OPTION_NON_CHOSEONG_COMBI` | same | Per-context, mutable anytime | `true` (`hangulinputcontext.c:1528`) | User preference |
| Output mode (`HANGUL_OUTPUT_SYLLABLE` vs `HANGUL_OUTPUT_JAMO`) | `hangul_ic_set_output_mode(hic, mode)` (`hangulinputcontext.c:1380-1388`) | Per-context, mutable anytime (unless `use_jamo_mode_only` is set — see note) | `HANGUL_OUTPUT_SYLLABLE`, set explicitly in `hangul_ic_new` (`hangulinputcontext.c:1530`) | Could be either |
| Translate callback | `hangul_ic_connect_callback(hic, "translate", cb, data)` / `hangul_ic_connect_translate()` (`hangulinputcontext.c:1390-1399`, `1412-1425`) | Per-context, mutable anytime | `NULL` (`hangulinputcontext.c:1518-1519`) | Ephemeral (see note) |
| Transition callback | `hangul_ic_connect_callback(hic, "transition", cb, data)` / `hangul_ic_connect_transition()` (`hangulinputcontext.c:1401-1409`, `1412-1425`) | Per-context, mutable anytime | `NULL` (`hangulinputcontext.c:1521-1522`) | Ephemeral (see note) |
| Keyboard registration (custom `HangulKeyboard*`) | `hangul_keyboard_list_register_keyboard()` / `..._unregister_keyboard()` (`hangul.h:114-115`) | Global process | n/a | User preference |
| `hangul_ic_set_combination()` | deprecated, **no-op** (`hangulinputcontext.c:1484-1488`) | n/a | n/a | n/a — dead API, ignore |

### 1.2 Detail and justification

**Keyboard registry init — `hangul_init()` / `hangul_fini()`.**
Gated behind the `ENABLE_EXTERNAL_KEYBOARDS` compile flag (`hangul.h:99-102`). Loads
`HangulKeyboard` definitions from external files under a `:`-separated directory list (or a
built-in default path if `NULL`) into the process-global static `hangul_keyboards` list in
`hangulkeyboard.c:227`. **Crucially, the nine built-in layouts do not require this call** —
`hangul_keyboard_list_get_keyboard()` (`hangulkeyboard.c:1169-1185`) first searches the
(possibly-empty) registered list, then falls back to
`hangul_builtin_keyboard_list_get_keyboard()` unconditionally. So `hangul_init`/`hangul_fini`
is purely about *discovering additional, file-defined keyboards*; it is a global,
process-lifetime, one-time (non-reentrant, not thread-safe per `docs/adapter-findings.md` finding 3) operation, matching
the "global/process init" layer of the two-layer lifetime model already identified in `docs/adapter-findings.md`.
Classified as a user preference in the sense that *which extra keyboards exist* is an
installation/deployment-time choice, not a per-session toggle — but it is not itself a
"setting with a value" so much as an initialization step.

**Keyboard layout selection.**
Chosen at `hangul_ic_new(const char* keyboard)` (`hangulinputcontext.c:1502-1536`) and freely
re-selectable afterward via `hangul_ic_select_keyboard(hic, id)`
(`hangulinputcontext.c:1470-1482`), which resolves the id through the registry/builtin lookup
and calls `hangul_ic_set_keyboard()`. The doc comment states this "does not affect the internal
composition state," i.e. switching layout mid-composition is explicitly supported and does not
reset the in-progress syllable (`hangulinputcontext.c:1466-1467`). **Hazard** (already flagged in
`docs/libhangul-mapping.md`): an unknown id resolves to `NULL` and is not reported as an error —
the next `hangul_ic_process()` call dereferences it and crashes. Callers must validate ids
against `hangul_keyboard_list_get_count()` / `..._get_keyboard_id()` (or the deprecated
`hangul_ic_get_n_keyboards()` / `..._get_keyboard_id()`) before use.
**Classification:** User preference — this is the canonical "which physical/phonetic layout do I
type on" choice, analogous to an OS keyboard-layout setting; it is meaningless to reset per
session and users expect it to persist.

**Keyboard sub-table switch — `hangul_ic_switch_keyboard_table()`.**
Selects among multiple internal tables a single `HangulKeyboard` may define
(`HangulKeyboard.table[4]`, `hangulkeyboard.c` struct definition) — e.g. a shift-state or
mode-dependent sub-table within one logical layout. Distinct from selecting a different
`HangulKeyboard` entirely. Reset to `0` whenever `hangul_ic_set_keyboard()` runs.
**Classification:** Ephemeral — it tracks a transient sub-state of the current keyboard/typing
session (akin to which shift-level table is active), reset on layout change, and has no
independent meaning to persist across sessions on its own (any product-level equivalent would
likely be re-derived from the persisted layout choice, not saved separately).

**`HANGUL_IC_OPTION_AUTO_REORDER`.**
Per the Korean doc comment (`hangulinputcontext.c:1348-1350`): allows automatic reordering of
choseong/jungseong for "moa-chigi" (모아치기, chorded/simultaneous) typing — e.g. typing ㅏ then
ㄱ still composes to 가. Default `false`.
**Classification:** User preference — this changes fundamental composition behavior to suit a
specific typing style (simultaneous key presses on mechanical keyboards); once a user picks a
value it should persist, and ibus-hangul indeed persists it (see §2).

**`HANGUL_IC_OPTION_COMBI_ON_DOUBLE_STROKE`.**
Per the doc comment (`hangulinputcontext.c:1351-1356`): on 2-set (dubeolsik) layouts, allows
doubling a consonant key in quick succession to produce a tensed consonant (e.g. ㄱ+ㄱ → ㄲ),
for compatibility with MS IME behavior. Explicitly a no-op on 3-set/Old-Hangul layouts where the
keyboard already has dedicated keys for tensed consonants. Default `false`.
**Classification:** User preference — a compatibility/typing-habit switch that a user sets once
based on which other IME they are used to, not something that should reset each session.

**`HANGUL_IC_OPTION_NON_CHOSEONG_COMBI`.**
Per the doc comment (`hangulinputcontext.c:1357-1361`): on 2-set layouts, allows combining
consonant clusters that are not valid choseong (syllable-initial) clusters, e.g. ㄱ+ㅅ → ㄳ, again
for MS IME compatibility. Also a no-op outside 2-set layouts. Default `true` (note this differs
from the other two options, which default `false`).
**Classification:** User preference, same reasoning as COMBI_ON_DOUBLE_STROKE.

**Output mode — `hangul_ic_set_output_mode()`.**
Chooses whether `hangul_ic_get_preedit_string()` / `..._get_commit_string()` /
`..._flush()` return precomposed syllables (`HANGUL_OUTPUT_SYLLABLE`) or decomposed jamo
(`HANGUL_OUTPUT_JAMO`). This is the closest libhangul analogue to what other engines might call
an "output encoding" or "romanization vs. native script" toggle. There is a per-context guard
field `use_jamo_mode_only` (`hangulinputcontext.c:207-219`) that, if set, makes
`hangul_ic_set_output_mode()` a silent no-op (`hangulinputcontext.c:1386-1387`) — **but in the
current source, `use_jamo_mode_only` is initialized to `FALSE` in `hangul_ic_new()`
(`hangulinputcontext.c:1524`) and is never set to `true` anywhere else in the library.** It
therefore currently behaves as dead/vestigial state — a hook for a future or historical mode
that forces jamo-only output regardless of caller request, but nothing in the shipped library
activates it.
**Classification:** Could be either. Argument for user preference: a keyboard-layout-adjacent
display choice some users may want fixed (e.g. accessibility or linguistic-study tools that
always want decomposed jamo). Argument for ephemeral/derived: most consumers will want this tied
mechanically to the selected keyboard type (e.g. an "Old Hangul" layout may want jamo output) or
to a display-only feature toggle in the client, not saved as independent user state. The tension
is exactly this: is it an independent axis of user choice, or a derived property of another
choice (layout) that shouldn't be separately persisted?

**Translate / transition callbacks — `hangul_ic_connect_callback()`.**
Two named hooks, dispatched through one function (`hangulinputcontext.c:1412-1425`) or the
typed convenience wrappers `hangul_ic_connect_translate()` /
`hangul_ic_connect_transition()`. `on_translate` is invoked from `hangul_ic_process()`
(`hangulinputcontext.c:1088-1089`) to let the caller override/inspect the ASCII→jamo mapping
before composition; `on_transition` is invoked (`hangulinputcontext.c:460,480`) to let the
caller veto a proposed jaso-buffer transition (return `false` to block it — used by
ibus-hangul as a fallback implementation of auto-reorder on older libhangul versions lacking
`HANGUL_IC_OPTION_AUTO_REORDER`, see `refs/ibus-hangul/src/engine.c:605-608`).
**Classification:** Ephemeral. These are not "settings" with a persisted value at all — they are
function-pointer hooks wired up once per context by the *caller's code*, not by end-user choice.
The caller's decision of *whether* to install a hook is a code-level/build-level decision (as
the ibus-hangul fallback shows); there is no end-user-facing value to persist. Listed here
because they are configuration-shaped (they change per-context behavior) even though they are
not data-valued options.

**Keyboard registration — `hangul_keyboard_list_register_keyboard()` /
`..._unregister_keyboard()`.**
Adds/removes a `HangulKeyboard*` to/from the global registry independent of the file-loading
path used by `hangul_init()`. Registered keyboards are searched in reverse-registration order
before falling back to built-ins (`hangulkeyboard.c:1169-1185`), so a later registration can
shadow an id that also exists as a built-in or earlier-registered custom keyboard.
**Classification:** User preference (of the installation) — same reasoning as `hangul_init`.

**`hangul_ic_set_combination()` (deprecated).**
Declared with `LIBHANGUL_DEPRECATED` and implemented as an empty function body
(`hangulinputcontext.c:1484-1488`) — calling it has **no effect at all**. Included here only so
it is not mistaken for a live option; do not carry it forward into libpathime's design.

### 1.3 Not found / explicitly absent at the library level

- No capability or purpose negotiation (confirmed also in `docs/libhangul-mapping.md`
  "Negotiation" row).
- No candidate-list-related option in the composition core (the separate `HanjaTable` API has no
  configuration surface either — it is a pure load-and-query dictionary with no runtime option).
- No maximum-candidate-list-size option (there is no candidate list in the core at all).
- No punctuation, full/half-width, or locale-style options — out of scope for a
  syllable-composition-only library.
- No thread-safety / concurrency options — the two-layer model (global init vs. per-context) from
  `docs/adapter-findings.md` finding 3 is the only concurrency-relevant structure, and neither layer documents thread safety
  guarantees.

---

## 2. ibus-hangul wrapper-level options

Sources consulted: `refs/ibus-hangul/data/org.freedesktop.ibus.engine.hangul.gschema.xml` (the
GSettings schema — the authoritative list of persisted settings) and
`refs/ibus-hangul/src/engine.c` (where every key is read, applied, and in most cases re-applied
live on change via the `settings-changed` signal, `engine.c:1927-2005`).

### 2.1 Summary table

| GSettings key | Type | Default | Maps to libhangul? | Applied where | Classification |
|---|---|---|---|---|---|
| `hangul-keyboard` | string | `'2'` | Yes — `hangul_ic_select_keyboard()` | Constructor (`engine.c:601`) + live on change (`engine.c:1944-1948`) | User preference |
| `initial-input-mode` | string (`latin`/`hangul`) | `'latin'` | No — wrapper-invented `input_mode` field | Constructor only (`engine.c:613`); live-changed value affects only *future* new contexts | User preference |
| `word-commit` | boolean | `false` | No — **read into a global but never consulted anywhere else in `engine.c`** (dead/vestigial in this source tree; see note) | n/a | N/A (see note) |
| `auto-reorder` | boolean | `true` | Yes — `HANGUL_IC_OPTION_AUTO_REORDER` (when available) | Constructor (`engine.c:602-604`) + live on change (`engine.c:1956-1961`) | User preference |
| `switch-keys` | string, comma-separated hotkeys | `'Hangul,Shift+space'` | No — purely wrapper hotkey list | Init + live (`engine.c:416-424`, `1963-1966`) | User preference |
| `hanja-keys` | string, comma-separated hotkeys | `'Hangul_Hanja,F9'` | No — purely wrapper hotkey list | Init + live (`engine.c:428-436`, `1949-1952`) | User preference |
| `on-keys` | string, comma-separated hotkeys | `''` (empty) | No — purely wrapper hotkey list | Init + live (`engine.c:438-444`, `1967-1970`) | User preference |
| `off-keys` | string, comma-separated hotkeys | `'Escape'` | No — purely wrapper hotkey list | Init + live (`engine.c:446-452`, `1971-1974`) | User preference |
| `disable-latin-mode` | boolean | `false` | No — wrapper-invented; forces `input_mode = INPUT_MODE_HANGUL` and blocks switching | Constructor + every mode-switch check (`engine.c:619-621`, `1867-1880`) | User preference |
| `use-event-forwarding` | boolean | `true` | No — wrapper protocol workaround | Init + live (`engine.c:483-487`, `1983-1985`); consulted per key event (`engine.c:1588`) | Could be either (see note) |
| `preedit-mode` | string enum (`none`/`syllable`/`word`) | `'syllable'` | No — wrapper-invented display strategy | Init (as `global_preedit_mode`) + live; per-instance copy `hangul->preedit_mode` set at construction and recomputed via `ibus_hangul_engine_update_preedit_mode()` (`engine.c:788-798`) | Could be either (see note) |
| *(non-schema)* `hangul_mode` vs `hanja_mode` toggle | in-memory only, `IBusProperty` | `hanja_mode = FALSE` at construction (`engine.c:615`) | No | Property-panel click handler (`engine.c:1795-1808`) | Ephemeral |
| *(non-schema)* current `input_mode` (Hangul/Latin) | in-memory only, per-instance | seeded from `initial-input-mode` | No (drives `hangul_ic_*` calls indirectly by gating whether keys reach libhangul at all) | `ibus_hangul_engine_switch_input_mode()` / `..._set_input_mode()` (`engine.c:1831-1888`) | Ephemeral |
| *(non-schema)* `input_purpose` / content type | in-memory only, per-instance | `IBUS_INPUT_PURPOSE_FREE_FORM` (`engine.c:614`) | No — gates whether libhangul is invoked at all (bypassed entirely for `IBUS_INPUT_PURPOSE_PASSWORD`, `engine.c:1404`) | `ibus_hangul_engine_set_content_type()` (`engine.c:2076-2084`), called by the client/framework, not the user | Ephemeral |
| `org.freedesktop.ibus.panel` / `lookup-table-orientation` | int32 (foreign schema) | framework default | No — Hanja `IBusLookupTable` display only | Init + live (`engine.c:502-506`, `1997-2001`) | Could be either — shared across all engines, arguably a desktop-wide preference rather than an engine-specific one |

### 2.2 Detail and justification

**`hangul-keyboard`.** Directly forwarded to `hangul_ic_select_keyboard()` both at construction
and live on GSettings change (applied to the specific engine instance that received the signal,
via the `user_data` bound at `g_signal_connect(settings_hangul, "changed", ..., hangul)`, so each
open input context updates itself independently even though the underlying `GSettings` object
and its cached values — `hangul_keyboard`, `switch_keys`, etc. — are process-wide globals in
`engine.c`, not per-instance). **Classification:** User preference — same reasoning as the
library-level option it wraps.

**`initial-input-mode`.** Purely wrapper state: seeds `input_mode` when an `IBusHangulEngine`
instance is constructed. Changing it live only affects contexts created *afterward*; it does not
retroactively change already-open contexts (unlike `hangul-keyboard` and `auto-reorder`, which
are re-applied to the live context on change). **Classification:** User preference — "what mode
do I want to start typing in" is exactly the kind of thing a user sets once in a settings panel
and expects respected every session.

**`word-commit`.** Declared in the schema, read into the global `word_commit` at both init
(`engine.c:456`) and on live change (`engine.c:1954`) — but grep across all of
`refs/ibus-hangul/src/*.c` shows no other reference to the `word_commit` variable anywhere. It is
**dead/vestigial** in this snapshot of the source tree: its apparent original purpose (delay
commit until a whole word is finished) is now implemented instead by the `preedit-mode = word`
setting (`PREEDIT_MODE_WORD`, described in `docs/libhangul-mapping.md` "Multi-syllable preedit
buffer"). Flagged as N/A rather than classified, since it currently controls nothing; worth
knowing so libpathime's design doesn't cargo-cult a redundant setting.

**`switch-keys` / `hanja-keys` / `on-keys` / `off-keys`.** Four independent hotkey lists, each
parsed by `hotkey_list_set_from_string()` from a comma-separated string like
`'Hangul,Shift+space'` into a `HotkeyList` of `(keyval, modifier-mask)` pairs, matched via
`hotkey_list_match()` (masking out CapsLock/NumLock, `engine.c:2027-2039`). `switch-keys` toggles
Hangul/Latin mode; `hanja-keys` opens the Hanja candidate list; `on-keys`/`off-keys` force
Hangul-mode on/off unconditionally (distinct from *toggling*) and are empty/single-bound by
default. All four are purely wrapper-level: libhangul has no concept of a hotkey or of key
events at all beyond the single ASCII-per-`hangul_ic_process()` call.
**Classification:** User preference for all four — hotkey bindings are a canonical example of
something users customize once (to avoid clashing with other software or match muscle memory
from another IME) and expect to persist across every session.

**`disable-latin-mode`.** When true, forces every new context to start in (and calling
`ibus_hangul_engine_set_input_mode()` to refuse leaving) Hangul mode — effectively disables the
Hangul/Latin toggle feature entirely, turning this engine into a Hangul-only input method.
**Classification:** User preference — a deployment/user choice about whether the Latin passthrough
feature should exist at all, analogous to disabling a feature flag; not meaningful to vary
within a single session.

**`use-event-forwarding`.** A protocol-level workaround: when enabled, `process_key_event()`
always reports the key as handled and instead calls `ibus_engine_forward_key_event()` explicitly
for anything libhangul declined, to route around an IBus ordering bug between commit/preedit
updates and the handled-key return (`engine.c:1588` and surrounding comment). **Classification:**
Could be either. This is really a compatibility flag for the *client/framework version*, not a
matter of taste — in an ideal world it would be auto-detected (as `check_client_commit()` already
does for a related quirk, `engine.c:509`) rather than exposed as a user-facing setting at all. Its
presence as a GSettings key is arguably a symptom of the library needing to work around a
framework bug it can't detect reliably; whether libpathime should expose an equivalent depends on
whether its own client/framework integrations have comparable protocol quirks. Tension: expose it
as an escape-hatch persisted setting (safe, discoverable) vs. treat it as purely
internal/auto-negotiated (cleaner, but leaves no user recourse if auto-detection is wrong).

**`preedit-mode`.** Selects among `none` (surrounding-text-driven display, no IBus preedit),
`syllable` (standard one-syllable-at-a-time preedit — the natural libhangul default), and `word`
(wrapper-side multi-syllable buffering, see `docs/libhangul-mapping.md` "Multi-syllable preedit
buffer"). Notably this is *not* simply applied as read: `ibus_hangul_engine_update_preedit_mode()`
downgrades `none` back to `syllable` at runtime when the connected client lacks
`IBUS_CAP_SURROUNDING_TEXT` (`engine.c:788-798`), i.e. the effective value is
`min(user's persisted preference, what this client supports)`. **Classification:** Could be
either. Argument for user preference: some users may have a stable reason to prefer word-buffered
composition (e.g. it changes undo granularity and how quickly text commits). Argument for
ephemeral/derived: the *effective* value already depends on a live, per-context negotiated
fact (client capability), so treating it as a single persisted global is already an approximation
— a more capability-aware design might compute it per input-context rather than store one global
"preference" that gets silently overridden.

**`hanja_mode` property toggle (not in GSettings).** A boolean flag on each `IBusHangulEngine`
instance, always starting `FALSE`, toggled only by the user clicking the Hanja-mode property in
the IBus panel (`engine.c:1795-1808`) — never read from or written to GSettings, so it does not
survive engine restart. While active, every committed syllable feeds the Hanja candidate lookup
instead of (or in addition to) normal commit. **Classification:** Ephemeral — this is a clear
example of "mode currently active" state, analogous to a Caps Lock indicator: meaningful only for
the current session, and re-arming it automatically at every new session (rather than
remembering "last time you closed the app, Hanja mode was on") is almost certainly the expected
behavior. Contrast with `hangul-keyboard`, which *is* persisted, to see the ephemeral/preference
line: layout choice is stable identity, Hanja-mode-active is a transient "am I mid-conversion"
flag.

**Current `input_mode` (Hangul/Latin), live value.** Distinct from `initial-input-mode` (the
seed): this is the actual current mode of a running context, changed by hitting a switch-key or
programmatically. Not persisted; each new context reseeds from `initial-input-mode`.
**Classification:** Ephemeral — a live toggle state, with the *starting* value being the
persisted preference (`initial-input-mode`) and the *current* value being session-only, exactly
the split CONCEPTS.md draws between negotiated/persistent per-context settings and transient
composition state.

**`input_purpose` / content type.** Set by the client framework (not the user) via
`ibus_hangul_engine_set_content_type()` whenever the focused field's purpose changes (e.g.
navigating into a password field). When the purpose is `IBUS_INPUT_PURPOSE_PASSWORD`, key events
bypass libhangul entirely and go straight to the parent class (`engine.c:1404`), i.e. Hangul
composition is unconditionally disabled in password fields regardless of any other setting.
**Classification:** Ephemeral, and notably **not user-controlled at all** — it is negotiated
per-field by the client, matching the "Input purpose and hints" section of `CONCEPTS.md`
("Negotiation" → "Input purpose and hints") almost exactly. Included here because it functions as
a configuration input to the engine even though no persisted value is involved.

**`lookup-table-orientation` (foreign schema `org.freedesktop.ibus.panel`).** Read from a
different GSettings schema shared by the whole IBus desktop session, not owned by this engine.
Purely a display/orientation choice for the Hanja candidate window.
**Classification:** Could be either — from this engine's point of view it behaves like a user
preference (persisted, applies to every session), but the tension is that it is *not*
engine-specific: it is a desktop-wide setting this engine merely reads, which argues that
libpathime should treat "how are candidates laid out" as a client-side/global concern rather
than something each engine negotiates individually (consistent with `CONCEPTS.md`'s explicit
exclusion of "candidate layout or orientation" from the core model).

### 2.3 Settings applied once vs. re-applied live

Cross-referencing `ibus_hangul_init()` (process-wide read at IBus daemon startup,
`engine.c:390-512`) against `settings_changed()` (per-key live handler,
`engine.c:1927-2005`) shows an inconsistency worth flagging for libpathime's own design:

- **Re-applied to a running context on change:** `hangul-keyboard`, `auto-reorder`.
- **Re-read into the global cache but only affects *newly constructed* contexts:**
  `initial-input-mode`, `switch-keys`, `hanja-keys`, `on-keys`, `off-keys`,
  `use-event-forwarding`, `preedit-mode` (with the capability-downgrade caveat above),
  `lookup-table-orientation`.
- **Read but never consulted anywhere:** `word-commit` (dead).

Only two of the ten schema keys are genuinely "live" in the sense of affecting an already-focused,
already-composing input context; the rest are effectively construction-time parameters that
happen to be re-read from a global on every settings change (so *new* contexts pick them up
immediately, but already-open ones do not retroactively change hotkeys or preedit strategy
mid-session). This is a real design data point: most of these options behave, in practice, like
per-context-creation-time configuration rather than continuously-live settings, even though
GSettings' change-notification mechanism could in principle support the latter for all of them.

---

## 3. Cross-cutting observations for libpathime

- **Common-in-spirit vs. Hangul-specific.** Two categories of option recur here that would be
  expected in *any* wrapped engine, per the mapping docs' framing (`docs/adapter-findings.md`'s cross-cutting
  findings):
  - **Keyboard/layout selection** (`hangul-keyboard`) — every engine with more than one physical
    or phonetic input scheme needs an analogous option (pyzy has phonetic-mode-at-creation
    only, per `docs/*-mapping.md`; anthy has none, being kana-based).
  - **Composition-behavior toggles** (the three `HANGUL_IC_OPTION_*` flags) — engine-specific in
    their *meaning* (moa-chigi reordering, MS-IME-compatible double-stroke/cluster combination)
    but structurally identical to option categories other engines expose differently (e.g. pyzy's
    `PROPERTY_MODE_SIMP` simplified/traditional toggle, noted in `docs/*-mapping.md` "Per-engine
    specifics").
  - **Hotkey bindings** (`switch-keys`, `hanja-keys`, `on-keys`, `off-keys`) and **mode-active
    flags** (`hanja_mode`, `input_mode`) are wrapper-invented, not library-provided, but every
    other reference wrapper is expected to need equivalents (mode-switch hotkeys, a
    candidate-window-open hotkey) since none of the three libraries has a hotkey or key-event
    concept at all (`docs/adapter-findings.md` finding 6: the entire key-event → handled →
    preedit-assembly half of `docs/CONCEPTS.md` lives in libpathime).
  - **Output encoding** (`HANGUL_OUTPUT_SYLLABLE`/`JAMO`) is the closest libhangul analogue to a
    "candidate/output form" option seen elsewhere (cf. pyzy's simplified/traditional Chinese,
    `docs/*-mapping.md`), though it is about *composition* output form rather than a
    *candidate list*, since libhangul's composition core has no candidate list at all.
  - Options that are **unique to Hangul composition** and have no clear analogue in the other two
    engines: `HANGUL_IC_OPTION_AUTO_REORDER` (moa-chigi/simultaneous-stroke reordering — specific
    to Hangul's choseong/jungseong/jongseong assembly model) and both double-stroke/cluster
    combination options (specific to 2-set layout ambiguity resolution). These map to no concept
    in `docs/CONCEPTS.md` and are not mirrored by anything in the anthy or pyzy mapping notes.
  - **No candidate-list-related option exists at the libhangul-composition level at all** — the
    only candidate list in this stack (Hanja) belongs to a bolt-on dictionary lookup, not the
    composition core, so "maximum candidate list size," "candidate sort order," etc. (categories
    that likely *do* apply to anthy and pyzy) have zero libhangul analogue and are entirely
    wrapper-invented in ibus-hangul's `IBusLookupTable` handling.

- **Options with no independent user-facing value at all:** the translate/transition callbacks
  (code-level hooks, not settings) and `hangul_ic_set_combination()` (dead no-op). Neither should
  inform libpathime's *options* design, though the callback hook pattern itself (letting a caller
  intercept a translation/transition decision) is a distinct extensibility mechanism worth
  separating conceptually from "configuration options with values."

- **The persisted-vs-live inconsistency in §2.3** is a cautionary example: a GSettings-backed
  design does not automatically make every option "live" from the engine's perspective — the
  actual application point in `engine.c` determines that, and ibus-hangul mostly treats its
  settings as construction-time parameters re-read into globals. If libpathime wants genuinely
  live-mutable options, that needs to be a deliberate per-option design decision, not an assumed
  consequence of a persistence mechanism.
