# Submodules / Core Dependencies

Each IBus engine in `refs/` depends on an underlying input-method library. This file records the correct upstream repository for each.

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
