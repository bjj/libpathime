# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

`libpathime` is a new IBus input method engine library. Three backends wrap
vendored submodules (libhangul, anthy-unicode, pyzy); the fourth, the
table-driven engine, is **ours** — `ibus-table` is Python, so it was the
reference feature set rather than a linkable library, and the engine is written
in `src/engines/table/` against the data format in
`docs/ibus-table-mapping.md`. The build, the core, all
four adapters, options and negotiation including tier 3, and the terminal demo
are implemented and tested; the table engine is Linux-verified only so far, and
what it still lacks (learning, full-width conversion, pinyin and suggestion
modes) is in `TODO.md`.

> **Build:** see `BUILD.md`. NOTE! If you are on Linux, you are in a sbx
> environment on Windows: build in `/tmp` to avoid issues with symlinks and
> case-insensitivity. A source change a vendored library needs is a **commit on
> that submodule's `libpathime` branch**, never a rewrite applied during the
> build; what stays in the build is the compat layer and the two configure-time
> copies that express our contract rather than fix a bug (`docs/windows-port.md`).

## Repository Structure

```
TODO.md                  # Upcoming work only — start here for "what's next"
include/pathime/         # Public C API
  pathime.h              # The API itself; config.h is generated here by CMake
docs/                    # Documentation — see "Where to look" below
src/                     # Library implementation; the map is docs/source-layout.md
  backend.h              # The seam between core and adapters
  composition.h          # The structured composition model core and adapters share
  engines/               # One adapter directory per backend, plus table/ for the future table engine
tests/                   # Test suites: one per backend, plus api/ and core/ — see docs/testing.md
demo/                    # Interactive terminal IME demo (LIBPATHIME_BUILD_DEMO=ON); README.md is the guide
  cpp-terminal/          # Submodule — portable terminal library, used only by the demo
examples/
  install-check/         # Standalone consumer of an *installed* libpathime; CI runs it against every install layout
refs/                    # Local reference clones — gitignored, not submodules
  ibus-hangul/           # C engine wrapping libhangul; straightforward single-library integration
  ibus-anthy/            # Python engine wrapping anthy-unicode — pinned to bjj/ibus-anthy @ 0962741
  ibus-pinyin/           # C++ engine wrapping pyzy; most complex, Pinyin and Bopomofo
  ibus-table/            # Self-contained Python engine — the reference our table engine was specified from

engines/                 # Vendored — three libraries and one data set; anthy and pyzy
                         # carry portability commits on a `libpathime` branch
  libhangul/             # Korean input library
  anthy-unicode/         # Japanese kana-kanji conversion library
  pyzy/                  # Chinese Pinyin/Bopomofo conversion library
  ibus-table-chinese/    # Table sources (Wubi, Cangjie, …); data, not code — its own
                         # CMake build is unused. mike-fabian/ibus-table-chinese @ 1.8.14
tools/                   # Build-time tools: the table compiler, the variant generator
```

After cloning, `git submodule update --init --recursive`.

## Where to look

Every document opens with its own statement of what it is for; this list is
only the routing.

**Three documents are development-only and must stay leaves: this file,
`TODO.md` and `docs/design-history.md`.** They may cite anything; nothing may
cite them. Code comments, the public header and every other document are
written to stand on their own, so that all three could be deleted with nothing
dangling. When a round settles something a reader of the code needs, put the
substance in the code or in the permanent document that owns it, and leave only
the reasoning in the history.

- `TODO.md` — upcoming work, open questions, deliberate deferrals. Nothing
  settled lives there.
- `docs/design-history.md` — the settled decisions: ruling, reason, cost, and
  what would legitimately reopen each. **Read it before reopening anything
  that looks undecided.** §6 is the table engine; the full narrative behind
  every entry is in that file's git history.
- `docs/CONCEPTS.md` and `include/pathime/pathime.h` — the model and the
  contract, kept in lockstep; neither carries deviations from the other.
- `docs/source-layout.md` — which file owns what. Read before adding or
  moving implementation code.
- `docs/japanese-input-model.md` — measured anthy/ibus-anthy behaviour. Read
  before designing anything Japanese-facing, and before trusting a
  candidate-order observation taken against a real profile.
- `docs/testing.md` — the suites and what is deliberate about them. Read
  before concluding a missing test is a bug.
- `docs/*-mapping.md` — one per backend, describing how a living external thing
  connects to this library; read before upgrading the submodule it covers. The
  three vendored ones map a C API with file:line citations;
  `ibus-table-mapping.md` maps a *data format*, because there is no library to
  link. Each ends in "Impedance mismatches".
- `src/engines/table/README.md` — the table engine's own map, including the
  data/behaviour boundary inside that directory and what the ibus-table data
  contract costs.
- `THIRD-PARTY.md` — what libpathime links and ships, and under what terms.
  Read before changing how a vendored library is built or linked, before
  vendoring anything new, and before adding a file to `pathime-data/`.
- `BUILD.md`, `docs/windows-port.md`, `demo/README.md` — build (including
  "Shipping the data"), the Windows port, the demo.

## How we work

The API was designed interactively in small rounds, one topic at a time, and the
same habits should carry into the implementation:

- **Verify against source before designing around a claim.** Every assertion
  about backend behaviour in these docs was checked in the submodule or `refs/`
  tree and cited by file:line. When a design hinges on "engine X needs Y," go
  read engine X first — twice now that check changed the answer (keycodes turned
  out to matter only to Hangul and only as key *position*; the no-locking claim
  turned out to be unsupportable once anthy's and pyzy's process-global
  conversion state came to light).
- **Remove concepts nothing needs.** Key releases, forwarded key events, engine
  activation state, client capability flags, and client-managed candidate
  reservation were all cut once no backend justified them. A concept carried
  "just in case" costs every future reader.
- **Prefer a determinate rule to a deferral.** Where backends disagree, pick one
  fixed behaviour and write down why, rather than declaring it engine-dependent,
  negotiable, or undefined. The stale-snapshot recovery is the model case: an
  engine that cannot see the text it wants to revise abandons the revision and
  treats what is in the document as final, rather than guessing or refusing the
  key. Reserve "undefined" for things genuinely outside the model.
- **State the cost of a decision, not just the decision.** Where something was
  given up — the JIS ¥ case, thumb-shift layouts, segment navigation — say so
  plainly at the point of the choice.
- **The phone-keyboard target breaks ties.** Greedy left-to-right resolution,
  no segment navigation or resizing, and no exposure of anthy's multi-segment
  nature all follow from it.
- **A fix to a vendored library is a commit in that submodule**, on its
  `libpathime` branch, narrowly scoped and titled — not a rewrite applied while
  the build runs. Keep the upstream branch clean so a bump is a rebase. What
  belongs in our build instead is anything that is *not* a fix to the library:
  the compat layer, the build files, and the two configure-time copies that
  express libpathime's contract with anthy and pyzy. See `docs/windows-port.md`.
- **Write in the present tense; the library is unreleased.** Nothing has
  shipped, so there is no "used to", no "no longer does", no "was widened", and
  no dated changelog entry. A comment that explains the current shape by
  contrasting it with an earlier one makes every reader reconstruct a decision
  they do not need. State what is true and why.
- **Keep commit messages short.** A subject line and a few sentences. The
  reasoning belongs in the code comments and in `docs/`, where it is next to
  what it explains and gets updated with it; a commit message repeating it just
  goes stale somewhere no one looks. Say what changed and why, not how it was
  decided — `git log` is not a design document.
