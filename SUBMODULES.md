# Submodules / Core Dependencies

Each IBus engine in `refs/` depends on an underlying input-method library. This file records the correct upstream repository for each.

One does not: `ibus-table` is self-contained Python, so there is no library to vendor and no submodule for it. Its table-driven methods are provided by an engine written inside `libpathime` instead — see the closing section.

---

## ibus-hangul → libhangul

- **Repository**: https://github.com/libhangul/libhangul
- **pkg-config name**: `libhangul >= 0.1.0`
- **Header**: `<hangul.h>`

---

## ibus-anthy → anthy-unicode

- **Reference clone**: https://github.com/bjj/ibus-anthy — pinned to `0962741856498bc9d197be0713b2fae1879c29ba` ("engine: Add behavior-on-select-candidate")
- **Upstream**: https://github.com/ibus/ibus-anthy
- **pkg-config name**: `anthy-unicode` (checked first), falls back to `anthy`
- **Header**: `<anthy/anthy.h>`
- **Notes**: The original anthy project lived on OSDN (defunct). `anthy-unicode` is the actively maintained fork shipped by Fedora/RHEL; `fujiwarat/anthy-unicode` is the canonical upstream GitHub location.
- **Optional dependency**: [Kasumi](https://osdn.net/projects/kasumi/) — GTK dictionary manager, not required for the engine itself.

---

## ibus-pinyin → pyzy (libpyzy)

- **Repository**: https://github.com/openSUSE/pyzy
- **pkg-config name**: `pyzy-1.0 >= 0.0.8`
- **Headers**: `<PyZy/InputContext.h>`, `<PyZy/Const.h>`, `<PyZy/Variant.h>`
- **Notes**: Provides the Chinese phonetic input-context engine used throughout ibus-pinyin's editor classes. Additional build deps (sqlite3, lua 5.1, boost) are standard distro packages, not custom libraries.

---

## ibus-table → (no library; implemented in libpathime)

- **Reference clone**: https://github.com/kaio/ibus-table — engine source, GSettings schema
- **Table data**: https://github.com/bjj/ibus-table-chinese — Wubi, Cangjie, Stroke5, Zhuyin, … source `.txt` tables
- **pkg-config name**: none
- **Header**: none
- **Notes**: `ibus-table` is a self-contained Python engine — the table logic and the SQLite access are the engine, not a separate library, so there is nothing to wrap. `libpathime` implements its own table engine as a peer of the three vendored libraries, against the clean-room specification in `docs/ibus-table-spec.md`, and consumes the same data (source `.txt` and compiled `.db`) so tables can be shared. Not written yet: `LIBPATHIME_WITH_TABLE` defaults OFF. The dependency it will acquire is `sqlite3`, which pyzy already requires.
