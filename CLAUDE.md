# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

`libpathime` is a new IBus input method engine library, currently in the scaffolding stage. No source code exists yet.

## Repository Structure

```
refs/            # Local reference clones — gitignored, not submodules
  ibus-hangul/   # Korean IBus engine (reference)
  ibus-anthy/    # Japanese IBus engine (reference)
  ibus-pinyin/   # Chinese Pinyin IBus engine (reference)
  ibus-table/    # Table-based IBus engine (reference, self-contained)

libhangul/       # Submodule — Korean input library
anthy-unicode/   # Submodule — Japanese kana-kanji conversion library
pyzy/            # Submodule — Chinese Pinyin/Bopomofo conversion library
```

See `SUBMODULES.md` for upstream URLs and pkg-config details for each submodule.

## Submodules

After cloning, initialize submodules with:

```bash
git submodule update --init --recursive
```

## Reference Codebases

The `refs/` directory contains four IBus engine implementations to study. Each follows a similar pattern:

- **ibus-hangul**: C engine wrapping `libhangul`; straightforward single-library integration
- **ibus-anthy**: Python/GObject Introspection engine wrapping `anthy-unicode` — pinned to `bjj/ibus-anthy` @ `0962741`
- **ibus-table**: Self-contained Python engine; no external IM library dependency
- **ibus-pinyin**: C++ engine wrapping `pyzy`; most complex, supports Pinyin and Bopomofo
