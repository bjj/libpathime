# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

`libpathime` is a new IBus input method engine library. The IME concepts are described in `docs/CONCEPTS.md`.

## Repository Structure

```
docs/                    # Documentation and project design information
  CONCEPTS.md            # Detailed description of all IME concepts
  ibus-table-spec.md     # Clean-room behavioral spec for ibus-table (source format, DB schema, engine logic)
  *-mapping.md           # Overview of how the submodule libraries relate to the concepts
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
- **ibus-table**: Self-contained Python engine; no external IM library dependency
- **ibus-pinyin**: C++ engine wrapping `pyzy`; most complex, supports Pinyin and Bopomofo
- **ibus-table-chinese**: Chinese input method table sources (Wubi, Cangjie, Stroke5, Zhuyin, etc.) for use as test data

## Documentation

- `docs/CONCEPTS.md` — defines the canonical IME concepts (engine, client, composition, etc.) that `libpathime` will implement
- `docs/ibus-table-spec.md` — complete behavioral specification for ibus-table: source `.txt` file format, compiled SQLite schema, key-event state machine, candidate sorting, and clean-room implementation notes for a C++ port
- `docs/*-mapping.md` — per-library notes mapping each submodule's API to the concepts
