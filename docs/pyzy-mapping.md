# pyzy API and Concept Mapping

This document maps the pyzy (libpyzy) C++ library and its IBus wrapper
(`refs/ibus-pinyin`) to the project's canonical IME concepts defined in
`docs/CONCEPTS.md`.

## Library overview

libpyzy is a C++ shared library for Chinese Pinyin and Bopomofo phonetic
conversion. Its sole public header is `engines/pyzy/src/InputContext.h` (upstream
installs it as `<PyZy/InputContext.h>`, capital-P directory, and this build stages
it into that layout to compile the adapter against); the constants in
`engines/pyzy/src/Const.h` and the tagged-union type in `engines/pyzy/src/Variant.h` are also
part of the public surface. Everything else in `engines/pyzy/src/` is an internal
implementation detail.

The conversion model is phonetic-to-phrase:

1. The caller feeds raw ASCII keystrokes one character at a time via
   `InputContext::insert(char ch)`. The library parses these into a
   sequence of pinyin syllables (or Bopomofo symbols) and maintains a
   cursor position within that raw input.
2. pyzy runs phrase-lookup against a bundled SQLite database and a
   user-phrase database, producing an ordered candidate list on demand.
3. The candidate list is lazy: only the candidates that have been
   explicitly requested via `hasCandidate()` or `getCandidate()` are
   prepared. `getPreparedCandidatesSize()` returns how many have been
   prepared so far.
4. Results are decomposed into three named text segments — `selectedText`,
   `conversionText`, `restText` — plus an `auxiliaryText` string. These
   represent the parts of the preedit as the user selects candidates in
   sequence.
5. The library fires state-change notifications synchronously through a
   virtual `InputContext::Observer` interface. The caller overrides this
   class to receive callbacks.

Three input modes are supported, selected at `InputContext::create()` time:
`FULL_PINYIN`, `DOUBLE_PINYIN`, and `BOPOMOFO`. Conversion options
(typo-correction rules, fuzzy-pinyin pairs, Bopomofo keyboard layout,
double-pinyin schema, simplified/traditional mode, special-phrase lookup)
are set per-context via `setProperty()` using the `PropertyName` enum and
`Variant` wrapper type.

---

## API concept mapping

| CONCEPTS.md concept | pyzy API element |
|---|---|
| **Client** | The code that owns a `PyZy::InputContext::Observer` subclass and holds the `InputContext*` pointer. pyzy has no built-in notion of a client; the caller is the client. |
| **Engine** | `PyZy::InputContext` is the engine object. There is no separate factory or engine registry; the factory method is `InputContext::create()`. |
| **Input context** | `PyZy::InputContext*` — created by `InputContext::create(InputType, Observer*)`, destroyed by `delete`. Each instance carries independent composition state. |
| **Key event** | No key-event type exists in pyzy. The caller decomposes key events itself and calls individual mutation methods (`insert(char)`, `removeCharBefore()`, `moveCursorLeft()`, etc.). |
| **Handled / Unhandled** | Each mutation method returns `bool`: `true` if the operation changed state (approximately "handled"), `false` if it had no effect. For full pinyin, `insert(char)` returns `false` **only for an invalid character** (`!islower(ch) && ch != '\''`, `FullPinyinContext.cc:41-51`); when the buffer is full (`MAX_PINYIN_LEN == 64`) it returns `true` and **silently drops** the character. So the bool is not a clean "handled" signal. There is no concept of forwarding an unhandled event. |
| **Forward key event** | Not present. pyzy never asks the caller to deliver a key event to a downstream recipient. |
| **Composition data** | The combination of `selectedText()`, `conversionText()`, `restText()`, and `auxiliaryText()` accessors, plus the lazy candidate list. These are delivered via separate `Observer` callbacks rather than as a single composite value. The adapter projects `selectedText()` + `auxiliaryText()` and *not* `conversionText()` — see "Auxiliary text" below. |
| **Preedit text** | Decomposed into three `std::string` segments returned by `selectedText()`, `conversionText()`, and `restText()`. The internal `Preedit` struct (`PhoneticContext.h:40-50`) holds fields `selected_text`, `candidate_text`, and `rest_text` — note the accessor `conversionText()` returns the `candidate_text` field (the accessor and struct-field names differ). The full preedit string must be assembled by concatenation. `preeditTextChanged(InputContext*)` is the notification callback. |
| **Auxiliary text** | Not a concept in this model — `pathime_composition_t` has no such field. pyzy's `auxiliaryText()` is read all the same, but as the *preedit*; see below. |
| **Candidate list** | Accessed via `hasCandidate(size_t index)` and `getCandidate(size_t index, Candidate& output)`. `Candidate` is a struct with `std::string text` and `CandidateType type` (`NORMAL_PHRASE`, `USER_PHRASE`, `SPECIAL_PHRASE`). The list is unbounded and lazily populated. **`hasCandidate(i)` is not a const query** — it loops calling `PhraseEditor::fillCandidates()` until index `i` is reachable, materializing candidates as a side effect (`PhoneticContext.cc:231-250`). The flat index space is `special_phrases` first, then phrase-editor candidates: indices `[0, m_special_phrases.size())` are special phrases; indices `>= m_special_phrases.size()` map into the phrase editor via `i -= m_special_phrases.size()` (`PhoneticContext.cc:166-277`). `candidatesChanged(InputContext*)` is the notification callback. `getPreparedCandidatesSize()` returns how many entries have been materialized. |
| **Select candidate** | `selectCandidate(size_t index)` — 0-origin absolute index into the current candidate list. Returns `bool`. If selecting the candidate exhausts the remaining input, pyzy fires `commitText` automatically. Otherwise it updates the three preedit segments and fires `preeditTextChanged`. |
| **Surrounding text** | Not present. pyzy has no API to accept or use surrounding text from the client. The adapter uses the client's snapshot for the punctuation look-behinds, which are ours rather than pyzy's. |
| **Commit text** | `Observer::commitText(InputContext*, const std::string&)` — fires when pyzy decides to emit finalized text. The caller can also force a commit by calling `InputContext::commit(CommitType)` directly (`TYPE_RAW`, `TYPE_PHONETIC`, or `TYPE_CONVERTED`). |
| **Delete surrounding text** | Not present. pyzy never issues a delete-surrounding-text request. |
| **Activation** | Not present. pyzy has no enable / disable callbacks, and no focus-in / focus-out ones either — part of why CONCEPTS.md has neither concept. |
| **Commit (forced)** | `InputContext::commit(CommitType)` exists, but this adapter does not use it: the committed text is derived from the composition model, so that "what is committed is the preedit the client was last shown" holds by construction. Under **double pinyin** `TYPE_CONVERTED` would emit the raw keystrokes ("nihk" for a preedit reading "ni hao"), which is exactly the case that rules it out. |
| **Reset** | `InputContext::reset()` — discards all transient composition state and returns to empty. Does not commit; the caller commits first if it wants to preserve the preedit. |
| **Negotiation** | Partially covered by `setProperty(PropertyName, Variant)`. The **five** properties (`InputContext.h:176-209`) are `PROPERTY_CONVERSION_OPTION` (unsigned int bitmask), `PROPERTY_DOUBLE_PINYIN_SCHEMA` (unsigned int), `PROPERTY_BOPOMOFO_SCHEMA` (unsigned int), `PROPERTY_SPECIAL_PHRASE` (bool), and `PROPERTY_MODE_SIMP` (bool). `setProperty` is split across the class hierarchy: `PhoneticContext::setProperty` handles only `CONVERSION_OPTION`, `SPECIAL_PHRASE`, and `MODE_SIMP`, and returns `false` for the two schema properties, which are honored only in the `DoublePinyinContext` / `BopomofoContext` subclasses. There is no capability negotiation, no field-purpose hints, and no behavioral-policy exchange. |

---

## What the library provides

pyzy covers the following parts of a working Pinyin/Bopomofo IME engine
directly:

**Phonetic parsing.** `insert(char ch)` for full pinyin accepts only
lowercase letters `[a-z]` and the apostrophe `'` (`FullPinyinContext.cc:44`);
digits, uppercase, and punctuation are rejected (the wrapper routes those
itself). It internally parses the accumulated string into a `PinyinArray` (or
Bopomofo symbol sequence for `BOPOMOFO` mode). The cursor within the raw input
string is tracked by `cursor()`, which is a **byte offset** (`size_t
m_cursor`, `PhoneticContext.h:147`) into the raw ASCII input — distinct from
any UTF-8 code-point position in the output.

**Phrase lookup and ranking.** The internal `PhraseEditor` queries a
bundled SQLite phrase database and a user-phrase history database,
returning ranked candidates. The user-phrase history is updated
automatically when the user selects candidates, improving future
suggestions.

**Lazy candidate enumeration.** Candidates are prepared on demand through
`hasCandidate()` and `getCandidate()`. The caller controls how many are
fetched. `getPreparedCandidatesSize()` lets the caller avoid re-fetching
already-materialized candidates.

**Candidate type tagging.** Each `Candidate` carries a `CandidateType`
(`NORMAL_PHRASE`, `USER_PHRASE`, `SPECIAL_PHRASE`) so the wrapper can
distinguish provenance.

**Three-segment preedit decomposition.** `selectedText()` is the portion
the user has confirmed by selecting candidates, `conversionText()` is the
candidate currently being shown for the next phonetic segment, and
`restText()` is the unconverted tail. This decomposition conveys the
display-position concept from `CONCEPTS.md` in structural form. Note there are
effectively **two** boundaries (`selectedText | conversionText | restText`):
the CONCEPTS.md "internal display position" is the end of `selectedText` (that
prefix is stable on commit), but `conversionText` beyond it is itself
**provisional** — it tracks `focusedCandidate()` and changes as the user
focus-navigates the candidate list. The single-position concept does not
capture that focused-candidate substructure.

**Auxiliary text is where the preedit comes from.** This is the one place the
adapter departs most visibly from what the names suggest, so it is worth
stating plainly: `model->active` is `auxiliaryText()`, and `conversionText()`
is read nowhere.

`PinyinContext::updateAuxiliaryText` (`PinyinContext.cc:160-208`) renders from
`m_phrase_editor.cursor()` — the input **not yet consumed by a selection** —
through `m_pinyin`, then appends `textAfterPinyin()`. For Bopomofo that is the
zhuyin being composed; for Pinyin, the parsed syllables separated by spaces
with any non-pinyin tail after them; and in both cases a `|` marker at the
cursor. So it is exactly "the input the user has typed and not yet settled, in
the script they are composing in", which is what `docs/CONCEPTS.md` requires a
preedit to be. `conversionText()` is a conversion the user has not asked for,
which that same rule excludes; it is candidate 0 instead.

Two adjustments the adapter makes:

- The `|` is stripped. It marks `m_cursor`, and this library never sends a
  cursor movement, so it is always trailing and distinguishes nothing.
- When `hasCandidate(0)` is false, `updateAuxiliaryText` returns early with an
  **empty** string (`PinyinContext.cc:163-167`) and the typed input is in
  `restText()` instead. Bopomofo reaches this on the first key of most
  syllables. The adapter falls back to `restText()` there, which is also why it
  never needs a separate tail.

**Conversion options.** The bitmask `PROPERTY_CONVERSION_OPTION` (flags
from `Const.h`) controls incomplete-pinyin matching, typo-correction rules
(**8** `PINYIN_CORRECT_*` bits; `PINYIN_CORRECT_ALL` is an aggregate mask, not
a rule), and fuzzy-pinyin pairs (**14** consonant-pair bits plus **6** vowel-pair
bits, `Const.h:49-73`). These can be changed at any time via `setProperty()`.

**Special phrases.** A phrase table (`phrases.txt`) supports date/time macros
and other special expansions. Enabled by `PROPERTY_SPECIAL_PHRASE`. Their
candidates occupy the front of the flat candidate list (indices
`[0, special_phrases.size())`). A client's own copy under
`<data_dir>/pyzy/config/` wins over the one libpathime ships in its resource
directory, so overriding the table means dropping a file there.

**Simplified/Traditional selection.** `PROPERTY_MODE_SIMP` (bool)
switches the output character repertoire between simplified and traditional
Chinese.

**Cursor navigation and editing within the input buffer.** The library
provides `moveCursorLeft()`, `moveCursorRight()`, `moveCursorLeftByWord()`,
`moveCursorRightByWord()`, `moveCursorToBegin()`, `moveCursorToEnd()`,
`removeCharBefore()`, `removeCharAfter()`, `removeWordBefore()`,
`removeWordAfter()`. These all return `bool` indicating whether the
operation changed state.

**Candidate focus without selection.** `focusCandidate(size_t)`,
`focusCandidatePrevious()`, `focusCandidateNext()` — these move a
"focus" cursor within the candidate list and update the preedit segments to
show the currently focused candidate's conversion text, without committing.
`focusedCandidate()` returns the current focus index.

**User-phrase history management.** `resetCandidate(size_t index)` removes
a specific candidate from the user-phrase history.

**Unselect.** `unselectCandidates()` undoes any candidate selection,
returning to the unconverted state.

**Initialization and teardown.** `InputContext::init()` (or
`init(user_cache_dir, user_config_dir)`) must be called once before
creating any context. `InputContext::finalize()` must be called before
program exit. These are **process-global** operations (`InputContext.cc:34-65`)
that initialize the shared SQLite `Database` and `SpecialPhraseTable` — they are
not per-context, so a library must tie them to library load/unload separately
from per-context lifetime.

**Mode is fixed at construction.** The `InputType` (`FULL_PINYIN` /
`DOUBLE_PINYIN` / `BOPOMOFO`) is chosen at `create()` and cannot be changed on a
live context (`InputContext.cc:67-81`); switching phonetic type means destroying
and recreating the context.

**Encoding.** All text is returned as `const std::string&` (UTF-8) **by
reference** into internal buffers (`PhoneticContext.h:74-97`); the internal
`String` type derives from `std::string` (`String.h:33`). Returned references
are valid only until the next mutation.

---

## What the IBus wrapper adds

The IBus wrapper in `refs/ibus-pinyin/src/` provides everything that is
required to connect pyzy to an IBus engine but that pyzy does not supply.

### Key-event routing and the mode-switching dispatcher

`PinyinEngine` in `PYPinyinEngine.h/.cc` is the top-level IBus engine
class. It receives `processKeyEvent(guint keyval, guint keycode, guint
modifiers)` from IBus and routes each event to one of five mode-specific
editors held in `m_editors[MODE_LAST]`:

- `MODE_INIT` — normal Pinyin or Bopomofo input, handled by
  `FullPinyinEditor`, `DoublePinyinEditor`, or `BopomofoEditor`.
- `MODE_PUNCT` — Chinese punctuation picker, handled by `PunctEditor`.
- `MODE_RAW` — raw Latin pass-through, handled by `RawEditor`.
- `MODE_ENGLISH` — English word input (entered by pressing `v`), handled
  by `EnglishEditor`.
- `MODE_EXTENSION` — Lua-scripted extension (entered by pressing `i`),
  handled by `ExtEditor`.

The dispatcher in `PinyinEngine::processKeyEvent()` inspects the modifier
mask, current mode, and key value to decide which editor handles the event
and whether to switch modes. It also handles the Shift-release toggle
(switching Chinese/English input mode) and `Ctrl+Shift+F` (toggling
simplified/traditional).

pyzy has no concept of these modes. The entire mode dispatcher is wrapper
logic.

### Raw key-event decomposition

`PinyinEditor::processKeyEvent()` in `PYPinyinEditor.cc` breaks the
incoming keyval into categories: Pinyin letters (`a`–`z`), digits
(`0`–`9`, `KP_0`–`KP_9`), punctuation, Space, and function keys. For each
category it calls the appropriate method on `m_context` (the
`PyZy::InputContext*`) or manages the lookup table directly.

pyzy's `insert(char ch)` only accepts a single ASCII character; it never
sees IBus key codes, modifiers, or special keys. The entire key-to-action
mapping is wrapper logic.

### The Editor / PhoneticEditor class hierarchy

`Editor` (`PYEditor.h`) is the base class for all mode editors. It holds a
set of C++11 `signal<>` objects (defined in `PYSignal.h`) that replace
direct IBus calls. Signals are fired instead of calling IBus API directly,
and `PinyinEngine::connectEditorSignals()` wires them to the corresponding
`Engine` methods (`updatePreeditText`, `commitText`, etc.).

`PhoneticEditor` (`PYPhoneticEditor.h/.cc`) inherits `Editor` and owns the
`PyZy::InputContext*` (`m_context`) and `PinyinObserver` (`m_observer`).
It bridges pyzy Observer callbacks to IBus update calls and also owns the
IBus `LookupTable` (`m_lookup_table`).

`PinyinEditor` adds Pinyin-specific key processing (number keys for
candidate selection in page, comma/period and minus/equal for paging,
apostrophe disambiguation).

`BopomofoEditor` adds Bopomofo-specific key processing: a guide-key mode
(`m_select_mode`), nine configurable select-key layouts
(`bopomofo_select_keys`), auxiliary KP and F-key selection, and candidate
label coloring that dims labels when not in select mode.

### The PinyinObserver bridge

`PinyinObserver` (`PYPinyinObserver.h/.cc`) subclasses
`PyZy::InputContext::Observer` and forwards each callback to the
corresponding `PhoneticEditor` update method:

| pyzy callback | PhoneticEditor method called |
|---|---|
| `commitText` | `commitCallback(String)` |
| `inputTextChanged` | `updateInputText()` |
| `cursorChanged` | `updateCursor()` |
| `preeditTextChanged` | `updatePreeditText()` |
| `auxiliaryTextChanged` | `updateAuxiliaryText()` |
| `candidatesChanged` | `updateLookupTable()` |

### Preedit assembly with IBus attributes

`PhoneticEditor::updatePreeditText()` assembles the full preedit string by
concatenating `selectedText() + conversionText() + restText()`. It then
attaches IBus text attributes:

- A single-underline attribute across the whole string.
- A foreground color (`0x00000000`, black) and background highlight
  (`0x00c8c8f0`, pale blue) on the `conversionText` segment to mark the
  active conversion region.

The IBus preedit cursor is set to `selectedText().utf8Length()`, placing it
at the start of the conversion segment.

None of this attribute logic exists in pyzy. pyzy returns plain
`std::string` values.

### Lookup-table management and pagination

`PhoneticEditor::fillLookupTableByPage()` fetches one page of candidates
at a time from pyzy by calling `m_context->getCandidate(i, candidate)` in
a loop. It also applies per-candidate foreground colors based on
`CandidateType`: blue (`0x000000ef`) for `USER_PHRASE`, green
(`0x0000ef00`) for `SPECIAL_PHRASE`.

`pageUp()` and `pageDown()` operate on the wrapper's `m_lookup_table`
(IBus `LookupTable`) via `m_lookup_table.pageUp()`/`pageDown()` without
calling back into pyzy. `cursorUp()` and `cursorDown()` move the IBus
cursor within the already-fetched page. Only when paging down past the last
fetched candidate does the wrapper call `fillLookupTableByPage()` again to
materialize more candidates from pyzy.

`selectCandidateInPage(guint i)` converts a page-relative index to an
absolute index using `(cursor_pos / page_size) * page_size + i` and then
calls `m_context->selectCandidate(absolute_index)`.

The `m_dont_update_preedit` flag in `PhoneticEditor` suppresses preedit
updates during `reset()` when there is no conversion in progress, avoiding
a flash of stale preedit text.

### The PinyinProperties mode flags

`PinyinProperties` (`PYPinyinProperties.h/.cc`) maintains four runtime
mode flags not present in pyzy:

- `m_mode_chinese` — whether Chinese input is active (as opposed to
  English pass-through). Toggled by Shift-release.
- `m_mode_full` — full-width versus half-width alphanumeric output.
- `m_mode_full_punct` — full-width versus half-width punctuation.
- `m_mode_simp` — simplified versus traditional Chinese. This one is
  mirrored into pyzy via `setProperty(PROPERTY_MODE_SIMP, ...)` when it
  changes, and applied to a new context when `setContext()` is called.

These flags are also exposed as IBus `Property` objects (language bar
buttons) via `PropList m_props`, with SVG icons. pyzy has no concept of
mode flags, language-bar properties, or icons.

### Full/half-width output conversion

`HalfFullConverter` (`PYHalfFullConverter.h/.cc`) converts ASCII printable
characters to their full-width Unicode equivalents (U+FF01–U+FF5E range).
`PhoneticEditor::commitCallback()` applies this conversion to every
character of the commit text when `m_mode_full` is active.
`FallbackEditor::processKeyEvent()` similarly converts directly typed
characters. pyzy commits `std::string` values without any width conversion.

### Chinese punctuation substitution

`FallbackEditor` (`PYFallbackEditor.h/.cc`) handles all key events when
the active editor has no buffered input, and also handles direct character
input in English mode. It performs Chinese punctuation substitution: `!`
→ `！`, `.` → `。`, `,` → `，`, paired `'` → `''`/`''`, paired `"` →
`""`/`""`, etc., with separate tables for simplified and traditional
Chinese. This logic depends on `m_props.modeFullPunct()` and
`m_props.modeSimp()`, and tracks `m_prev_committed_char` to handle the
special case of `.` after a digit. pyzy provides none of this punctuation
logic.

### Config propagation to pyzy contexts

`Config` (`PYConfig.h/.cc`) reads IBus GSettings keys (page size,
double-pinyin schema, fuzzy-pinyin flags, Bopomofo layout, etc.) and
propagates changes to all live `InputContext*` instances it tracks in
`m_contexts`. `addContext()` / `removeContext()` register and unregister
contexts with the `Config`. `updateContext(PropertyName, Variant)` iterates
over `m_contexts` and calls `setProperty()` on each one. This creates an
"all contexts share config changes" model. pyzy itself has no config
object; every property must be pushed in by the caller.

### IBus lifecycle hooks with no pyzy equivalent

- `Engine::focusIn()` — re-registers IBus properties and may swap the
  active editor. No pyzy call is made.
- `Engine::focusOut()` — calls `reset()` on all editors, which calls
  `m_context->reset()`. pyzy reset semantics are the same, but the
  focus-out trigger is entirely wrapper logic.
- `Engine::enable()` — calls `m_props.reset()` to restore default mode
  flags. No pyzy call.
- `Engine::disable()` — empty in the pinyin engine. No pyzy call.
- `Engine::propertyActivate()` — handles language-bar button clicks by
  toggling mode flags via `PinyinProperties`. Only `PROPERTY_MODE_SIMP`
  changes are forwarded into pyzy.
- `Engine::candidateClicked()` — routes mouse clicks on the candidate
  window to `selectCandidateInPage()`.
- `Engine::pageUp()`, `pageDown()`, `cursorUp()`, `cursorDown()` —
  IBus hooks for scroll-wheel and keyboard paging; these manipulate the
  wrapper's `LookupTable` and call `fillLookupTableByPage()` if needed.
  pyzy has no concept of pages.

---

## Impedance mismatches

### 1. Key events versus character insertion

**Project concept:** The engine receives a key event (logical key, physical
key, modifier state) and returns whether it was handled.

**pyzy provides:** `insert(char ch)` takes a single printable ASCII
character and returns `bool`. There is no key-event type. The caller must
decode each key event into the appropriate mutation call (insert, move
cursor, delete, commit, reset, etc.) before touching pyzy.

**Bridge required:** A complete key-to-action dispatcher must be written
by the integrator. This is exactly what `PinyinEditor::processKeyEvent()`
and `PhoneticEditor::processFunctionKey()` implement. The dispatcher is not
trivial: it must handle modifiers, key release events, mode selection,
cursor movement, page navigation, and candidate selection keys, none of
which pyzy is aware of.

### 2. Composition data as separate callbacks versus one composite value

**Project concept:** Composition data is one value containing preedit text and
a candidate list. It is emitted atomically after each operation.

**pyzy provides:** Six independent callbacks (`inputTextChanged`,
`cursorChanged`, `preeditTextChanged`, `auxiliaryTextChanged`,
`candidatesChanged`, `commitText`) that fire separately and in sequence
during a single `insert()` or other operation. The preedit text itself is
split across three separate `std::string` accessors
(`selectedText()`, `conversionText()`, `restText()`).

**Bridge required:** The integrator must either accumulate all callbacks
that arrive during one operation and emit a single composite update
afterward, or accept that composition data is delivered in parts. The
ibus-pinyin wrapper does not aggregate; each callback triggers a separate
IBus update call immediately. For a library that promises one atomic
composition-data value per operation, a buffering layer is needed.

### 3. Candidate list: lazy/paged versus complete and flat

**Project concept:** The candidate list is the complete ordered list of
alternatives. The client paginates without engine involvement. The engine
provides absolute positions. The client must not select from an obsolete
list.

**pyzy provides:** A lazy, on-demand list. The total size is not known in
advance. `hasCandidate(i)` and `getCandidate(i, out)` materialize entries
one at a time. `getPreparedCandidatesSize()` returns how many have been
fetched so far. `candidatesChanged` fires when the list is regenerated (but
does not indicate the new total size).

**Bridge required:** To expose a complete flat candidate list as
CONCEPTS.md requires, the integrator must either pre-fetch all candidates
eagerly (potentially expensive for large databases) or accept that the
"complete list" concept is approximated by fetching on demand. The
ibus-pinyin wrapper approximates by fetching one page at a time and
fetching more pages as the user pages down.

### 4. Candidate selection is index-into-full-list but pyzy's model is session-accumulated

**Project concept:** `selectCandidate` takes an absolute position in the
most-recently-supplied candidate list.

**pyzy provides:** `selectCandidate(size_t index)` takes an absolute
0-origin index into the current candidate list and advances the selected
portion of the preedit. If the remaining input is exhausted, `commitText`
fires automatically. If not, the selected text grows and the candidate list
is regenerated for the next phonetic segment. The index is absolute within
the current candidate query, not across multiple selection steps.

**Bridge required:** No bridging is needed for the index itself, but the
integrator must understand that each `selectCandidate` call advances the
conversion state and the next `candidatesChanged` callback brings a new
list for the next phonetic segment — the same index value may refer to a
different candidate after the first selection.

### 5. Surrounding text: not present

**Project concept:** The client can supply surrounding text for
context-sensitive conversion, reconversion, or deletion. The engine may
request `deleteSurroundingText`.

**pyzy provides:** No surrounding-text API. There is no method to supply
context text, no API to request deletion of client text, and no
reconversion facility.

**Bridge required:** Any feature depending on surrounding text must be
implemented entirely outside pyzy, and one is. The punctuation layer
(`src/punctuation.*`) is ours rather than pyzy's, and both of its rules are
about the character before the insertion position — a full stop after a digit
is a decimal point, a quotation mark alternates with the one before it. The
adapter corrects them from the client's snapshot before each emitted key
(`observe_document()`), falling back to what it emitted itself when no snapshot
covers the position. pyzy is not involved either way; it never sees the
characters this layer handles.

### 6. Focus and activation lifecycle: not present

**Project concept:** Neither exists. This section records what pyzy lacks and
what the reference wrapper builds on top, because a reader coming from
ibus-pinyin will look for both.

**pyzy provides:** No focus or activation callbacks on `InputContext`. The
only lifecycle events pyzy knows about are `reset()` (clear state) and
destruction.

**Bridge required:** None for this library, which asks pyzy for nothing at a
lifecycle boundary the client does not ask for itself. For comparison, the
ibus-pinyin wrapper calls `reset()` on all editors in `focusOut()`
(`PYPinyinEngine.cc:198`) and re-registers language-bar properties in
`focusIn()` — the latter being panel bookkeeping this model has no equivalent
of, since the client presents everything.

### 7. Negotiation: not present except for conversion options

**Project concept:** Capabilities, field information, behavioral policies,
and engine options are exchanged through negotiation.

**pyzy provides:** Five properties settable via `setProperty()`:
`PROPERTY_CONVERSION_OPTION`, `PROPERTY_DOUBLE_PINYIN_SCHEMA`,
`PROPERTY_BOPOMOFO_SCHEMA`, `PROPERTY_SPECIAL_PHRASE`, and
`PROPERTY_MODE_SIMP` (the two schema properties are honored only in the
`DoublePinyinContext` / `BopomofoContext` subclasses). There is no capability
query mechanism, no field-type hint, no behavioral-policy exchange, and no
protocol versioning.

**Bridge required:** Engine options that map to pyzy properties can be
delivered via `setProperty()`. All other negotiation concepts (client
capabilities and input purpose/hints) must be managed entirely by the
integrator above pyzy.

### 8. Commit text: auto-fired versus caller-controlled

**Project concept:** Commit text is an explicit engine output; after
committing, the engine supplies whatever composition data should remain.

**pyzy provides:** `commitText` fires automatically from within
`selectCandidate()` when the last phonetic segment is covered, or can be
requested explicitly via `commit(CommitType)`. The `CommitType` controls
whether the raw input, phonetic symbols, or converted text is emitted.
After an automatic commit from `selectCandidate`, pyzy resets its context
internally; after a caller-initiated `commit()`, pyzy also resets.

**Bridge required:** The integrator must handle both the auto-fire path
(callback arrives during a `selectCandidate()` call) and the explicit path
(caller calls `commit()`). In both cases the callback arrives synchronously
within the mutation call, before that call returns. The integrator must not
assume commit text arrives only in a separate event loop turn.

### 9. Forward key event: not present

**Project concept:** The engine may request the client to process a key
event as an explicit output.

**pyzy provides:** No mechanism to forward a key event. pyzy only emits
text (via `commitText`) and state changes (via the other five callbacks).

**Bridge required:** If the host IME framework (such as IBus) requires
forward-key-event capability (for example to pass through unhandled keys
after a commit), the integrator must implement this entirely outside pyzy
using the host framework's own API.

### 10. Plain-text rule: pyzy output is plain but preedit is structurally split

**Project concept:** All composition data is plain Unicode text. The
candidate list contains only text entries, with no labels, comments, or
type annotations.

**pyzy provides:** `commitText` and `auxiliaryText()` are plain
`std::string`. `selectedText()`, `conversionText()`, and `restText()` are
plain `std::string` segments. However, each `Candidate` carries a
`CandidateType` field (`NORMAL_PHRASE`, `USER_PHRASE`, `SPECIAL_PHRASE`)
that is metadata beyond plain text.

**Bridge required:** The integrator can ignore `CandidateType` and expose
only `Candidate::text` to satisfy the plain-text rule. The ibus-pinyin
wrapper uses the type to apply colored text attributes in the IBus lookup
table; a plain-text integrator would drop this. The three-part preedit
decomposition must be re-joined into a single string, with the boundary
between `selectedText` and `conversionText` used to derive the preedit
display position concept from CONCEPTS.md.
