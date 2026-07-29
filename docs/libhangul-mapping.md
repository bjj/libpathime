# libhangul API and Concept Mapping

This document maps the public API of libhangul to the IME concepts defined in
`docs/CONCEPTS.md`, documents what the library provides directly, describes
what `refs/ibus-hangul/src/engine.c` adds on top of it, and enumerates the
impedance mismatches between the library model and the project's model.

---

## Library overview

libhangul is a C library that implements Korean Hangul syllable composition.
It accepts a stream of ASCII key codes (one per key press), assembles them into
Hangul jamo (consonants and vowels), combines the jamo into syllables according
to the rules of Korean orthography, and reports two output strings after each
key:

- **preedit string** — the syllable currently being assembled, as a
  NUL-terminated `ucschar*` (UCS-4) string of at most one syllable.
- **commit string** — any syllable that became complete and ready to be
  inserted into the document as a result of the most recent key press.

The library also ships a Hanja (Chinese-character) dictionary (`hanja.c`) and a
set of lookup functions for converting Korean text to Hanja. This dictionary is
separate from the syllable-composition core.

---

## API concept mapping

| CONCEPTS.md term        | libhangul element                                                                                      |
|-------------------------|--------------------------------------------------------------------------------------------------------|
| **Engine**              | No direct object. Composition state lives in `HangulInputContext`, but the library is **not** fully stateless: the keyboard registry (`hangul_keyboards`) is a process-global file-scope static in `hangulkeyboard.c`, mutated by `hangul_init()` / `hangul_fini()`. Built-in layouts resolve without `hangul_init`; external/custom keyboards require it. |
| **Input context**       | `HangulInputContext` (opaque struct, `engines/libhangul/hangul/hangulinputcontext.c`). Created with `hangul_ic_new(const char* keyboard)`, destroyed with `hangul_ic_delete()`. **Hazard:** an unknown keyboard id is not reported — `hangul_ic_select_keyboard()` sets `keyboard = NULL` and the first `hangul_ic_process()` then dereferences it and crashes. The caller must validate the id before use. |
| **Key event**           | A plain C `int ascii` value passed to `hangul_ic_process(HangulInputContext*, int ascii)` or `hangul_ic_backspace(HangulInputContext*)`. The value is expected to be a US-QWERTY ASCII code; modifier state is expressed only by case (uppercase = Shift held). |
| **Handled**             | Boolean return value of `hangul_ic_process()` and `hangul_ic_backspace()`. `true` = handled; `false` = unhandled (the caller must forward the event). |
| **Unhandled**           | `hangul_ic_process()` / `hangul_ic_backspace()` returns `false`.          |
| **Preedit text**        | `const ucschar* hangul_ic_get_preedit_string(HangulInputContext*)` — UCS-4 string, valid until the next call that mutates `hic`. At most one syllable's worth of composition, but that may be **1–3 UCS-4 codepoints** (e.g. choseong + `HANGUL_JUNGSEONG_FILLER`, non-precomposable jaso combinations, or decomposed jamo under `HANGUL_OUTPUT_JAMO` mode). |
| **Commit text**         | `const ucschar* hangul_ic_get_commit_string(HangulInputContext*)` — UCS-4 string, valid until the next call that mutates `hic`. **Not limited to one syllable:** a single `hangul_ic_process()` can leave a completed syllable *followed by* an appended character (up to the internal `commit_string[64]` buffer). In particular a printable **non-jamo ASCII** character *may be* appended to the commit string with the call returning `true` (handled), so libhangul swallows and re-emits it rather than declining it — but **only when the selected layout's table carries a non-zero entry for that key**, which makes the behaviour layout-dependent. See mismatch #3. |
| **Reset**               | `void hangul_ic_reset(HangulInputContext*)` — clears preedit, commit, and flushed strings and resets the internal jamo buffer; does not commit. |
| **Flush (forced commit)** | `const ucschar* hangul_ic_flush(HangulInputContext*)` — serializes the pending jamo buffer into a **separate** `flushed_string[64]` buffer and returns *that*. It first clears the preedit, commit, **and** flushed strings, so `hangul_ic_get_commit_string()` is **empty** afterward; the caller must use the flush return value directly. This is distinct from the internal flush-to-commit path (`hangul_ic_flush_internal`) used during normal composition. Intended for context-switch scenarios, where the caller wants the pending syllable kept rather than discarded. |
| **Composition data**    | Partially covered. `hangul_ic_get_preedit_string()` provides preedit text. No auxiliary text concept. No candidate list concept. |
| **Auxiliary text**      | No equivalent in libhangul.                                               |
| **Candidate list**      | No equivalent in libhangul for Hangul composition. The Hanja subsystem (`HanjaTable`, `HanjaList`) provides a candidate list for Hanja conversion, but it is a separate lookup table, not integrated into the composition loop. |
| **Select candidate**    | No equivalent in libhangul. Hanja selection is performed entirely by the caller. |
| **Surrounding text**    | No equivalent. libhangul does not receive or use surrounding text.        |
| **Delete surrounding text** | No equivalent. libhangul does not request surrounding-text deletion.  |
| **Activation**          | No equivalent. The caller decides when to route keys to libhangul. libhangul has no focus concept either, which is part of why CONCEPTS.md has none: the caller decides when to call `hangul_ic_reset()` or `hangul_ic_flush()`, and nothing in the library wants to be told about a transition. |
| **Forward key event**   | No equivalent. libhangul reports an unhandled key by returning `false`; the caller is responsible for forwarding it. |
| **Negotiation**         | Partially covered. `hangul_ic_set_option()` sets per-context options (`HANGUL_IC_OPTION_AUTO_REORDER`, `HANGUL_IC_OPTION_COMBI_ON_DOUBLE_STROKE`, `HANGUL_IC_OPTION_NON_CHOSEONG_COMBI`). `hangul_ic_set_output_mode()` selects `HANGUL_OUTPUT_SYLLABLE` (precomposed) vs `HANGUL_OUTPUT_JAMO` (decomposed) for preedit/commit/flush text. Keyboard layout is selected at creation time, changed with `hangul_ic_select_keyboard()`, or its sub-table switched with `hangul_ic_switch_keyboard_table()`. Note `hangul_ic_set_combination()` is **deprecated and a no-op**. No capability or purpose negotiation. |

**Hanja dictionary API** (separate from composition):

| Element                          | Purpose                                                                         |
|----------------------------------|---------------------------------------------------------------------------------|
| `HanjaTable`                     | Loaded dictionary file (`hanja_table_load(const char* filename)`).              |
| `HanjaList`                      | Result set from a lookup (`hanja_table_match_exact/prefix/suffix`).             |
| `hanja_list_get_size()`          | Number of candidates in a result set.                                           |
| `hanja_list_get_nth_value()`     | The Hanja character string for candidate *n* (UTF-8).                           |
| `hanja_list_get_nth_key()`       | The Hangul key string that produced candidate *n* (UTF-8).                      |
| `hanja_list_get_nth_comment()`   | A comment string for candidate *n* (UTF-8); used as auxiliary text by ibus-hangul. |
| `hanja_list_delete()`            | Frees a result set.                                                             |

---

## What the library provides

libhangul directly covers the following parts of a working Hangul IME engine:

**Syllable composition.** The core function `hangul_ic_process(hic, ascii)`
implements the full jamo-to-syllable state machine. It consults the selected
keyboard layout table (stored in `HangulKeyboard`) to map the ASCII code to a
Hangul jamo codepoint, then advances the composition state held in
`HangulBuffer` (which tracks `choseong`, `jungseong`, and `jongseong`). Three
composition strategies are supported depending on keyboard type:
`hangul_ic_process_jamo()` (standard 2-set and 3-set layouts),
`hangul_ic_process_jaso()` (jaso-style layouts), and
`hangul_ic_process_romaja()` (romanization input).

**Preedit and commit output.** After each `hangul_ic_process()` call the caller
reads the updated preedit and commit strings. The library handles the timing of
when a syllable is committed (e.g. when a new syllable is started or a
consonant sequence is ambiguous).

**Backspace handling.** `hangul_ic_backspace()` removes the last jamo from the
current composition buffer and updates the preedit accordingly, returning
`true` if the backspace was consumed by the composition state and `false` if
the buffer was already empty.

**Handled / unhandled reporting.** Both `hangul_ic_process()` and
`hangul_ic_backspace()` return a boolean indicating whether the key was
consumed by the composition logic. This maps directly to the CONCEPTS.md
`Handled` / `Unhandled` distinction.

**Reset.** `hangul_ic_reset()` discards all transient composition state without
committing it. `hangul_ic_flush()` serializes any remaining composition into a
separate flushed buffer and returns it (and clears the commit string in the
process — read the flush return value, not `hangul_ic_get_commit_string()`).

**Composition state predicates.** `hangul_ic_is_empty()`,
`hangul_ic_has_choseong()`, `hangul_ic_has_jungseong()`, and
`hangul_ic_has_jongseong()` let a caller inspect composition state without
serializing it — useful for deciding commit/flush/reset policy at the boundary.

**Translation / transition callbacks.** `hangul_ic_connect_callback(hic,
"translate" | "transition", ...)` lets a caller intercept ASCII→jamo translation
and veto jaso transitions. This has no CONCEPTS.md analogue but is a real
per-context hook for custom layout logic.

**Keyboard layouts.** The library ships built-in tables for the standard 2-set
layout, two-set old Korean, 3-set layouts (390, 3-final, 3-sun, and others),
and romanization. External custom keyboards can be loaded from files.

**Per-context options.** Three boolean options can be set via
`hangul_ic_set_option()`: `HANGUL_IC_OPTION_AUTO_REORDER` (allow
choseong/jungseong reordering), `HANGUL_IC_OPTION_COMBI_ON_DOUBLE_STROKE`
(enable combination on double stroke), and
`HANGUL_IC_OPTION_NON_CHOSEONG_COMBI` (allow combinations not starting from
choseong position).

**Hanja lookup.** The `HanjaTable` / `HanjaList` API provides exact, prefix,
and suffix lookups of Hangul keys against a Hanja dictionary file. This is
an optional add-on used to provide a Hanja candidate list.

**Hangul classification utilities.** `hangulctype.c` exports a family of
`hangul_is_*()` predicate functions and jamo-to-syllable / syllable-to-jamo
conversion functions. These are helpers for callers that need to inspect or
decompose Hangul text.

**Encoding split (library-wide).** The composition APIs work in **UCS-4**
(`ucschar` = `uint32_t`): `hangul_ic_get_preedit_string`,
`hangul_ic_get_commit_string`, and `hangul_ic_flush` all return
`const ucschar*`. The entire Hanja API, by contrast, returns **UTF-8**
(`hanja_list_get_nth_value/key/comment`). A wrapper must convert Hanja UTF-8 ↔
UCS-4 at the boundary and settle on one canonical Unicode-scalar unit for
commit/composition text. All returned string pointers are **borrowed and
volatile** — they point into fixed internal `[64]` buffers that the next
mutating call (`process`, `backspace`, `reset`, `flush`) overwrites; the commit
buffer is cleared at the *start* of `process`/`backspace`, so read it before the
next key. Anything that must survive must be copied immediately.

---

## What the IBus wrapper adds

All of the following is implemented in `refs/ibus-hangul/src/engine.c` (and the
helper `refs/ibus-hangul/src/ustring.c`).

**Multi-syllable preedit buffer (`UString* preedit`).** libhangul's own preedit
string is always at most one syllable. `IBusHangulEngine` keeps an internal
`UString* preedit` (a GArray of `ucschar`) that accumulates all syllables
committed from libhangul but not yet committed to the application. In
`PREEDIT_MODE_WORD` or `hanja_mode`, syllables flow from the libhangul commit
string into this buffer rather than straight to the application, allowing a
whole word to be held as preedit until conversion is confirmed.

**Preedit mode selection.** Three preedit modes are defined in the wrapper:
`PREEDIT_MODE_SYLLABLE` (standard one-syllable IBus preedit), `PREEDIT_MODE_WORD`
(multi-syllable preedit held until conversion), and `PREEDIT_MODE_NONE` (no IBus
preedit; surrounding text is used as the display mechanism instead).
`ibus_hangul_engine_update_preedit_mode()` selects the appropriate mode based
on client capabilities (`IBUS_CAP_SURROUNDING_TEXT`).

**Hangul / Latin mode switching.** libhangul has no concept of input modes.
The wrapper adds a two-state `input_mode` field (`INPUT_MODE_HANGUL` /
`INPUT_MODE_LATIN`). In Latin mode, key events are passed directly to the
parent IBus class rather than to libhangul. Mode toggling is driven by
configurable hotkey lists (`switch_keys`, `on_keys`, `off_keys`).

**Hanja (Sino-Korean) candidate list.** `ibus_hangul_engine_update_hanja_list()`
assembles a lookup key from the current preedit buffer plus (when using
surrounding text) up to 32 characters of preceding context, queries
`HanjaTable` via exact, prefix, or suffix match, and populates an
`IBusLookupTable`. The candidate list is activated by a configurable hotkey
(`hanja_keys`, default `Hangul_Hanja` / `F9`) or kept permanently active in
`hanja_mode`. Candidate selection is handled inside
`ibus_hangul_engine_process_candidate_key_event()` using numeric keys 1–9,
Return, Escape, and directional arrows; `ibus_hangul_engine_candidate_clicked()`
handles mouse clicks on the lookup table.

**Hanja commit with surrounding-text deletion.** When a Hanja candidate is
confirmed, `ibus_hangul_engine_commit_current_candidate()` computes how many
characters must be removed from both the internal preedit buffer and
(if necessary) the surrounding text, calls
`ibus_engine_delete_surrounding_text()` to remove them, then commits the Hanja
value string.

**Auxiliary text from Hanja comments.** The Hanja comment string (`hanja_list_get_nth_comment()`)
is displayed in the IBus auxiliary text bar via
`ibus_engine_update_auxiliary_text()` in
`ibus_hangul_engine_update_lookup_table_ui()`.

**Key normalisation for non-US keyboard layouts.** Before passing a key event
to libhangul, `ibus_hangul_engine_process_key_event()` uses `ibus_keymap_lookup_keysym()`
with a US keymap to convert the key's physical position (`keycode`) to the
US-QWERTY equivalent `keyval`. This is necessary because libhangul's internal
keyboard tables are indexed by US-QWERTY ASCII codes. The normalisation is
skipped when the selected input method is a transliteration type (detected via
`hangul_ic_is_transliteration()`).

**CapsLock compensation.** After normalisation, the wrapper swaps the case of
letter keysyms when `IBUS_LOCK_MASK` is set, because libhangul treats
uppercase as "Shift held" and CapsLock should not activate the shifted
keyboard mapping.

**Key-event forwarding workaround.** When `use_event_forwarding` is enabled,
the wrapper always returns `TRUE` from `process_key_event()` (marking the key
as consumed) and then explicitly calls `ibus_engine_forward_key_event()` for
any key that libhangul did not handle. This works around an IBus synchronisation
problem where clients could receive commit and preedit updates out of order
relative to an unhandled-key return.

**Focus-in: property registration and preedit refresh.** `ibus_hangul_engine_focus_in()`
calls `ibus_engine_register_properties()` to display the mode indicator in the
IBus panel and calls `ibus_hangul_engine_update_preedit_text()` to redisplay
any buffered preedit.

**Focus-out: conditional reset.** `ibus_hangul_engine_focus_out()` calls
`hangul_ic_reset()` and clears the internal preedit buffer when no Hanja list
is active. IBus's `IBUS_ENGINE_PREEDIT_COMMIT` mode causes the preedit text
to be committed automatically on focus-out, so an explicit commit call is
omitted in the normal path. If a Hanja list is open, the lookup table and
auxiliary text are hidden but composition is not reset.

**Activation / deactivation (enable / disable).** `ibus_hangul_engine_enable()`
requests surrounding text from the client (via
`ibus_engine_get_surrounding_text(engine, NULL, NULL, NULL)`, which signals to
IBus that the engine wants surrounding-text notifications).
`ibus_hangul_engine_disable()` delegates to `focus_out`.

**Preedit caret sanity check.** In `PREEDIT_MODE_NONE`, before processing each
keystroke `ibus_hangul_engine_check_caret_pos_sanity()` fetches surrounding
text and compares it against the cached preedit buffer. If the client's text
no longer matches (indicating the user moved the caret externally), the
libhangul context and the internal preedit buffer are reset.

**GSettings-based configuration.** The wrapper reads all its settings at
startup and on change from the `org.freedesktop.ibus.engine.hangul` GSettings
schema. This covers keyboard selection, hotkeys, word-commit, auto-reorder,
disable-latin-mode, initial input mode, event-forwarding toggle, and preedit
mode. libhangul itself has no configuration subsystem beyond
`hangul_ic_set_option()`.

**Property UI.** `IBusPropList` containing mode-toggle properties
(`prop_hangul_mode`, `prop_hanja_mode`) and a setup launcher are managed
entirely by the wrapper.

**Content-type / input-purpose awareness.** `ibus_hangul_engine_set_content_type()`
stores the IBus input purpose. When the purpose is `IBUS_INPUT_PURPOSE_PASSWORD`,
key events bypass all libhangul processing and are handled by the parent class.

---

## Impedance mismatches

### 1. Key event representation

**Project concept:** A key event is an abstract structure that may carry a
logical key, a physical key, and modifier state. Modifier state is part of the
event.

**libhangul provides:** A single `int ascii` — a US-QWERTY ASCII code. Modifier
state is implicit: uppercase means Shift. There is no physical key, no explicit
modifier mask, and no key release. The library does not distinguish between
"key press" and "key release" events; it only processes presses.

**Bridge required:** The wrapper must convert from the framework's key event
(IBus provides `keyval`, `keycode`, `modifiers`) to an ASCII code before
calling libhangul. This involves: (a) mapping `keycode` through a US-QWERTY
keymap to obtain the position-based keyval; (b) normalising CapsLock; (c)
discarding release events (`IBUS_RELEASE_MASK`); and (d) discarding
events with Control, Alt, Super, or other non-Shift modifiers.

### 2. Preedit text scope: one syllable only

**Project concept:** Preedit text is a single plain-text string of arbitrary
length representing the entire current composition.

**libhangul provides:** A preedit string that is at most one syllable's worth of
composition — though that syllable may serialize to **1–3 UCS-4 codepoints**
(fillers, non-precomposable jaso combinations, or decomposed jamo under
`HANGUL_OUTPUT_JAMO`). Once a syllable is complete, it moves to the commit string
and the preedit resets. There is no multi-syllable preedit buffer in the library.

**Bridge required:** If the application wants to show a whole word (or
multi-syllable sequence) as preedit, the wrapper must accumulate committed
syllables in its own buffer (`UString* preedit` in ibus-hangul) and construct
the full preedit string by concatenating that buffer with the current libhangul
preedit.

### 3. Commit text granularity: one syllable per key

**Project concept:** Commit text is a request to insert finalised text into
the client; quantity and timing are engine-defined.

**libhangul provides:** Usually zero or one syllable per `hangul_ic_process()`
call, but **not strictly** — a single call can leave a completed syllable
*followed by* an appended non-jamo character (up to the buffer size), and on
some layouts such a printable non-jamo ASCII character is committed with the
call returning `true` (handled). The commit string is valid only until the next
call to `hangul_ic_process()` or any other mutating function, and is overwritten
at the *start* of the next `process`/`backspace`. The text belongs to an
internal fixed-size buffer (`commit_string[64]` in `_HangulInputContext`) and
must be consumed immediately.

**Which keys get absorbed is layout-dependent**, measured while writing the
adapter. The absorb-and-re-emit branch (`hangulinputcontext.c:612-616`, and the
identical one in `hangul_ic_process_romaja`) fires on the value *after*
translation, not on the raw ASCII, and `hangul_ic_map_to_char()` answers 0 for a
key the selected layout's table does not carry:

- On the **two-set** layouts (`2`, `2y` — the default and the common case) the
  tables map letters only. All 43 non-letter printables map to 0, are declined,
  and reach the client untouched. Nothing is swallowed.
- On the **three-set**, ahnmatae and romaja layouts every printable except
  space is absorbed, because those layouts genuinely reassign the symbol
  positions — on Sebeolsik Final `?` is `!` and `{` is `%`.

**Bridge required:** The wrapper must read the commit string after every call
and act on it before the next call. In word-commit mode it must buffer
committed syllables and delay the actual commit to the application until the
word boundary.

The layout-dependence above decides which keys a wrapper may filter for itself:
intercepting punctuation defensively "to be safe" is correct-looking on two-set
and hands a three-set user the US-QWERTY character their layout explicitly
reassigns. The safe rule is to offer every printable ASCII key to libhangul and
honour its verdict. What is *not* libhangul's to decide is the word boundary: a
key it declines closes a held word.

### 4. No flush concept distinct from reset

**Project concept:** Reset discards transient state without committing. The
model has no focus, so nothing in it decides "what happens on focus-out" — a
client that wants the pending syllable kept finalizes it before it stops
sending keys.

**libhangul provides:** `hangul_ic_reset()` discards without committing.
`hangul_ic_flush()` serializes any in-progress syllable into a separate
`flushed_string` buffer and returns it (clearing the commit string as a side
effect), but does not interact with any client. Note this means the flush return
value is the only place the flushed text appears — `hangul_ic_get_commit_string()`
is empty after a flush.

**Bridge required:** The adapter must decide, when the client leaves a field
(and on receiving Control/Alt/etc. keystrokes), whether to call
`hangul_ic_reset()` (discard) or `hangul_ic_flush()` (commit the remaining
syllable). The ibus-hangul wrapper
uses the IBus `IBUS_ENGINE_PREEDIT_COMMIT` mode to delegate this to IBus where
possible, and calls `hangul_ic_reset()` directly otherwise.

### 5. No surrounding text support

**Project concept:** Surrounding text is a first-class input to the engine,
used for context-sensitive conversion and reconversion. Availability is
negotiated.

**libhangul provides:** No surrounding text concept whatsoever. The library
operates entirely on the key stream it receives.

**Bridge required:** All surrounding-text use is added entirely by the wrapper.
In ibus-hangul, surrounding text is used for two purposes: (a) in
`PREEDIT_MODE_NONE`, the pre-cursor text is used as the display buffer for
composing text, and a sanity check compares it against the internal preedit
cache to detect external cursor movement; (b) for Hanja lookup, up to 32
characters before the cursor are prepended to the current preedit to form a
longer lookup key, enabling suffix-based Hanja lookup.

### 6. No candidate list concept in the core

**Project concept:** The candidate list is part of composition data. `Select candidate`
is an explicit API call that identifies a candidate by its absolute position.

**libhangul provides:** No candidate list in the composition core. The Hanja
dictionary API (`HanjaList`) is a separate lookup facility: the caller constructs
a key string, queries the table, iterates over results by index, and decides
what to commit. The library does not track a "current candidate" position; that
is left entirely to the caller.

**Bridge required:** The wrapper must build and maintain the candidate list
data structure (`IBusLookupTable` in ibus-hangul), navigate it in response to
key events, translate page-relative selection events into absolute list
positions, delete the appropriate preedit/surrounding text, and commit the
selected Hanja value. None of this is provided by libhangul.

### 7. No auxiliary text concept

**Project concept:** Auxiliary text is an optional supplemental string
associated with the current composition.

**libhangul provides:** None. The Hanja comment string
(`hanja_list_get_nth_comment()`) is the closest analogue, but it is per-candidate
data in the Hanja dictionary, not a composition-level concept.

**Bridge required:** The wrapper extracts the comment for the currently
highlighted Hanja candidate and pushes it to the auxiliary text channel.

### 8. No activation / deactivation lifecycle

**Project concept:** The model has neither activation nor focus. This section
records what the reference wrapper does with the lifecycle events IBus gives
it, because that is what a reader coming from ibus-hangul will expect to find.

**libhangul provides:** No lifecycle callbacks. The library is pure state
machine; it has no concept of being "active" or "focused". The caller is
entirely responsible for deciding when to route key events to the library and
when to call reset or flush.

**Bridge required:** The wrapper implements `enable`, `disable`, `focus_in`,
and `focus_out` entirely in its own code. The only libhangul calls involved
are `hangul_ic_reset()` / `hangul_ic_flush()` at lifecycle boundaries, and
`hangul_ic_get_surrounding_text()` indirectly through the IBus API on enable.

### 9. No negotiation subsystem

**Project concept:** Negotiation is a structured exchange of capabilities,
field purpose, options, and policies between engine and client.

**libhangul provides:** `hangul_ic_set_option()` for three boolean flags, and
`hangul_ic_select_keyboard()` / `hangul_ic_set_keyboard()` for keyboard
selection. There is no facility for the library to declare what it needs from
the client, and no facility for the client to describe the field type.

**Bridge required:** The wrapper reads all configuration from GSettings and
applies it to libhangul via `hangul_ic_set_option()`. Client capability
information (from `ibus_hangul_engine_set_capabilities()`) is inspected by the
wrapper itself to select preedit mode and decide whether surrounding text is
available; libhangul never sees this information.

### 10. Key event character encoding: ASCII, not Unicode keysyms

**Project concept:** The key event carries "the logical key" without prescribing
the encoding.

**libhangul provides:** The input must be an ASCII byte (0–127), specifically
the US-QWERTY character that corresponds to the physical key position. This
makes libhangul's key event concept narrower than any Unicode-keysym framework.

**Bridge required:** When the host keyboard is not US-QWERTY (e.g. a French or
German physical keyboard layout), or when the framework delivers Unicode keysyms
rather than physical-position codes, the wrapper must remap the key to its
US-QWERTY equivalent before calling libhangul. In ibus-hangul this is done by
calling `ibus_keymap_lookup_keysym()` with a US keymap using the raw
hardware `keycode`.

### 11. Backspace is a special case, not a regular key

**Project concept:** Backspace is a key event like any other; the engine
handles or declines it through the same `handled` mechanism.

**libhangul provides:** A separate function `hangul_ic_backspace()` rather than
routing backspace through `hangul_ic_process()`. The comment in the library
documentation (in Korean) explains this was done because mapping backspace to
ASCII is awkward. The function also modifies only the composition buffer, not
any surrounding client text.

**Bridge required:** The wrapper must intercept the Backspace keysym before
calling libhangul, call `hangul_ic_backspace()` instead of `hangul_ic_process()`,
and—when `hangul_ic_backspace()` returns `false` (buffer already empty)—
additionally erase one character from its own internal multi-syllable preedit
buffer before deciding the key was handled.
