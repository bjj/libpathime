# The adapter findings

The six numbered constraints that shape the adapter layer — why it is more
than a thin shim over the three vendored libraries. They came out of the
mapping review (see `docs/*-mapping.md`), still hold, and are cited by number
from `src/` comments and from `docs/*-options.md`; the numbering is inherited
from the old `PLAN.md` and must not be reused or reordered. This file was
extracted from `TODO.md` §2 when that file was compacted to upcoming work
(2026-07-28); `docs/design-history.md` holds the rest of what moved.

### Finding 1 — the internal model must be richer than the API projection

Every backend keeps state that the flat `{preedit, preedit_settled,
auxiliary, candidates}` value cannot hold: anthy has N segments, each with its
own candidate array, plus an active-segment index; pyzy's preedit is three parts
(`selectedText | conversionText | restText`) with the middle one provisional and
its own focused-candidate index; libhangul exposes only the trailing mutable
syllable, so the settled prefix must be accumulated by us. Keep the structured
form internally and compute the flat value at the boundary on every change. The
API's `preedit_settled` is the boundary between the settled prefix and the
still-mutable region, and the candidate list always describes the leftmost
unsettled span — that projection is what makes greedy resolution work.

### Finding 2 — we own the "currently shown" candidate

Neither anthy nor pyzy durably records which candidate the user is hovering
before commit — anthy records only at `anthy_commit_segment` time. The input
context must track it itself.

### Finding 3 — two-layer lifetime everywhere

Process-global one-time init (pyzy's shared SQLite `Database` and
`SpecialPhraseTable` via `InputContext::init()`; anthy's `anthy_init()`) is
separate from per-context handles (`HangulInputContext*`, `anthy_context_t`,
`PyZy::InputContext*`), which are one owned handle each and caller-destroyed.

**Corrected during the `src/` stub-out:** this finding used to name libhangul's
keyboard registry via `hangul_init()` as a third case. It is not one in our
build. `hangul_init()`/`hangul_fini()` exist only under
`ENABLE_EXTERNAL_KEYBOARDS` (`libhangul/hangul/hangul.h:99-103`,
`hangulkeyboard.c:994-1033`), which the top-level `CMakeLists.txt:34` turns off
to avoid an EXPAT dependency and a `sed -i` codegen step. Without it the nine
built-in layouts are static tables and hangul has **no** process-global setup at
all — it is the one backend whose availability cannot fail at runtime. Recorded
at the two places that would otherwise have coded around it, in `src/init.cc`'s
`pathime_init()` and `src/engine.cc`'s registry.

This one is already realized in the API: `pathime_init()` / `pathime_shutdown()`
are the global layer, `pathime_engine_*` holds what is shared across contexts,
and `pathime_context_*` is the per-context layer. It stays listed because the
options docs classify options by which layer they belong to, and because it is
the reason `pathime_init()` is documented as the one slow call.

### Finding 4 — encoding is not uniform

Conversion happens at every boundary. libhangul's composition API is UCS-4 and
everything we hand out is UTF-8; anthy is UTF-8 only after
`anthy_context_set_encoding(ctx, ANTHY_UTF8_ENCODING)` and its `seg_len` counts
input reading xchars, not bytes; pyzy is UTF-8 but its `cursor()` is a byte
offset into the raw ASCII input, which must never be conflated with output
scalar positions. Returned strings from all three are borrowed and volatile —
valid only until the next mutating call — so copy immediately.

### Finding 5 — push and pull must be reconciled

pyzy fires six granular `Observer` callbacks synchronously *during* a mutation
call; anthy and libhangul are pull-only. A small per-context Observer that sets
dirty flags, plus a post-call assembly step, reconciles all three into one
atomic composition value. No event loop is needed. (ibus-pinyin does not buffer,
so it is not a model for this.)

### Finding 6 — we own the whole key-event layer

The backends take only finished input: anthy wants completed kana, pyzy
accepts only `[a-z]` and `'`, and libhangul takes a US-QWERTY `int` (uppercase =
Shift, no modifiers, no release, backspace via a separate
`hangul_ic_backspace`). Everything from `pathime_key_event_t` to
handled/unhandled to preedit assembly — including the romaji/kana state machine
for Japanese — is ours. Only candidate retrieval, selection, and commit map onto
backend calls.

### Obligations from the API round

**Candidates must be materialized eagerly, up to the cap.** The header promises
`pathime_context_candidate()` is callback-safe, which is only true if every
candidate the cap allows is fetched *before* `composition_changed` is
dispatched. pyzy's `hasCandidate(i)` is lazy and mutating, so this is a real
obligation, not a formality.

Per-backend gotchas (flush semantics, the unknown-keyboard crash, the two-call
length protocol, which pyzy properties are honoured only in subclasses) are
documented in `docs/*-mapping.md` with file:line citations. Consult those rather
than re-deriving.
