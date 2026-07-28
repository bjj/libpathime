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
guarded by `_chk_mode` (`engine/python3/engine.py:1969-1990`), whose five states
are mutually exclusive:

| `_chk_mode` | state | Space does |
|---|---|---|
| `0` | nothing composing | insert a space — **full-width** by default |
| `1` | composing kana, not yet converted | `convert` |
| `2` | converting, candidate list hidden | `select_next_candidate` |
| `3` | predicting, candidate list hidden | `select_next_candidate` |
| `4` | an F-key convert mode (katakana, wide latin, …) | `convert` |
| `5` | candidate list visible, any mode | `select_next_candidate` |

**source:** `__cmd_convert` is guarded `_chk_mode('14')` (`engine.py:2274`),
`__cmd_select_next_candidate` is guarded `_chk_mode('235')` (`engine.py:2547`),
`__cmd_insert_half_space` is guarded `_chk_mode('0')`.

So "Space converts, and then Space advances the candidate cursor" is not a
special case bolted onto a convert key. They are two disjoint commands that
happen to share a binding, and the state decides which one exists. This is worth
stating because the alternative reading — one key with a mode-dependent
exception — makes it look like something we invented and could remove.

**general knowledge:** Space-converts-then-Space-cycles is identical in MS-IME,
ATOK, Google Japanese Input and macOS. It is the single most ingrained habit a
Japanese typist has, and an engine that breaks it reads as broken. We match it.

## 2. Tab is `predict`, and after conversion it is a second Space

Same dual life. `'predict': ['Tab', 'ISO_Left_Tab']` at modes `1,4`
(`gschema.xml.in:938`, `__cmd_predict` at `engine.py:2283`), and Tab also appears
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
`engine.py:1590-1600`, rendered by `__update_convert_chars` at
`engine.py:1226-1228`). That family is deliberately out of scope — `TODO.md` §1
records the decision and what it costs.

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
`anthy_traverse_record_for_prediction` (`anthy-unicode/src-main/context.c:484`) —
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
to unsupported on pyzy does not exist here. What is in doubt is the value, not
the feasibility.

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

## 6. Preedit and auxiliary: the two engines were mirror images

**Superseded by the work this file prompted.** The table below is what the two
engines did *before* `docs/CONCEPTS.md` gained its preedit rule and
`pathime_composition_t` lost its auxiliary field. It is kept because the
asymmetry is the evidence for both changes, and because a reader meeting the
rule needs to see what it replaced. What is true now: the preedit is what the
user typed in the script they are composing in, for every engine; there is no
auxiliary text; and Return commits what is on screen everywhere.

**measured** through `pathime.h`, composing state, nothing settled:

| | anthy, typing `nihon` | pyzy, typing `nihao` |
|---|---|---|
| preedit | `にほn` — the reading, pending latin inline | `你好` — a conversion preview |
| `preedit_settled` | 0 | 0 |
| auxiliary | *(empty — always)* | `ni hao\|` — raw input, segmented, with a cursor mark |
| candidates | 0 until Space | 64 immediately |
| Space | convert, stay composing | **commit** |
| Space, nothing composing | **unhandled** | commits `" "` |

The same two facts — what the user typed, and what it might become — were in
**opposite fields** in the two engines. A client could not write one renderer
for both. That is what the preedit rule fixed: what the user typed is the
preedit in both, and what it might become is the candidate list in both.

The pending romaji is genuinely part of anthy's preedit and not an artefact:
`jastring.get_hiragana()` maps each segment through `to_hiragana()`, which
returns `_jachars` if the romaji resolved and the raw `_enchars` if it did not
(`engine/python3/segment.py:71-74`). The trailing `n` becomes `ん` only when the
`commit=True` form is used (`jastring.py:259-264`), which is why `nihon` + Return
commits にほん. Our `RomajiComposer` already splits `display()` from
`commit_text()` the same way.

**ibus-anthy's auxiliary text carries nothing worth having.** It is built as
`'( %d / %d )' % (cursor + 1, count)` (`engine.py:1296`) — the candidate position
and total, which this API already hands the client as `candidate_cursor` and
`candidate_count`. In composing mode (`__update_input_chars`, `engine.py:1208`)
no auxiliary text is set at all.

That was the first of the four checks that removed the field. hangul never had
one; anthy's was empty; pyzy's turned out to be the *preedit* under another
name (§7); and the table engine's is `get_aux_strings()`
(`refs/ibus-table/engine/table.py:1732`), which is the raw key run mapped
through `char_prompts` plus the same `current / total` counter — the key run
being preedit text under the rule, exactly as `docs/ibus-table-spec.md` §6.2
already specified before any of this. Four engines, nothing left in the field.

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
  is precisely `settled + active-as-typed` in anthy terms. This is now what the
  adapter projects, and `conversionText()` is read nowhere.

- **The `|` is always trailing.** cursor() equals the input length at every step
  above, because this library never sends pyzy a cursor movement — Left/Right are
  declined while composing (`TODO.md` §3 q3, and the reason is that routing them
  through made pyzy render the same `|` inside `conversionText()`). A marker that
  can only ever appear at the end carries no information.

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

**One real gap for eager candidates.** A client can see where `settled` ends
(`preedit_settled`) but not where the *active span* ends. Today that is
inferable, because after Space the preedit shows the conversion and the boundary
is visible in the text. If candidates were published without previewing, a client
would see `きょうはいいてんきですね` with candidates for `今日は` and no way to
know they cover only the first four kana.

## 9. Return was already consistent; the preview was not

**measured**:

```
anthy   nihon          preedit にほn     0 candidates
        Return         commits にほん

anthy   nihon Space    preedit 日本      11 candidates, cursor 0
        Return         commits 日本

pyzy    nihao          preedit 你好      64 candidates, cursor 0
        Return         commits "nihao"
```

Return does the same thing in both: commit what the user has explicitly settled.
Anthy's Space *was* an explicit choice, so 日本 is settled and Return takes it.
pyzy never asked for 你好 — it previewed it unbidden — so Return declines it.

So Return is not the inconsistency. The inconsistency is that **pyzy rewrites the
preedit with a conversion the user did not request and anthy never does.** The
preview is also visibly unstable mid-word:

```
n → 了     ni → 你     nih → 你好     niha → 立法     nihao → 你好
```

Deciding whether an engine may preview settled Return's apparent split, the
preedit/auxiliary mirror image, and the shape of any always-on candidate mode,
all at once. It was one decision, not three, and it went against the preview.

Two things turned up while implementing it that reasoning had not predicted,
both now covered by `api.engine_pyzy`:

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

The probes live in the session scratchpad rather than the tree, because they are
one-off instruments rather than assertions. Each is a single translation unit:

- against anthy directly, linked to `libanthy-unicode` with
  `anthy_conf_override("DIC_FILE", …)` and a scratch `HOME`, exactly as
  `tests/anthy/anthy_test_util.h` does — candidate order, record dependence,
  segmentation, prediction, eager-conversion timings;
- against pyzy directly, linked to `libpyzy-1.0` with `pyzy_set_data_dir()` and a
  scratch cache/config pair — the four text surfaces across a partial selection;
- against `pathime.h`, linked to `libpathime`, printing the whole
  `pathime_composition_t` after every key — everything in the comparison tables.

Any of these that becomes an assertion rather than an observation belongs under
`tests/`; see `docs/testing.md` for which suite.

**A record state must be stated for any anthy candidate measurement to mean
anything** — see §3. All fresh-record figures above used a scratch `HOME` wiped
immediately before the run.
