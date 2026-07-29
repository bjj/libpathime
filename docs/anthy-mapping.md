# anthy-unicode API and Concept Mapping

This document maps the anthy-unicode C library and its ibus-anthy Python wrapper to the canonical
IME concepts defined in `docs/CONCEPTS.md`. It is intended to guide the design of a libpathime
binding for Japanese kana-kanji conversion.

---

## Library overview

**anthy-unicode** is a C library that converts hiragana strings into kanji-kana mixed Japanese text.
Its conversion model has three distinct stages:

1. **Romaji/kana input** — the caller maintains a buffer of hiragana text, assembled from raw
   keystrokes by romaji-to-kana or kana-keyboard mapping logic that lives entirely outside the
   library. anthy-unicode does not accept raw keystrokes.

2. **Segmentation and conversion** — the caller passes a hiragana string to
   `anthy_set_string(context, hiragana)`. The library segments it into morphological units
   (文節, *bunsetsu*) and converts each segment to one of several candidate readings. The result
   is an array of segments, each with an ordered list of conversion candidates.

3. **Commit** — the caller selects candidates segment-by-segment with
   `anthy_commit_segment(context, seg_idx, cand_idx)`, reinforcing user preferences in the
   library's personal dictionary for future conversions.

The library also provides a prediction API (`anthy_set_prediction_string` /
`anthy_get_prediction`) that returns completion suggestions for a partial hiragana string,
independent of the segmentation/conversion pipeline.

All text exchanged with the library is UTF-8 when the context is configured with
`anthy_context_set_encoding(ctx, ANTHY_UTF8_ENCODING)`.

---

## API concept mapping

| CONCEPTS.md concept | anthy-unicode equivalent | Notes |
|---|---|---|
| **Input context** | `anthy_context_t` (opaque struct pointer) | Created by `anthy_create_context()`, freed by `anthy_release_context()`. One per independently active composition. |
| **Reset** (discard composition) | `anthy_reset_context(ctx)` | Clears the conversion state inside the context. Does not destroy the context. |
| **Key event** | No equivalent | anthy-unicode has no key-event API. Keystrokes are handled by the caller; only the resulting hiragana string is handed to the library. |
| **Handled / Unhandled** | No equivalent | Follows directly from the absence of a key-event API. The caller decides which keys to consume. |
| **Forward key event** | No equivalent | Not applicable; anthy-unicode is a pure conversion library. |
| **Preedit text** | Derived by the caller from `anthy_get_segment(ctx, seg_idx, NTH_UNCONVERTED_CANDIDATE)` for each segment once `anthy_set_string` has been called, or from the pre-conversion hiragana buffer maintained by the wrapper | See "Impedance mismatches" below. |
| **Candidate list** | `anthy_get_segment(ctx, seg_idx, cand_idx)` iterated over `anthy_segment_stat.nr_candidate` | Per-segment, not a flat global list. Special index constants `NTH_UNCONVERTED_CANDIDATE` (-1), `NTH_KATAKANA_CANDIDATE` (-2), `NTH_HIRAGANA_CANDIDATE` (-3), `NTH_HALFKANA_CANDIDATE` (-4) access pseudo-candidates. |
| **Select candidate** | `anthy_commit_segment(ctx, seg_idx, cand_idx)` for finalising, plus the caller tracking which candidate is currently "shown" per segment | Library commit is destructive (updates personal dict). Navigating candidates without committing is a caller responsibility. |
| **Commit text** | `anthy_commit_segment(ctx, seg_idx, cand_idx)` records the choice in the personal dictionary; the actual text insertion is done by the caller using the string returned by `anthy_get_segment` | The library does not push text to any output; it just records the selection. |
| **Auxiliary text** | No equivalent | Not a concept in this model either — `pathime_composition_t` has no such field. ibus-anthy synthesises `( 3 / 12 )` (candidate position / total), which this API publishes as `candidate_cursor` and `candidate_count`. See `docs/japanese-input-model.md` §6. |
| **Surrounding text** | No direct equivalent | `anthy_set_reconversion_mode(ctx, ANTHY_RECONVERT_*)` controls whether the library uses reconversion, but the caller must supply the surrounding text to `anthy_set_string` manually. |
| **Delete surrounding text** | No equivalent | The library never requests deletion of client text. |
| **Focus** | No equivalent | anthy-unicode has no lifecycle callbacks or awareness of focus. |
| **Activation** | No equivalent | Same as focus — purely a wrapper/framework concern. |
| **Negotiation** | `anthy_conf_override(key, value)` for global config; `anthy_set_personality(name)` for dictionary personality | Closer to static configuration than to a per-context negotiation protocol. `anthy_set_personality` and `anthy_conf_override` are the **public** entry points (`anthy/anthy.h`); `anthy_do_set_personality` / `anthy_init_personality` are **internal** (`src-main/main.h`) and must not be bound directly. |
| **Composition data** | Assembled by the caller from (a) the pre-conversion hiragana buffer and (b) `anthy_get_segment` calls for the converted text | No single library call returns both. |

---

## What the library provides

anthy-unicode directly covers the **kana-kanji conversion engine** and **personal-dictionary
management**:

- **Context lifecycle**: `anthy_create_context()` / `anthy_release_context()` / `anthy_reset_context()` map cleanly to the creation, destruction, and reset of a per-context conversion state.

- **Hiragana-to-kanji conversion**: `anthy_set_string(ctx, hiragana_utf8)` accepts a complete hiragana string and returns a **status code** (`0` on success, `-1` on error) — *not* a segment count (`src-main/main.c:202`). The segment count is read separately from `anthy_get_stat(ctx, &stat)` → `stat.nr_segment`. The caller iterates segments with `anthy_get_segment_stat` and retrieves each candidate with `anthy_get_segment(ctx, seg_idx, cand_idx)`. `anthy_set_string` calls `anthy_do_reset_context` as its first action (`main.c:212`), so it fully clears any prior conversion state before segmenting.

- **Candidate string retrieval / buffer sizing**: `anthy_get_segment(ctx, seg_idx, cand_idx, buf, buf_len)` uses a **two-call length protocol** — calling it with `buf = NULL, buf_len = 0` returns the required byte length without copying; a too-small buffer returns `-1` (`main.c:349-357`). A binding must measure then fetch (or grow a buffer); do not assume a fixed candidate size. `anthy_segment_stat.seg_len` is the segment's length in **input reading xchars (kana characters)**, not a byte length and not the candidate's length (`main.c:282`).

- **Segment boundary adjustment**: `anthy_resize_segment(ctx, seg_idx, +1|-1)` widens or narrows a segment boundary; re-querying the segment count and candidates afterward reflects the new segmentation.

- **Candidate commit with learning**: `anthy_commit_segment(ctx, seg_idx, cand_idx)` records the user's choice in the personal dictionary, improving future conversion accuracy.

- **Prediction**: `anthy_set_prediction_string(ctx, hiragana_prefix)` followed by `anthy_get_prediction_stat` and `anthy_get_prediction(ctx, n, buf, len)` provides word/phrase completion. `anthy_commit_prediction(ctx, n)` records a selected prediction.

- **Encoding negotiation**: `anthy_context_set_encoding(ctx, ANTHY_UTF8_ENCODING)` selects UTF-8 for all string exchange, avoiding legacy EUC-JP issues.

- **Reconversion mode**: `anthy_set_reconversion_mode(ctx, ANTHY_RECONVERT_AUTO|DISABLE|ALWAYS)` controls whether a context participates in reconversion (re-editing already committed text).

- **Dictionary personality**: `anthy_set_personality(name)` (public) switches the active user dictionary, enabling per-user or per-domain conversion profiles. (It is a thin wrapper over the internal `anthy_do_set_personality` / `anthy_init_personality` in `src-main/main.h`, which are not part of the public `anthy/anthy.h` API.)

What the library explicitly does **not** provide: keystroke processing, romaji-to-kana conversion,
mode state (hiragana/katakana/latin), preedit string assembly, candidate list paging, focus
tracking, surrounding-text retrieval, or any user-interface output.

---

## What the IBus wrapper adds

All source references below are to
`refs/ibus-anthy/engine/python3/` unless otherwise noted.

### Romaji/kana input layer (`jastring.py`, `romaji.py`, `kana.py`, `thumb.py`)

anthy-unicode receives only a completed hiragana string. The entire pre-conversion input pipeline
is implemented in the wrapper:

- `jastring.JaString` (`jastring.py`) maintains a list of `Segment` objects representing
  partially and fully resolved input characters. It supports three typing modes:
  `TYPING_MODE_ROMAJI`, `TYPING_MODE_KANA`, and `TYPING_MODE_THUMB_SHIFT`.

- `romaji.RomajiSegment` (`romaji.py`) accumulates ASCII keystrokes and resolves them to
  hiragana using `romaji_typing_rule_static` from `tables.py` or a user-configurable rule table.
  It handles multi-character sequences (`kya` → `きゃ`), double-consonant gemination (`tt` → `っt`),
  and `n`-disambiguation (`n'a` → `んa`). Shift-key detection for Latin-mode passthrough is also
  here.

- `kana.KanaSegment` (`kana.py`) implements JIS kana keyboard mapping, including the voiced
  consonant combining rules (dakuten/handakuten applied to preceding kana).

- `thumb.ThumbShiftSegment` (`thumb.py`) implements the Nicola thumb-shift keyboard layout,
  which requires timed key-release detection and a GLib timer (`GLib.timeout_add` in
  `engine.py:__process_key_event_thumb`).

- `JaString.get_hiragana(commit)` / `get_katakana()` / `get_half_width_katakana()` convert the
  accumulated segment list to the appropriate kana string for display or for passing to
  `anthy_set_string`.

### Input mode and conversion mode state (`engine.py`, class `Engine`)

The wrapper maintains two mode dimensions that anthy-unicode is unaware of:

- **Input mode** (`Engine.__input_mode`): one of `INPUT_MODE_HIRAGANA`, `INPUT_MODE_KATAKANA`,
  `INPUT_MODE_HALF_WIDTH_KATAKANA`, `INPUT_MODE_LATIN`, `INPUT_MODE_WIDE_LATIN`. This controls
  how keystrokes are interpreted before any conversion.

- **Conversion mode** (`Engine.__convert_mode`): one of `CONV_MODE_OFF`, `CONV_MODE_ANTHY`,
  `CONV_MODE_PREDICTION`, or direct-conversion modes (`CONV_MODE_HIRAGANA`,
  `CONV_MODE_KATAKANA`, `CONV_MODE_LATIN_0–3`, `CONV_MODE_WIDE_LATIN_0–3`). The direct modes
  convert the preedit to a fixed script without calling anthy-unicode at all.

### Key event dispatch (`engine.py:__process_key_event_internal2`, `__process_key_event_thumb`)

The IBus callback `process-key-event` delivers (keyval, keycode, state) to the wrapper, which:

1. Strips the RELEASE mask; key releases are silently consumed (return `False`).
2. Translates numpad keys via `KP_Table` if ten-key mode is on.
3. Dispatches to named command methods (`__cmd_*`) via a keybinding table (`__keybind`).
4. Falls through to `__on_key_common` for printable characters, which appends the character to
   `__preedit_ja_string` and optionally triggers immediate conversion
   (`SEGMENT_IMMEDIATE` mode).

Returns `True` (handled) or `False` (unhandled) as an IBus Boolean.

### Preedit text assembly (`engine.py:__update_input_chars`, `__update_anthy_convert_chars`)

The wrapper builds the IBus preedit string in two distinct states:

- **Pre-conversion** (`CONV_MODE_OFF`): preedit is `JaString.get_hiragana()` (or katakana /
  half-width katakana depending on input mode). The IBus attribute list adds a single underline
  across the whole string.

- **Post-conversion** (`CONV_MODE_ANTHY`): preedit is the concatenation of all segment texts
  from `self.__segments` (a Python list of `(cand_index, text)` tuples maintained by the
  wrapper). The active segment gets a background highlight; all other segments get an underline.
  The cursor position in the IBus preedit is set to the character position of the active segment.

### Auxiliary text (`engine.py:__update_anthy_convert_chars`)

During `CONV_MODE_ANTHY`, the wrapper synthesises an auxiliary string of the form
`( N / M )` where N is the 1-based current candidate index and M is the total number of
candidates for the active segment (from `IBus.LookupTable.get_cursor_pos()` and
`get_number_of_candidates()`). This string is passed to `self.update_auxiliary_text`. When the
lookup table is not visible, an empty auxiliary string is sent.

### Candidate list management (`engine.py:__fill_lookup_table`)

The wrapper maintains an `IBus.LookupTable` object (size set from user preferences, default 9).
`__fill_lookup_table` populates it by calling `self.__context.get_segment(seg_idx, i)` for
`i` in `0 .. nr_candidates-1`. For prediction mode it calls `self.__context.get_prediction(i)`.
Candidate navigation (`do_cursor_up`, `do_cursor_down`, `__page_up`, `__page_down`) moves the
IBus lookup-table cursor and simultaneously updates `self.__segments[cursor_pos]` with the
currently highlighted candidate text.

Candidate selection triggered by number keys (`__on_key_number`) maps page-relative index to
absolute lookup-table position using `get_cursor_pos() - get_cursor_in_page() + index`.

### Segment cursor (`engine.py:self.__cursor_pos`)

The wrapper maintains `self.__cursor_pos` (an integer index into `self.__segments`) to track
which segment the user is currently editing candidates for. Moving between segments with arrow
keys calls `__select_segment`, which updates `__cursor_pos` and refills the lookup table.
Segment resizing (`anthy_resize_segment`) is exposed via `__shrink_segment`.

### Focus, activation, and reset (`engine.py:do_focus_in`, `do_focus_out`, `do_disable`, `do_reset`)

These IBus lifecycle methods are all handled by the wrapper; anthy-unicode has no corresponding
callbacks:

- `do_focus_in`: re-registers IBus properties; may restore preedit depending on
  `behavior-on-focus-out` preference.
- `do_focus_out`: calls `self.__reset()` and `self.__invalidate()` (modes 0 and 1 of the
  focus-out preference) or does nothing (mode 2, "commit on focus-out" is handled by IBus
  itself via `update_preedit_text_with_mode(..., COMMIT)`).
- `do_disable`: always calls `self.__reset()` and `self.__invalidate()`.
- `do_reset`: calls `self.__reset()` unless `behavior-on-focus-out` is mode 2.

`self.__reset()` (`engine.py:Engine.__reset`) discards the `JaString`, clears `__segments`,
clears the lookup table, and resets `__convert_mode` to `CONV_MODE_OFF`. It does **not** call
`anthy_reset_context` directly. That is safe because `anthy_set_string` itself calls
`anthy_do_reset_context` as its first action (`main.c:212`), so the C context is always reset at
the start of the next conversion — there is never stale conversion state for it to read. Calling
`anthy_reset_context` on reset is still worthwhile, to release the segment list / dictionary
session promptly (see mismatch #5).

**Note on API naming:** the introspection wrapper uses convenience methods
(`get_nr_segments()`, `get_nr_candidates(seg)`, `get_nr_predictions()`) that are GObject-introspection
helpers over `anthy_get_stat` / `anthy_get_segment_stat` / `anthy_get_prediction_stat`. They are
**not** C entry points — do not look for `anthy_get_nr_segments` in the header.

### Reconversion (`engine.py:__cmd_reconvert`, `__update_reconvert`)

The wrapper implements reconversion by reading the X11 PRIMARY clipboard via `Gtk.Clipboard`,
feeding the clipboard text to `JaString.insert` character-by-character, and passing it to
`anthy_set_string`. This simulates re-entering previously committed text for re-conversion.
anthy-unicode's own `anthy_set_reconversion_mode` flag is configured in the GObject
introspection wrapper (`anthy.i`) but the clipboard-based approach in the wrapper is the
primary mechanism.

### Punctuation and symbol style (`jastring.py:JaString._chk_text`)

Before returning hiragana text, `JaString._chk_text` applies user-configured transformations:
period style (。/． etc.), symbol style (「/［ etc.), half-width symbols, and half-width
numbers. These rules are applied in the wrapper before the string is either displayed as preedit
or passed to anthy-unicode.

### IBus property panel (`engine.py:__init_props` and related methods)

The wrapper registers and maintains a complete IBus property list (language bar) covering:
input mode, typing method, segment mode, and dictionary mode. These are presented as IBus radio
menus and updated on each mode change. anthy-unicode has no UI concept equivalent.

---

## Impedance mismatches

### 1. Candidate list granularity: per-segment vs. flat

**What CONCEPTS.md expects**: a single flat ordered candidate list. The client selects a
candidate by its absolute position in that list.

**What anthy-unicode provides**: a separate candidate list per segment. After calling
`anthy_set_string`, the caller must choose which segment to show candidates for. The segment
index acts as an additional dimension. `anthy_get_segment_stat(ctx, seg_idx, &stat)` gives
`stat.nr_candidate` candidates for one segment; `anthy_get_segment(ctx, seg_idx, cand_idx)`
retrieves a specific candidate.

**What must be bridged**: the libpathime binding must decide how to expose this. Options are:
(a) present only the active segment's candidates as the flat candidate list, updating the list
when the active segment changes; (b) flatten all segments' candidates into one list with a
disambiguation scheme. Option (a) matches ibus-anthy's behaviour and is simpler. With option
(a), "select candidate" is translated to `anthy_commit_segment(ctx, active_seg_idx, position)`,
but only at commit time — during navigation the candidate is tracked locally without informing
the library.

anthy's real model has **three** independent state changes that each regenerate the flat
candidate list the concepts model exposes:

- **Segment focus movement** — an *active-segment index* (the wrapper's `__cursor_pos`) selects
  which segment's candidates are shown. Moving it (arrow keys → `__select_segment`) refills the
  list. CONCEPTS.md has no notion of segment focus, so "select candidate" is only unambiguous once
  the binding fixes it to mean "select within the currently focused segment."
- **Segment resize** — `anthy_resize_segment(ctx, seg_idx, ±1)` (`main.c:259`) re-segments from
  that boundary and **invalidates every segment's candidate list**.
- **Per-segment candidate selection** — the "currently shown but not committed" candidate index is
  **library-invisible**: anthy only records a choice at `anthy_commit_segment` time. The binding
  must own an array of per-segment chosen indices itself.

All three produce fresh composition data; any client-cached candidate list is invalid after each.

### 2. No key-event API

**What CONCEPTS.md expects**: the engine receives key events and reports handled/unhandled.

**What anthy-unicode provides**: no key-event entry point whatsoever. The library is a pure
string-conversion service.

**What must be bridged**: the entire key-dispatch, romaji-to-kana conversion, mode state machine,
and handled/unhandled logic must be implemented in the libpathime layer, mirroring what
`engine.py` does with `JaString`, `RomajiSegment`, and the `__keybind` dispatch table.

### 3. Preedit text is not available as a unified string until the caller constructs it

**What CONCEPTS.md expects**: the engine produces preedit text as a single plain-text string.

**What anthy-unicode provides**: before conversion, the library knows nothing; the hiragana
buffer is maintained by the caller (`JaString`). After `anthy_set_string`, the library provides
per-segment unconverted text via `anthy_get_segment(ctx, seg_idx, NTH_UNCONVERTED_CANDIDATE)`.

**What must be bridged**: the libpathime layer must concatenate per-segment texts (or the
pre-conversion `JaString` output) to form a single preedit string. CONCEPTS.md's preedit
"internal display position" maps to the boundary of the active segment: text of segments before
the active one is settled, the active segment and everything after is still mutable. The wrapper
computes this cursor offset by summing the character lengths of the unconverted-segment strings
(`len(get_segment(i, NTH_UNCONVERTED_CANDIDATE))`, `engine.py:2731-2732`), not `seg_len` and not
any library-provided cursor — so in the anthy world the display position is
**per-segment-boundary**, driven by the active-segment index, not a free character cursor.

### 4. Commit is destructive and triggers learning

**What CONCEPTS.md expects**: commit text is a request from the engine to the client to insert
finalized text. The engine may issue commit requests independently of any internal bookkeeping.

**What anthy-unicode provides**: `anthy_commit_segment(ctx, seg_idx, cand_idx)` does two
things simultaneously: it signals the user's choice to the personal-dictionary learning system
and indicates that the segment is finalised. There is no separate "record this choice" vs
"produce commit text" distinction.

**What must be bridged**: the libpathime layer must call `anthy_commit_segment` at the
moment the user confirms a segment, even though the actual text insertion into the client is a
separate action driven by the engine's output. The ordering — call `anthy_commit_segment`, then
emit commit text — must be correct. Skipping the commit call causes the personal dictionary to
miss a learning opportunity; calling it prematurely (e.g. during candidate navigation) would
corrupt learning.

### 5. Context reset does not call anthy_reset_context in ibus-anthy

**What CONCEPTS.md expects**: reset discards transient composition state and returns to a
neutral state.

**What anthy-unicode provides**: `anthy_reset_context(ctx)` resets the context's internal
conversion state. The function exists and is the correct tool.

**What must be bridged**: ibus-anthy's `self.__reset()` does *not* call `anthy_reset_context`;
it discards only the Python-side `JaString` and segment list. There is **no stale-state hazard**,
because `anthy_set_string` calls `anthy_do_reset_context` as its first action (`main.c:212`,
`context.c:322` — it frees the prior segment list, split context, and input string). The C side
is therefore always reset at the start of each conversion. A libpathime implementation should
still call `anthy_reset_context` on reset, and the reason it should is **prompt resource
release**: freeing the segment list and releasing the dictionary session at the moment the
composition is abandoned rather than whenever the next one begins.

### 6. No surrounding text input to the library

**What CONCEPTS.md expects**: the engine may use surrounding text for context-sensitive
conversion, reconversion, or punctuation decisions.

**What anthy-unicode provides**: `anthy_set_reconversion_mode` controls whether the library
attempts reconversion, but the library has no API to receive surrounding text from the caller.
For reconversion, ibus-anthy reads the X11 PRIMARY clipboard (an external, non-IBus mechanism)
and passes the retrieved text to `anthy_set_string` manually.

**What must be bridged**: a libpathime binding that wants to support reconversion using
surrounding text provided by the client (via CONCEPTS.md's surrounding-text mechanism) must
manually extract the relevant portion of the surrounding text and pass it to `anthy_set_string`.
There is no library-level hook for this.

### 7. Segment mode and immediate conversion are wrapper-only concepts

**What CONCEPTS.md expects**: the engine receives key events and produces composition data;
conversion policy is an engine concern.

**What anthy-unicode provides**: a single conversion call (`anthy_set_string`). Whether
conversion happens immediately on each keystroke (immediate mode) or only when the user presses
the conversion key is a policy the caller implements.

**What must be bridged**: the ibus-anthy wrapper exposes four segment modes (multi, single,
immediate-multi, immediate-single) controlled by `Engine.__segment_mode`. The `SEGMENT_IMMEDIATE`
flag causes `__begin_anthy_convert()` to be called inside `__on_key_common`, which re-issues
`anthy_set_string` on every printable keystroke. libpathime must replicate this policy choice
if it wants to support immediate conversion.

### 8. Auxiliary text — a concept neither side has

**What CONCEPTS.md expects**: nothing. This model has no auxiliary text at all.

**What anthy-unicode provides**: nothing either. The `( N / M )` candidate counter is generated
entirely by the ibus-anthy wrapper, in `__update_anthy_convert_chars`; the library has no
notion of it.

**What must be bridged**: nothing. The counter's two numbers are `candidate_cursor` and
`candidate_count` in `pathime_composition_t`, and formatting and placing them is the client's
job. The only thing to be careful of is not reintroducing a status string on the anthy side to
carry information the composition data already carries.

---

## Why the anthy family is one library on Windows

anthy's six libraries share global data across the library boundaries: the
splitter reads `anthy_wt_all` / `anthy_wt_none`, which `src-worddic` owns and
initialises at runtime, and `src-diclib` owns the conf database (`conf.c`) and
the mapped dictionary (`file_dic.c`) that every other piece reaches through.

ELF resolves all of that to one definition whatever the packaging. Windows
resolves none of it for free. A data symbol read across a DLL boundary needs
`__declspec(dllimport)` at the *reference* to go through `__imp_` indirection,
and a static archive linked into two DLLs gives each one a private copy of that
archive's state — which of those two happens depends on whether the linker
prefers an archive member or the import library that also offers it, a question
GNU ld and `link.exe` do not answer the same way.

Windows therefore compiles the whole family as a **single DLL**. Every
cross-piece reference lands inside one module, so the process holds exactly one
conf database, one mapped dictionary and one wtype pair however many things
link it. Linux keeps upstream's six-library layout, which its loader makes
correct for free. `LIBPATHIME_ANTHY_SINGLE_LIBRARY` selects either shape on
either platform, which is how the combined build is tested away from Windows.

Because the boundary is a real shared library on both platforms, anthy's
process-global state is genuinely process-global: two modules that both link
anthy share one copy of it, and `anthy_conf_override()` from either is visible
to both.

`pathime_init_params_t::data_dir` reaches anthy as
`anthy_conf_override("XDG_CONFIG_HOME", data_dir)` inside the library, and the
per-user state (`<data_dir>/anthy/last-record2_*`, the private dictionary, the
lock file) is created there on Windows as it is on Linux — including creating a
multi-level, backslash-separated `data_dir` from nothing. The directory is
created when the first *context* is created, not by `pathime_init()`.
