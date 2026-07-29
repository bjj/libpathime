# The Japanese input model, as it actually behaves

Why this file exists: Japanese is the backend whose *user expectations* are
hardest to infer from the code. libhangul composes syllables and has no
candidates; pyzy converts and commits. Anthy sits behind a key model that looks
arbitrary until you see which command each key is bound to in which state, and
several of its behaviours that read as engine quirks turn out to be either
(a) universal Japanese typing habits we cannot change, or (b) ibus-anthy
decisions with no weight behind them at all. This file separates the two, and
records the measurements that settled each question.

Everything below marked **measured** was produced by driving the built anthy or
the public `pathime.h` and reading the result, not by reasoning about source.
Everything marked **source** carries a file:line. Everything marked **general
knowledge** is what commercial Japanese IMEs do and was *not* verified in this
tree; treat it as the weakest kind of claim here.

The reference is `refs/ibus-anthy` pinned at `bjj/ibus-anthy@0962741`.

---

## 1. Space is three commands, not one with exceptions

ibus-anthy binds Space to **three** commands at once
(`data/org.freedesktop.ibus.engine.anthy.gschema.xml.in:932-959`) — `insert_space`,
`convert`, and `select_next_candidate`. They never overlap, because each is
guarded by `_chk_mode` (`engine/python3/engine.py:1970-1990`), whose six states
are mutually exclusive in every reachable configuration:

| `_chk_mode` | state | Space does |
|---|---|---|
| `0` | nothing composing | insert a space — **full-width** by default |
| `1` | composing kana, not yet converted | `convert` |
| `2` | converting, candidate list hidden | `select_next_candidate` |
| `3` | predicting, candidate list hidden | `select_next_candidate` |
| `4` | an F-key convert mode (katakana, wide latin, …) | `convert` |
| `5` | candidate list visible, any mode | `select_next_candidate` |

The one overlap the code permits — mode `4` also answering to a query for `5`,
since the `'4'` branch returns before the `'5'` test — is unreachable, because
the F-key convert modes never open a lookup table.

**source:** `__cmd_convert` is guarded `_chk_mode('14')`
(`engine.py:2273-2274`), `__cmd_select_next_candidate` is guarded
`_chk_mode('235')` (`engine.py:2546-2547`), `__cmd_insert_half_space` is guarded
`_chk_mode('0')` (`engine.py:2233-2234`).

So "Space converts, and then Space advances the candidate cursor" is not a
special case bolted onto a convert key. They are two disjoint commands that
happen to share a binding, and the state decides which one exists. This is worth
stating because the alternative reading — one key with a mode-dependent
exception — makes the behaviour look like a local convenience of the binding
rather than the structure of ibus-anthy's command table, which is what it is.

**general knowledge:** Space-converts-then-Space-cycles is identical in MS-IME,
ATOK, Google Japanese Input and macOS. It is the single most ingrained habit a
Japanese typist has, and an engine that breaks it reads as broken. We match it.

## 2. Tab is `predict`, and after conversion it is a second Space

Same dual life. `'predict': ['Tab', 'ISO_Left_Tab']` at modes `1,4`
(`gschema.xml.in:938`, `__cmd_predict` at `engine.py:2282-2283`), and Tab also appears
in `select_next_candidate`'s binding list, which is live at modes `2,3,5`. So
**after conversion, Tab and Space do exactly the same thing**; they differ only
while composing, where Space converts and Tab predicts.

Tab-as-predict is an **ATOK** convention — the `atok` keymap in the same file
also carries `'predict': ['Tab']` — and is not an MS-IME behaviour. It is a
narrower expectation than Space-converts, and a client that never binds it loses
less than the symmetry suggests.

**Tab does not resolve a pending romaji `n`, and this is worth stating because
the code looks as though it does.** `__cmd_predict` calls
`get_hiragana(True)` (`engine.py:2286`), whose `commit=True` turns a trailing
`n` into ん — but `get_hiragana()` is a **pure read**. It builds its result from
`self.__segments` through `map(conv, ...)`, applies the substitution to the
returned string, and never assigns back; `_chk_text` is likewise a pure
transform. So the resolved form reaches `set_prediction_string()` as a lookup
key and nothing else. The displayed preedit comes from `__get_preedit()` with
`commit` defaulting to false and still reads にほn — and when there are no
predictions `__cmd_predict` returns False, so the key is unhandled and nothing
changed at all.

The key that really does re-render the preedit resolved, without committing and
without kanji conversion, is **F6** (`convert_to_hiragana` → `__on_key_conv(0)`,
`engine.py:1590-1623`, rendered by `__update_convert_chars` at
`engine.py:1226-1228`).

**F6 belongs to a family that is out of scope here.** ibus-anthy converts what
has already been typed to hiragana (F6), katakana (F7), half-width katakana
(F8), wide latin (F9) or latin (F10), plus `_all` variants; all of them run
through `__on_key_conv`, which sets `__convert_mode` so that
`__update_convert_chars` re-renders the preedit in the chosen script, and all of
them are guarded `_chk_mode('12345')` (`engine.py:2670-2672` for the hiragana
entry point), so they are live throughout composition. This is a real
composition operation and a different thing from `PATHIME_OPT_ANTHY_KANA_SCRIPT`,
which chooses what typing produces going forward; F7 converts what is already
there.

It is left out because the phone-keyboard target has no F-keys and because pyzy
has no equivalent, so it would be a concept carried for one and a half engines.
Two things soften that. Katakana is partly reachable anyway, since conversion
offers it as a candidate — にほんご gives `[2] ニホンゴ`, わたし gives ワタシ —
though the position varies with the record and some readings offer none at all.
And the operation is cheap to add if a consumer appears: it is one additive
call over the `display()` / `commit_text()` split `RomajiComposer` already has.
What is lost outright: **half-width katakana, wide latin and latin are reachable
by no route at all.**

So the routes from `にほn` to `にほん` are: type the second `n`, press Return, or
convert. That is what ibus-anthy offers too, and typing `nn` is what Japanese
typists do.

Because Tab carries both `predict` and `select_next_candidate`, what it appears
to do depends entirely on which mode you are in, which is how this misreading
arises in the first place.

## 3. Anthy's candidate 0 is "what you chose last time"

Not "the best conversion". This is the finding that invalidates any design
resting on a fixed position in the list.

**measured**, fresh record, `anthy_set_string` then `anthy_get_segment`:

```
にほん     11 candidates   [0] 日本  [1] 二本  [2] 二ホン … [9] にほん
にほんご    3 candidates   [0] 日本語 [1] にほんご [2] ニホンゴ
かんじ     24 candidates   [0] 漢字  [1] 監事 … [6] かんじ
わたし      7 candidates   [0] 私   [1] わたし
あめ       11 candidates   [0] 雨   [1] 飴   [2] あめ
はし       20 candidates   [0] 橋   [1] 端   [2] 箸  [3] はし
きょう    165 candidates   [0] 今日  [1] きょう
こうこう   28 candidates   [0] 高校 … [26] こうこう
```

The plain hiragana reading is **always present and never at 0**, at positions
9, 1, 6, 1, 2, 3, 1, 26.

Then `anthy_commit_segment(ctx, 0, 9)` — committing にほん *as the reading* — and
re-querying in a **new process** against the same record:

```
にほん     11 candidates   [0] にほん  [1] 日本  [2] 二本
```

The order changed and the change persisted. `anthy_do_set_prediction_str` and
the candidate ordering both consult the record; choosing a conversion once
demotes everything it outranked, permanently.

Two consequences:

- Nothing may assume the reading is at a known index. `NTH_UNCONVERTED_CANDIDATE`
  is the only reliable way to ask for it.
- Observations of anthy's candidate order are only meaningful with a stated
  record state. `tests/api/` handles this with the `api.engine_anthy.clean`
  fixture; ad-hoc testing against a real profile will disagree with itself.

Short readings *do* frequently put the reading at or near the top on a fresh
record — `に` has it at 0 — which is why the behaviour looks inconsistent rather
than history-driven when you meet it casually.

## 4. Prediction is history completion, and nothing else

`anthy_set_prediction_string` fills `ac->prediction` from
`anthy_traverse_record_for_prediction` (`engines/anthy-unicode/src-main/context.c:484`) —
the user's record. Not the dictionary.

**measured**, fresh record: every reading tried returned **0 predictions**. After
a single `anthy_commit_segment` of にほんご→日本語:

```
predict("に")     -> 1   [0] 日本語
predict("にほん")  -> 1   [0] 日本語
```

So prediction is: *complete a prefix into something this user has committed
before*. It is empty on first run, and empty whenever `PATHIME_OPT_LEARNING` is
off, because the adapter withholds `anthy_commit_segment` in that case
(`src/engines/anthy/anthy_backend.cc`, `record_choices`).

The prediction cache is entirely separate from `ac->seg_list`, so driving it
cannot disturb conversion — the obstruction that pushed `PATHIME_OPT_LEARNING`
to unsupported on pyzy does not exist here.

## 5. What "always show candidates" actually wants — and it is not prediction

The phone-keyboard target wants a candidate strip that is populated as the user
types, before any convert key. Anthy's prediction API cannot supply that: it is
empty until the user has a history.

Ordinary conversion, run eagerly on each growing prefix, can.

**measured**, fresh record, `anthy_reset_context` + `anthy_set_string` per kana,
reading segment 0's top candidates:

```
に        32 cands   に 二 荷 煮 丹          250 us
にほ       7 cands   仁保 二歩 2歩 ２歩 弐歩  178 us
にほん    11 cands   日本 二本 二ホン 2本     280 us
にほんご   3 cands   日本語 にほんご ニホンゴ  358 us
```

Real candidates from the first keystroke, no history required. Cost is 130 µs
to 1.7 ms per keystroke, the upper end being a twelve-kana sentence — affordable
at typing speed.

One caveat, also measured: as a sentence grows anthy **re-splits**, so the span
segment 0 covers is not monotonic.

```
きょうは       segs=1   seg0 = 今日は  (17 cands)
きょうはい     segs=2   seg0 = 今日    (165 cands)
きょうはいい   segs=2   seg0 = 今日は  (17 cands)
```

A candidate strip driven this way churns on long input. Fine for the
phrase-at-a-time typing a phone keyboard sees; ugly for a whole sentence.

**general knowledge:** this eager-conversion strip is what Gboard and the iOS
Japanese keyboard show. Desktop Japanese IMEs do *not* show candidates before a
convert key, which is a deliberate difference and not an oversight.

The strip is `PATHIME_OPT_PREDICTION`, on by default. It is an option precisely
because the paragraph above describes two deliberate paradigms, not one
behaviour and a bug: a client that wants the desktop convert-on-request shape
turns it off, and the name is kept because 予測入力 is what Japanese IMEs call
exactly this strip.

**Selecting from the strip settles greedily, and typing continues.** The chosen
text stays *preedit* rather than committing — pyzy's partial `selectCandidate()`
shape — the composer is re-seeded with the readings of the segments the choice
did not consume (`RomajiComposer::assign_kana()`), and conversion re-runs on the
remainder; when nothing remains the composition commits whole. Around that:

- **Browsing moves no text.** The strip's candidates arrived unbidden, so moving
  the candidate cursor through them settles nothing and leaves the preedit as
  kana. After Space the same list is being *chosen among*, and the cursor
  previews. That is the per-moment rule in `docs/CONCEPTS.md`, *Candidate
  cursor*, not an engine quirk.
- **Space adopts whatever the user browsed to** rather than restarting at
  candidate 0. A moved highlight is the user's most recent expression of
  interest; with an untouched cursor the two rules are indistinguishable, so the
  desktop habit is unaffected.
- **Un-settling exists.** Backspace deletes remainder kana first, then walks the
  most recent selection back to the reading it consumed; Escape un-settles every
  selection at once, and a second press discards the buffer.
- **Learning re-stages each strip choice on its own** (`learn_eager_choice()`):
  convert just the reading that choice consumed and commit the matching
  candidate as the only — and therefore last — segment, which is what flushes
  anthy's record. Doing it in place cannot work, because the flush needs every
  segment committed before the remainder re-runs. The cost, stated plainly: the
  solo commit loses anthy's view of the surrounding segments.
- Eager conversion feeds `anthy_set_string()` from the composer's resolved
  reading — the same form the convert key uses — so the strip may show
  candidates for にほん while the preedit reads にほn. That is the split Return
  has always had, now visible before a romaji sequence is finished.

## 6. anthy's preedit is the reading, and its auxiliary text is empty

**measured** through `pathime.h`, anthy, typing `nihon`, nothing settled: the
preedit is `にほn` with `preedit_settled` 0, there are no candidates until Space,
and Space converts while staying in composition rather than committing. With
nothing composing at all, Space is reported **unhandled**.

The pending romaji is genuinely part of anthy's preedit and not an artefact:
`jastring.get_hiragana()` maps each segment through `to_hiragana()`, which
returns `_jachars` if the romaji resolved and the raw `_enchars` if it did not
(`engine/python3/segment.py:71-74`). The trailing `n` becomes `ん` only when the
`commit=True` form is used (`jastring.py:259-264`), which is why `nihon` + Return
commits にほん. `RomajiComposer` splits `display()` from `commit_text()` the same
way.

**ibus-anthy's auxiliary text carries nothing worth having.** It is built as
`'( %d / %d )' % (cursor + 1, count)` (`engine.py:1291`) — the candidate position
and total, which this API hands the client as `candidate_cursor` and
`candidate_count`. In composing mode (`__update_input_chars`, `engine.py:1208`)
no auxiliary text is set at all.

**This API has no auxiliary text**, and anthy is the clearest case for why the
concept is unnecessary: across the four engines there is nothing left to put in
the field. hangul has none. anthy's is empty while composing and a counter
otherwise. pyzy's is the *preedit* under another name (§7). The table engine's
is `get_aux_strings()` (`refs/ibus-table/engine/table.py:1732`) — the raw key
run mapped through `char_prompts`, plus the same `current / total` counter — and
a key run is preedit text (`docs/ibus-table-mapping.md` §2, *Preedit text*).

## 7. pyzy's three text parts are really two, and they rearrange on selection

**measured** through pyzy directly (`selectedText` / `conversionText` /
`restText` / `auxiliaryText` / `inputText`):

```
typing "nihao"
  n     selected=""    conversion="了"      rest=""   aux="n|"        input="n"
  ni    selected=""    conversion="你"      rest=""   aux="ni|"       input="ni"
  nih   selected=""    conversion="你好"    rest=""   aux="ni h|"     input="nih"
  niha  selected=""    conversion="立法"    rest=""   aux="ni ha|"    input="niha"
  nihao selected=""    conversion="你好"    rest=""   aux="ni hao|"   input="nihao"

selectCandidate(2) = 你   -- covers only the first syllable
        selected="你"  conversion="好"      rest=""   aux="hao|"      input="nihao"
```

Three things fall out:

- **`restText()` is empty in every ordinary case.** Across full pinyin runs of
  1–16 characters, including an unparseable tail (`nihaoq` → `你好去`), it never
  became non-empty. The documented three-part preedit is two parts in practice;
  `restText` is for the case where a syllable cannot be parsed at all, which is
  reachable mainly through fuzzy/bopomofo settings.
- **`auxiliaryText()` is the *remaining* raw input, not the whole of it.** After
  selecting 你 the aux drops `ni` and reads `hao|`, while `inputText()` still
  reads `nihao`. So aux tracks exactly what has not yet been converted.
- **`selectedText() + auxiliaryText()` is anthy's shape.** `你` + `hao|` reads
  `你hao|` — settled conversions followed by the not-yet-converted input, which
  is precisely `settled + active-as-typed` in anthy terms. This is what the
  adapter projects; `conversionText()` is read nowhere.

- **The `|` is always trailing.** `cursor()` equals the input length at every
  step above, because this library never sends pyzy a cursor movement:
  Left/Right are declined while composing, and on pyzy that is forced rather
  than chosen, because routing them through makes pyzy render its own input
  cursor as a literal `|` inside `conversionText()` (`PinyinContext.cc:129-142`)
  — typing `nihao` then Left gives `ni h|a`, a display marker inside a string
  the API promises is plain content text. A marker that can only ever appear at
  the end carries no information, so the adapter strips it.

## 8. Segments: the machinery works, only re-splitting is missing

**measured**, anthy, whole clauses on a fresh record:

```
にほんごをべんきょうします   -> 2 segments: 日本語を | 勉強します
きょうはいいてんきですね     -> 3 segments: 今日は | 良い | 天気ですね
わたしはがくせいです         -> 2 segments: 私は | 学生です
```

and driven through `pathime.h`, greedy resolution walks them:

```
Space          preedit 今日はいいてんきですね   settled=0   17 candidates for 今日は
select         preedit 今日はいいてんきですね   settled=3   18 candidates for いい
```

What this model gives up against ibus-anthy is not segment *conversion* but
segment *re-splitting* — `shrink_segment` / `expand_segment`, bound to
Shift+Left / Shift+Right at modes `2,5` (`engine.py:2435,2443`). That is a repair
tool for when anthy splits wrongly, and it has no phone-keyboard equivalent.
Segment navigation proper (`select_next_segment` etc., Left/Right at modes `2,5`)
is what greedy resolution replaces.

**There is no active-span field, and under the strip the span is not otherwise
visible.** A client sees where `settled` ends (`preedit_settled`) but not where
the *active span* ends, so it can see `きょうはいいてんきですね` with candidates
for `今日は` and no way to tell they cover only the first four kana. After Space
the boundary is inferable, because the preedit shows the conversion; before it,
with candidates published and no preview, it is not. A composition-level span
could not be honest in any case: one pyzy candidate list mixes entries covering
different spans — 你 beside 你好 — so there is no single span the field could
report.

## 9. Return is consistent; the preview is not

**measured**, each engine driven through its own library. The pyzy preedit in
the third block is `conversionText()` — pyzy's own preview — and not what this
API publishes for pyzy, which is the typed input (§7):

```
anthy   nihon          preedit にほn     0 candidates
        Return         commits にほん

anthy   nihon Space    preedit 日本      11 candidates, cursor 0
        Return         commits 日本

pyzy    nihao          preedit 你好      64 candidates, cursor 0
        Return         commits "nihao"
```

Return does the same thing in both: commit what the user has explicitly settled.
Anthy's Space *is* an explicit choice, so 日本 is settled and Return takes it.
pyzy's user never asked for 你好 — pyzy previewed it unbidden — so Return
declines it.

So Return is not the inconsistency. The inconsistency is that **pyzy rewrites the
preedit with a conversion the user did not request and anthy never does.** The
preview is also visibly unstable mid-word:

```
n → 了     ni → 你     nih → 你好     niha → 立法     nihao → 你好
```

Whether an engine may preview at all is therefore a single question, not three:
Return's apparent split, the preedit/auxiliary asymmetry between the two
engines, and the shape of any always-on candidate mode all turn on it.
`docs/CONCEPTS.md` answers it against the preview — the preedit is what the user
typed, and a conversion nobody asked for is a candidate instead — which is why
the adapter takes the preedit from `auxiliaryText()` (§7).

Two consequences for the pyzy adapter, both pinned by `api.engine_pyzy`:

- **`commit(TYPE_CONVERTED)` does not agree with the preedit under double
  pinyin.** It commits the raw keystrokes — `nihk` for what the preedit shows
  as `ni hao`. So Return derives its text from the published preedit instead of
  delegating, which makes the guarantee structural rather than coincidental.
- **pyzy suppresses its auxiliary text entirely when it has no candidate**
  (`PinyinContext.cc:163-167`), leaving the typed input in `restText()`.
  Bopomofo hits this on the first key of most syllables — `,` is ㄝ, which
  matches no phrase — so without a fallback the user types a zhuyin symbol and
  sees nothing.

## 10. Reproducing these measurements

The probes were one-off instruments rather than assertions, so they are not in
the tree. Each is a single translation unit, and the recipes below are exact
because the setup is the part that costs time — every one of them has at least
one non-obvious step that is not worth rediscovering.

Build the library first; the paths assume a build directory `$B` configured
against this source tree and kept outside it — under `/tmp` when building on
Linux against a Windows-hosted checkout, where symlinks and case-insensitivity
otherwise get in the way.

**Against the public API** — the most useful of the three, and the one to write
first when touching an adapter. Drive `pathime_context_process_key()` and print
the whole `pathime_composition_t` after every key: preedit, `preedit_settled`,
candidate count, cursor, the first few candidates, and everything the
`commit_text` callback has received so far.

```
gcc -std=c11 -o trace trace.c -I<source>/include -I$B/include \
    -L$B/lib -lpathime -Wl,-rpath,$B/lib
```

Run it from `$B/lib`, so the default `resource_dir` — `pathime-data` beside the
library — resolves. Map a few punctuation characters to the non-printable keys
you need (`_` for Space, `=` for Return, `<` for Backspace) so a whole session
is one command-line argument.

**Against anthy directly**, for candidate order, record dependence,
segmentation and timings. Link *both* libraries — `-lanthy-unicode` alone fails
to resolve `anthy_set_logger`:

```
gcc -std=gnu11 -o probe probe.c -I<source>/anthy-unicode -I$B/cmake/ports/anthy-unicode \
    -DPROBE_DIC='"'$B'/cmake/ports/anthy-unicode/dic/anthy.dic"' \
    -L$B/lib -lanthy-unicode -lanthydic-unicode -Wl,-rpath,$B/lib
```

`gnu11` rather than `c11` if you time anything: `clock_gettime` needs it. Point
anthy at the build tree with the same four `anthy_conf_override()` calls
`tests/anthy/anthy_test_util.h` uses, and give each run a **scratch `HOME` that
is wiped first** — §3 is why that is not optional.

**Against pyzy directly**, for the four text surfaces:

```
g++ -std=c++17 -o pyprobe pyprobe.cc -I$B/src/include \
    -DPROBE_DATA='"'$B'/lib/pathime-data"' -DPROBE_HOME='"<scratch>"' \
    -L$B/lib -lpyzy-1.0 -Wl,-rpath,$B/lib
```

Three things the headers do not make obvious: the data directory is
`pyzy_set_data_dir()` from `<PyZy/DataDir.h>`, *not* an argument to
`InputContext::init()`, which takes only a cache and a config directory; there
is no `candidates()` accessor, so enumerate with `hasCandidate(i)` and
`getCandidate(i, out)`; and `Observer` is pure virtual, so all six methods need
overriding even to print one.

Any of these that becomes an assertion rather than an observation belongs under
`tests/`; see `docs/testing.md` for which suite.

**A record state must be stated for any anthy candidate measurement to mean
anything** — see §3. All fresh-record figures above used a scratch `HOME` wiped
immediately before the run.
