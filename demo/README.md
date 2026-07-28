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
| composition | The same thing as data: the preedit, how many of its scalars the engine considers settled, the auxiliary text, and the candidate list |
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
- **Page past the end of a candidate list with `PgDn`.** The program raises
  `max-candidates` and asks for more — the option is composition-safe precisely
  so a client can do that while a list is on screen. The panel shows the new
  value marked *set here*.
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

Ordinary selection with the mouse works too, which it does not in most
cpp-terminal programs: `Option::Raw` turns mouse reporting on with no way to
decline, and a terminal forwarding drags to the application is not selecting
text with them, so `main.cc` turns it back off. Nothing here wants mouse events.
On a legacy Windows console that does not help — mouse input is a console mode
flag set directly, and QuickEdit is off besides — which is what `Ctrl+Y` is
really for.

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

## Running out of a build tree

Neither anthy nor pyzy can find its data in an uninstalled build: anthy takes
`DIC_FILE` from the conf file named by `CONFFILE`, and pyzy's `Database::open()`
falls back to `main.db` relative to the working directory. `demo/CMakeLists.txt`
generates the one and stages the other, and `main.cc` puts them within reach —
by setting `CONFFILE` if nothing else has, and by changing into the staged
directory — before `pathime_init()`.

Both are compiled in only when the build supplied the paths, so an installed
copy of this program does neither and each backend finds its own data. This is
the same problem `tests/api/CMakeLists.txt` documents at length, solved the same
way and for the same reason: the library deliberately does not hunt for data
files itself.

Two consequences to know about. The program changes its working directory at
startup, which is harmless because nothing else in it touches the filesystem.
And on Windows the `CONFFILE` write reaches anthy only because the demo and
`pathime.dll` share a C runtime; with `/MD` on both, which is what this build
produces, they do.

## Layout

| File | What it owns |
|---|---|
| `src/main.cc` | Arguments, runtime data, `pathime_init()`, the event loop |
| `src/app.cc` | The client: engines, contexts, the document, the callbacks, what an unhandled key means |
| `src/keymap.cc` | Terminal key press → `pathime_key_event_t` |
| `src/options_view.cc` | Walking the option inventory, formatting a value, changing one |
| `src/render.cc` | Drawing. Calls no libpathime function of its own |
| `src/text.cc` | UTF-8 and terminal-width helpers |

`cpp-terminal` is a submodule under this directory, built with its examples,
tests, docs and install rules turned off, and with its warnings silenced — it
opts itself into `-Wall -Wextra -Wpedantic` (`/Wall` under MSVC), and a warning
in a tree we never edit is noise rather than information. `demo/CMakeLists.txt`
says how, and what one flag survives it under MSVC. Nothing above `demo/` knows
any of this exists.
