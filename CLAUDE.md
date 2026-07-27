# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

`libpathime` is a new IBus input method engine library. The IME concepts are described in `docs/CONCEPTS.md`.

Three backends wrap vendored submodules (libhangul, anthy-unicode, pyzy). A fourth, the table-driven engine behind `PATHIME_ENGINE_TABLE`, is **ours to write**: `ibus-table` is Python and cannot be linked against, so it serves as the reference feature set only, and `docs/ibus-table-spec.md` is the specification we implement. It does not exist yet — `LIBPATHIME_WITH_TABLE` defaults OFF and is refused if turned on — but it is a real engine for API-design purposes. See `TODO.md` §4.

> **Build:** a CMake build for the three submodules is in place and verified on both Linux and Windows (MSVC and clang-cl). See `BUILD.md` — its "Windows" section documents the compat layer, the generated source variants, and the known runtime limitations, and its "Tests" section covers the suites under `tests/`. The vendored submodule trees are never edited; if something needs a source change to compile on Windows, it goes through the compat headers or a configure-time generated copy.

## Repository Structure

```
TODO.md                  # Unfinished business — start here for "what's next"
include/pathime/         # Public C API
  pathime.h              # The API itself; config.h is generated here by CMake
docs/                    # Documentation and project design information
  CONCEPTS.md            # Detailed description of all IME concepts
  ibus-table-spec.md     # Clean-room behavioral spec for ibus-table (source format, DB schema, engine logic)
  *-mapping.md           # Overview of how the submodule libraries relate to the concepts
  *-options.md           # Per-library option inventories, for the options design round
src/                     # Library implementation — stubbed skeleton; the map is docs/source-layout.md
  engines/               # One adapter directory per backend, plus table/ for the future table engine
tests/                   # Test suites: one directory per backend, plus api/ — see BUILD.md
refs/                    # Local reference clones — gitignored, not submodules
  ibus-hangul/           # Korean IBus engine (reference)
  ibus-anthy/            # Japanese IBus engine (reference)
  ibus-pinyin/           # Chinese Pinyin IBus engine (reference)
  ibus-table/            # Table-based IBus engine (reference, self-contained)
  ibus-table-chinese/    # Chinese table data for ibus-table (bjj/ibus-table-chinese)

libhangul/               # Submodule — Korean input library
anthy-unicode/           # Submodule — Japanese kana-kanji conversion library
pyzy/                    # Submodule — Chinese Pinyin/Bopomofo conversion library
```

See `SUBMODULES.md` for upstream URLs and pkg-config details for each submodule.

## Submodules

After cloning, initialize submodules with:

```bash
git submodule update --init --recursive
```

## Reference Codebases

The `refs/` directory contains IBus engine implementations and table data to study:

- **ibus-hangul**: C engine wrapping `libhangul`; straightforward single-library integration
- **ibus-anthy**: Python/GObject Introspection engine wrapping `anthy-unicode` — pinned to `bjj/ibus-anthy` @ `0962741`
- **ibus-table**: Self-contained Python engine; no external IM library dependency — which is exactly why we reimplement it rather than wrap it (see `docs/ibus-table-spec.md`)
- **ibus-pinyin**: C++ engine wrapping `pyzy`; most complex, supports Pinyin and Bopomofo
- **ibus-table-chinese**: Chinese input method table sources (Wubi, Cangjie, Stroke5, Zhuyin, etc.) for use as test data

## Documentation

- `TODO.md` — unfinished business: the design rounds not yet held, the adapter-layer constraints, the open questions, and the known loose ends. Start here for "what's next."
- `docs/CONCEPTS.md` — defines the canonical IME concepts (engine, client, composition, etc.) that `libpathime` implements
- `include/pathime/pathime.h` — the public C API for the core input loop. Settled. Kept in lockstep with `docs/CONCEPTS.md`; the two do not disagree, so neither carries a list of deviations from the other.
- `docs/source-layout.md` — the map of `src/` and `tests/api/`: which file owns
  which responsibility (keyed to `TODO.md` §2's findings), the conventions, and
  which choices are settled versus deliberately open. Read it before adding or
  moving implementation code.
- `docs/ibus-table-spec.md` — the specification for our own table engine, derived clean-room from ibus-table: source `.txt` file format, compiled SQLite schema, key-event state machine, candidate sorting, and implementation notes for the C++ port
- `docs/*-mapping.md` — per-library notes mapping each submodule's API to the concepts. Verified against the actual submodule source (see cited file/line references throughout); each ends with an "Impedance mismatches" section.
- `docs/*-options.md` — per-library inventories of configurable options, gathered for the not-yet-held negotiation/options design round.

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
  negotiable, or undefined. Focus loss preserving composition state is the model
  case. Reserve "undefined" for things genuinely outside the model.
- **State the cost of a decision, not just the decision.** Where something was
  given up — the JIS ¥ case, thumb-shift layouts, segment navigation — say so
  plainly at the point of the choice.
- **The phone-keyboard target breaks ties.** Greedy left-to-right resolution,
  no segment navigation or resizing, and no exposure of anthy's multi-segment
  nature all follow from it.
- **Never edit the vendored submodule trees.** Windows fixes go through the
  compat headers or a configure-time generated copy; see `BUILD.md`.
