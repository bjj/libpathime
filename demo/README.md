# pathime-demo

An interactive IME in a terminal: type Korean, Japanese or Chinese into a text
field and watch the library work. It is a *client* in the sense
`docs/CONCEPTS.md` means — it owns a document, offers key presses to an engine,
and applies whatever the engine asks it to do — and it is written against
`<pathime/pathime.h>` and nothing else.

It verifies nothing. `tests/` does that. This is for seeing the model move.

```
cmake -S . -B build -DLIBPATHIME_BUILD_DEMO=ON
cmake --build build
./build/bin/pathime-demo
```

`--list` prints which engines this build can supply and exits, `--engine NAME`
starts on one of them, and `--data-dir DIR` decides where the engines keep what
they learn. `F1` inside the program lists the keys.

## What is on the screen

| Panel | What it shows |
|---|---|
| document | The client's own text, with the preedit drawn into it — settled text green, the still-changing tail yellow, exactly as `preedit_settled` divides them |
| composition | The same thing as data: the preedit, how many of its scalars the engine considers settled, and the candidate list with the hovered entry highlighted |
| event log | Everything that crossed the API boundary, in order |
| options | Every option the active engine implements, editable at either level |

The event log is the panel to watch, and it runs in both directions:

| | |
|---|---|
| `→` cyan | this program calling into libpathime, with what the call returned |
| `←` magenta | libpathime calling back into this program, indented under the call that caused it |
| `·` dim | this program talking to itself — not API traffic at all |

So a key press reads downward as one paragraph: the `process_key` that went in,
the deletions and commits that came back out, the `composition_changed` that
closed it, and the result. It is where the ordering rules the header fixes stop
being prose — every `delete_surrounding_text` arrives before any `commit_text`,
and `composition_changed` always arrives last — and where an option set is
visibly a call that dispatches callbacks of its own.

The log takes two thirds of the space left below the composition, or a third of
it while the options panel has the keyboard.

## Things worth trying

- **Type `gksrnr` under Hangul.** 한국, one jamo at a time.
- **Type `nihao` then Space under Pinyin.** Then do it again and press `Return`
  instead: the preedit reads 你好 but Return commits `nihao`, because the
  conversion was a preview the user never chose.
- **Type `nihao` and hold `Down`.** The highlight walks the candidate list and
  the preedit above rewrites itself to whatever is under it, because moving the
  cursor settles nothing — press `Up` and it all goes back. That is
  `pathime_context_set_candidate_cursor()`, and the arrows are bound here rather
  than in the engine: which key navigates, and whether the list wraps, are the
  client's to decide, so the demo decides them.
- **Page past the end of a candidate list with `PgDn`.** The program raises
  `max-candidates` and asks for more — the option is composition-safe precisely
  so a client can do that while a list is on screen. The panel shows the new
  value marked *set here*. Holding `Down` off the end of the page does the same
  thing, and the page follows the highlight so the digit keys always pick from
  what is on screen.
- **Walk `pinyin-fuzzy` in the options panel.** Left and Right step through the
  option's twenty bits by name — `c-ch`, `z-zh`, `an-ang` — and Space toggles
  the one under the cursor. Every one of those names comes from
  `pathime_option_value_name()`; this program carries no table of its own, which
  is the whole point of the inventory walk.
- **Switch engines mid-composition with `Ctrl+E` and come back.** A context
  that stops being keyed is not told so and does not care, so the composition
  is exactly where you left it.
- **Type half a syllable, then press `Ctrl+T` and `Ctrl+R` in turn.** They are
  the two ways a composition can end when the user walks away from it: `Ctrl+T`
  commits what is there — a `commit_text` comes back out under the call —
  and `Ctrl+R` throws it away, with only the `composition_changed` beneath it.
  Press either with nothing composing and the log shows the call and no
  callbacks at all, which is why a client can call them without checking first.
- **Type `1`, then `.` — then do it again after `Ctrl+O`.** Under a Chinese
  engine the `.` stays an ASCII period, because the engine can see it follows a
  digit and `1.5` is a number. `Ctrl+O` is what a client does when the user
  leaves the field — commit, then reset — and the reset is the half that drops
  what the engine remembered about the text before the caret.
  
  The period stays a period anyway, and that is the more interesting half: this
  program supplies surrounding text after every dispatch, so the engine reads
  the `1` out of the document rather than out of its own memory. Quotes behave
  the same way — `"` alternates by looking at the last one in the document, not
  by counting its own.
- **Press `Ctrl+U` to change how much surrounding text is supplied**, and try
  the two rules again at each setting. The heading above the document says
  which one is in force, and every `set_surrounding_text` call is in the log.
  - **whole document** — both rules read the document. This is what a real
    client should do.
  - **1 scalar** — the fragment case, and the one worth understanding. The
    header says the supplied text may be a fragment whose ends are *not*
    document boundaries, so an engine reads it as evidence and never as the
    whole truth. One scalar is all the digit rule ever needs, so `1.5` still
    works; it can never reach back to the last quotation mark, so quotes fall
    back to the engine's own memory. Under Hangul with `hangul-preedit = none`
    it is *also* enough to keep syllables assembling, because the only thing
    that mode asks is whether the one character it just wrote can still be
    deleted.
  - **none** — this program stops calling, and does not thereby retract what it
    already supplied: there is no call for that and no need of one. So the
    engine keeps a snapshot that goes stale, and the log fills with deletions
    declined because this program can see its own document has moved on. Type
    `gks` under Hangul with `hangul-preedit = none` to watch a syllable fail to
    assemble, which is the failure `PATHIME_REQUIRES_SURROUNDING_TEXT` warns
    about in as many words.
- **Set `hangul-preedit` to `none` in the options panel, then type `gks`.**
  This is the one mode where the document *is* the composition: each jamo is
  committed, then deleted and recommitted one component fuller. One
  `process_key` in the log, a `delete_surrounding_text` and a `commit_text` back
  out under it, per keystroke.
- **Turn `punctuation-width` to `half` and type `,`** under any Chinese or
  Japanese engine.
- **Switch to the table engine and step `table-file` in the options panel.**
  The values are the tables this build installed, and the library is what
  enumerates them — the demo knows only that a string option had legal values to
  step through. Pick `cangjie5` and type `ab` for 明, or `stroke5` and type `n`,
  whose input characters are punctuation rather than letters.

## Getting the text back out

`Ctrl+Y` copies the document to the system clipboard with OSC 52, which xterm,
kitty, wezterm, iTerm2, Windows Terminal and tmux (`set-clipboard on`) honour
and others silently ignore.

## What it does not do

- **The caret is always at the end of the document.** Arrow keys go to the
  engine rather than moving it. A demo text field, not an editor.
- **String options are stepped through, not typed into.** Of the three —
  `PATHIME_OPT_TABLE_FILE`, `PATHIME_OPT_TABLE_SINGLE_WILDCARD` and
  `PATHIME_OPT_TABLE_MULTI_WILDCARD` — only `PATHIME_OPT_TABLE_FILE` has legal
  values the library enumerates, the installed tables, so Left/Right picks one
  exactly as it picks an enum value. The other two enumerate nothing and stay
  unsupported here, because this panel has no text entry and a free-form string
  has no honest gesture.
- **Flags options are edited one bit at a time, not as a set.** Left and Right
  move between `pinyin-fuzzy`'s twenty bits and Space toggles the one under the
  cursor; there is no "all" or "none" gesture.
- **Chorded keys never reach an engine.** They are this program's shortcuts.
  Engines would decline them anyway, which is what `PATHIME_MOD_CONTROL` is for.

## The one piece of platform glue

`src/keymap.cc`. `pathime_key_event_t` wants an X11 keysym, the *physical* key
as a US-QWERTY keysym, and a modifier mask; a terminal delivers a decoded
character and nothing else. So `layout_key` is derived from the character on the
assumption that the keyboard is US QWERTY, and on any other physical layout the
two engines that read key position — Hangul, and Japanese under
`anthy-typing-method = kana` — will produce the wrong jamo or kana. A client on
a real windowing system reports the true key and has no such problem. CapsLock,
NumLock and Super are likewise not knowable here and are never reported.

## Layout

| File | What it owns |
|---|---|
| `src/main.cc` | Arguments, `pathime_init()`, the event loop |
| `src/app.cc` | The client: engines, contexts, the document, the callbacks, what an unhandled key means |
| `src/keymap.cc` | Terminal key press → `pathime_key_event_t` |
| `src/options_view.cc` | Walking the option inventory, formatting a value, changing one |
| `src/render.cc` | Drawing. Calls no libpathime function of its own |
| `src/text.cc` | UTF-8 and terminal-width helpers |

