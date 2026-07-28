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
| composition | The same thing as data: the preedit, how many of its scalars the engine considers settled, the auxiliary text, and the candidate list with the hovered entry highlighted |
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
- **Switch engines mid-composition with `Ctrl+E` and come back.** Losing focus
  neither commits nor discards, so the composition is exactly where you left it.
- **Set `hangul-preedit` to `none` in the options panel, then type `gks`.**
  This is the one mode where the document *is* the composition: each jamo is
  committed, then deleted and recommitted one component fuller. One
  `process_key` in the log, a `delete_surrounding_text` and a `commit_text` back
  out under it, per keystroke.
- **Turn `punctuation-width` to `half` and type `,`** under any Chinese or
  Japanese engine.

## Getting the text back out

`Ctrl+Y` copies the document to the system clipboard with OSC 52, which xterm,
kitty, wezterm, iTerm2, Windows Terminal and tmux (`set-clipboard on`) honour
and others silently ignore.

## What it does not do

- **The caret is always at the end of the document.** Arrow keys go to the
  engine rather than moving it. A demo text field, not an editor.
- **String options are shown, not edited.** The only one is
  `PATHIME_OPT_TABLE_FILE`, and the table engine cannot be built yet.
- **Flags options toggle all-or-nothing.** `pinyin-fuzzy` has twenty bits and
  the library gives no names for them — naming values is the client's job, and
  naming twenty of them would teach less than turning the feature off and on.
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

