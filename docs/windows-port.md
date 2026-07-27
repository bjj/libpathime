# The Windows port

How `libpathime` and its three vendored submodules build and behave on Windows.
`BUILD.md` is what to install and type; this is what the build is doing behind
that and where the result still differs from Linux.

Verified on Windows 11 with Visual Studio 2022 (MSVC 19.44) and with
clang-cl 19 + Ninja, x64, against vcpkg's glib 2.88 and sqlite3 3.53.
Both presets produce
identical `anthy.dic` and `android.db`, and both pass the full test suite — which
is the real check that the workarounds below preserve Linux behaviour rather
than merely compiling.

## The rule

Nothing under `libhangul/`, `anthy-unicode/` or `pyzy/` is ever edited. A
vendored source that will not compile as it stands is handled by one of the
three mechanisms below, never by a patch to the submodule tree.

## 1. The compat layer

`cmake/compat/win32/` supplies replacement POSIX headers — `sys/mman.h`,
`unistd.h`, `dirent.h`, `pwd.h`, `netinet/in.h`, `sys/time.h`, `uuid/uuid.h`,
`sys/utsname.h` — placed ahead of the system include path and backed by a small
static library. `libpathime_add_win32_compat()` in `cmake/LibpathimeCompat.cmake`
attaches it to a target.

Anything that belongs in a header Windows *does* ship — `alloca`,
`strncasecmp`, POSIX record locking — is injected with `/FI win32_prelude.h`
instead, because those headers must not be shadowed.

This is also where pyzy's UUID provider comes from: the shim forwards to
`Rpcrt4`, so there is no `libuuid` to install.

## 2. Generated source variants

Where a vendored file is not valid on an MSVC-style compiler, the port compiles
a fixed *copy* produced at configure time. Editing a submodule therefore needs a
re-run of CMake for the change to reach the copy.

- `anthy-unicode/src-diclib/alloc.c` casts heap pointers through `unsigned
  long`, which is 32-bit under LLP64; the copy uses `uintptr_t`.
- `anthy-unicode/src-diclib/file_dic.c` walks paths POSIX-style and so cannot
  create `%USERPROFILE%\.config\anthy`; the copy teaches it drive letters and
  `\`. This is what lets `pathime_init_params_t::data_dir` name a multi-level
  Windows path that does not exist yet.
- `pyzy/src/` is mirrored whole, into `PYZY_EFFECTIVE_SRC_DIR`.
  `PinyinParserTable.h` uses GNU labelled-field initialisers; `String.h` is
  missing `operator<<` overloads that only LP64 made unnecessary;
  `PhraseEditor.h` forward-declares `class Config` where it is a `struct`,
  which MSVC's mangling notices; and `BopomofoContext.cc` casts a
  `const wchar_t *` to UCS-4, which is only correct where `wchar_t` is 32 bits.

Consumers of pyzy's headers must use `PYZY_EFFECTIVE_SRC_DIR` rather than
`pyzy/src`, since on Windows the mirror is what the library was actually built
from. `src/CMakeLists.txt` does this when it stages the public headers for the
adapter.

## 3. Build-time behaviour

The codegen tools get an 8 MB stack (they `alloca` in loops) and run with the
CRT in binary mode, because they `fopen(..., "w")` binary dictionaries.
Generated text inputs are written with LF for the same reason — and it is why
the tree itself must be checked out with LF endings (`core.autocrlf input`): a
dictionary source converted to CRLF would leave stray CRs in parsed tokens.

## Linkage: anthy is static, libhangul and pyzy are not

On Windows the anthy family is built **static** regardless of
`BUILD_SHARED_LIBS`. The reason is anthy's own — cross-library global data — and
is set out in `docs/anthy-mapping.md`, "Why the anthy family is built static on
Windows", along with the constraint it imposes: **nothing that links
`libpathime` may also link anthy.** That constraint is the reason
`tests/api/` reaches anthy's build-tree data through the `CONFFILE` environment
variable instead of `anthy_conf_override()`; see `docs/testing.md`.

libhangul and pyzy still produce DLLs under a shared build.

## Known runtime limitations

Runtime gaps, not build failures; none of them affect the artifacts the build
produces.

- The anthy **runtime** library still opens its private-dictionary and history
  files in the CRT's default text mode. The binary-mode fix above is
  deliberately scoped to the codegen executables, which own their process — a
  library must not change `_fmode` under its host. The only observed effect is
  that `textdict`'s newline padding is written twice as long as intended; the
  private-dictionary write/read/delete cycle round-trips correctly, and
  `ctest -R '^anthy\.dicutil$'` covers it.
- `cmake --install` leaves a second copy of `hangul.dll` in `lib/` — upstream's
  install rule uses one bare `DESTINATION`. The usable copy in `bin/` is added
  by this build.
- The installed tree does not carry pyzy's vcpkg runtime dependencies
  (`glib-2.0-0.dll` and friends). They are staged into the build tree's `bin/`
  by vcpkg's applocal step; deploying an install needs them copied alongside.
- `hangul.vendored.unittest` does not run on Windows; `docs/testing.md`
  explains why, and what covers the same ground instead.
