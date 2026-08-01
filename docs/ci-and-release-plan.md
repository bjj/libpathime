# CI, publishing, and the macOS port — the plan

What has to happen to put libpathime on GitHub with continuous integration,
published packages, and a third platform, and in what order. Steps marked
**[manual]** need a human at a browser or a terminal with credentials; the rest
is work in this repository.

This is a plan, so unlike the rest of `docs/` it is written to be consumed and
deleted. Delete each phase as it lands; what survives belongs in `BUILD.md`,
`docs/testing.md` or `THIRD-PARTY.md` instead.

## Decisions already taken

These are settled and the rest of the document assumes them:

- **The GitHub repository is public.** That decision pays for everything else —
  GitHub-hosted runners are free and unmetered on public repositories, on every
  platform including macOS.
- **The release artifacts ship all data.** Consumers who want different licence
  terms build their own with `LIBPATHIME_WITH_ANTHY=OFF` /
  `LIBPATHIME_WITH_TABLE=OFF`. See "What a release contains" below — this makes
  every artifact GPL-3, and that has to be stated on the release rather than
  implied by `LICENSE`.
- **Two binary packages per platform, and they do not overlap.**
  - a **library package**, for developers embedding libpathime: headers,
    libraries, CMake package config, `pathime-data/`. **No demo.**
  - a **demo package**, for someone who just wants to try the thing: the
    `pathime-demo` executable, the libraries it needs, `pathime-data/`. Stands
    alone, downloadable, runnable.
- **And only two — no separate runtime package.** The runtime/devel split is
  *distribution* practice: it exists so a dependency resolver can install
  `libfoo` under an application without dragging headers along. Nothing plays
  that role on a GitHub release page, and nothing installs `libpathime.so`
  system-wide — the whole layout is built around an application *bundling* the
  library beside itself. A runtime-only artifact therefore has no consumer,
  and the library package carries the runtime libraries the way every
  developer-facing C SDK release does. The component model in 4.5 keeps the
  split cheap to add later if a consumer materialises.
- **One archive per platform–architecture pair; no arch subdirectories.** The
  generated `pathime-targets.cmake` describes exactly one triplet, so a
  combined SDK with `lib/x64`-style subdirectories (SDL's devel zips are the
  precedent) needs a hand-written dispatching config on top — a second,
  unverified statement of the install layout, free to drift from the generated
  truth. Folding would deduplicate the ~25 MB arch-independent
  `pathime-data/`, but at the price of every consumer downloading every
  architecture; per-arch archives spend the bytes on our side of the wire, not
  theirs. macOS universal binaries, the platform-native fold, are blocked
  anyway: Homebrew's glib is single-arch.
- **Windows artifacts ship their external runtime DLLs; POSIX artifacts ship
  no external libraries.** On Linux and macOS, glib, sqlite3 and libuuid come
  from the system or Homebrew, are on the loader's path, and shipping our own
  copies would be a collision hazard with no benefit. On Windows there is no
  system glib — the only ABI-compatible `glib-2.0-0.dll` in existence is the
  one this build's vcpkg produced — so both packages carry the five-DLL
  closure 4.3a measured. This is a fact about Windows and pyzy, not about
  vcpkg; 4.3c has the rule and the alternatives considered.
- **The source tarball is a release artifact, not an option.** Both binary
  packages are GPL-3 as a whole (6.4), and a complete source tarball on the
  same release page is the zero-argument corresponding-source story — where
  "clone recursively, two submodules from a personal fork" leans on GPLv3
  §6d's "clear directions to a different server" and rots if a fork ever
  moves. GitHub's auto-generated tarball omits submodules and cannot serve.
  6.5 has the mechanics.
- **One `THIRD-PARTY.md`, written artifact-relative, plus shipped licence
  texts.** The summary document is restructured to say *where each component
  lands* rather than making tense-dependent claims ("not shipped in an
  install"), so the identical file drops into the repository, both binary
  packages and the source tarball. What is generated per artifact is the set
  of licence *texts*, which MIT and the LGPL/GPLs all require to accompany a
  distribution and which nothing installs today. 4.8 and 6.4 split the work.
- **Nothing ships the tests.** Not the library package, not the demo package.
  This is already true — `tests/` installs nothing — so it is an invariant to
  assert in CI, not work to do. Same for `pathime-table-compile`, which is a
  build-time tool.
- **Publishing targets developers, not distributions.** GitHub Releases, then a
  vcpkg port, then possibly Conan. No deb, rpm, or AUR.
- **Static builds are out of scope for packaging.** Every release artifact is a
  shared build. `BUILD_SHARED_LIBS=OFF` stays a supported *build* — it is tested
  and its install layout is correct — but nothing ships it. Two independent
  reasons, and either alone is sufficient: the LGPL relinking obligation attaches
  to a static artifact in a way it does not to a shared one (see 6.4), and a
  static artifact pushes the vendored archives, the external libraries and the
  C++ runtime onto the consumer's own link line, which is a support surface with
  no audience yet. A consumer who wants it builds it. This is what keeps 4.3's
  static consumer a *CI* obligation rather than a packaging one.
- **The private origin remote stops mattering.** GitHub becomes the source of truth.
- **A formatter was wanted, negotiated, and rejected** — the configuration was
  to be chosen by looking at its output on this tree rather than adopted off
  the shelf, and doing that is what settled it. Phase 5 records the
  measurements and the ruling; there is no formatting job.

## Baseline: what the build costs

Measured on a 32-core Linux box, clean out-of-tree builds, all four backends
enabled:

| Job | Wall | CPU | Result |
|---|---|---|---|
| Configure + build + all runtime data | 4.6 s | 66 s | 20 MB `anthy.dic`, 3.3 MB pyzy, 5.5 MB tables |
| `ctest -j8` | 0.25 s | 0.7 s | 37/37 pass |
| Coverage build + `pathime-test-coverage` | 7.7 s | 50 s | 88.3 % lines (3118/3532) |
| Demo build | 3.5 s | 42 s | — |

A GitHub-hosted Linux runner has 4 vCPU, so a full build-and-test job lands
around 20–40 seconds. **No build caching, ccache, or artifact reuse is worth
configuring on Linux or macOS.** Be generous with the matrix instead.

The one expensive job is Windows, where vcpkg has to supply glib and sqlite3 for
pyzy. Built from source that is 15–30 minutes; with binary caching it is under a
minute. Configure the cache before adding the job, or it becomes a tax on every
push.

## macOS: yes, and it is cheaper than it looks

The question was whether macOS support is reachable without owning a Mac. It is,
on both counts — CI can carry it, and there is a way to get an interactive shell
today.

### Getting a macOS shell right now, for free

A public repository plus a workflow using an SSH-session action
(`mxschmitt/action-tmate` or `owenthereal/action-upterm`) gives a real macOS
runner with an SSH address printed in the job log, held open until the job
timeout of six hours. That is the fastest path to iterating on the port from
home, and on a public repository it costs nothing. Push a throwaway branch with
a workflow that installs the dependencies and then opens the session, and work
in it exactly as on a local machine.

Rented Macs are the alternative and are worth knowing about but not worth
starting with: Scaleway's Mac minis run about €0.11–0.22/hour and MacStadium's
start around $119/month, but **Apple's licensing imposes a 24-hour minimum
lease**, so the real floor is a few euros per session rather than cents.

### What actually has to change in the tree

The port is small and the reason is visible in the source: the tree is a clean
`WIN32` / not-`WIN32` split, with every platform conditional in
`cmake/ports/`, `cmake/LibpathimeOptions.cmake` and `cmake/LibpathimeRuntimeData.cmake`
testing `WIN32` and nothing else. There is no `APPLE` branch anywhere, and the
not-`WIN32` side is generic POSIX rather than Linux-specific. Four things need
attention:

1. **`src/module_path.cc:119` — the `/proc/self/exe` fallback.** `dladdr()`
   works on macOS and answers for a dylib, so a **shared build needs no change
   at all**. The fallback exists for static builds, where there is no separate
   object for `dladdr` to name, and `/proc` does not exist on macOS. The
   replacement is `_NSGetExecutablePath()` from `<mach-o/dyld.h>`, which takes a
   buffer and an in/out length and wants `realpath()` afterwards because it can
   return a non-canonical path. This is the only C++ source change the port
   needs. It is also, per `docs/testing.md`, the least-covered code in the
   tree — so it wants the static-build CI job of Phase 1 pointed at it.

2. **The uuid probe, in two places** — `cmake/LibpathimeDependencies.cmake:82-91`
   and `cmake/ports/pyzy/CMakeLists.txt:80`. Both ask pkg-config for `uuid`, and
   the second does so `REQUIRED`, making it a hard configure error rather than a
   gate. macOS has no `uuid.pc`, but it does have `<uuid/uuid.h>` and
   `uuid_generate`/`uuid_unparse_lower` in libSystem — which is exactly what
   pyzy's `engines/pyzy/src/Util.h:32` includes and calls. **The code compiles as
   written; only the detection fails.** The fix is an `APPLE` arm that does a
   `check_include_file(uuid/uuid.h ...)` and links nothing, structurally the same
   as the existing Windows arm that maps to the Rpcrt4 shim.

3. **`LIBPATHIME_TABLE_COVERAGE` has no macOS default.**
   `cmake/LibpathimeOptions.cmake:106` picks `windows` on Windows and `noto`
   everywhere else, so macOS silently inherits the Noto map. macOS ships PingFang
   and Hiragino rather than Noto, so the honest options are to generate a third
   map with `tools/generate-coverage.py` against the system faces, or to decide
   `noto` is close enough and say so where the default is set. Not a blocker for
   a first green build, but it is a real decision and should not be reached by
   accident — the whole point of `BUILD.md`'s "Glyph coverage" section is that
   the map is a recorded choice rather than a property of the machine.

4. ~~**Install-tree RPATH.**~~ Done with Phase 4:
   `libpathime_set_install_rpath()` in `cmake/LibpathimeInstall.cmake` picks
   `@loader_path` on `APPLE` and `$ORIGIN` elsewhere. The `APPLE` arm is written
   but has never run — it is one of the things the first macOS install job
   verifies.

Everything else is expected to carry over: sqlite3 comes from the macOS SDK,
glib and pkg-config from Homebrew, and libhangul's `glob.h` check succeeds.
anthy's build-time dictionary codegen and pyzy's generated tables have no
obvious platform dependency, but neither has ever been run there — **the CI job
is the verification, not this paragraph.**

### Runner labels

`macos-latest` moves to `macos-26` between June and July 2026, so pin the label
explicitly rather than inheriting a silent platform change. `macos-15` and
`macos-26` are Apple Silicon; `macos-15-intel` and `macos-26-intel` are x86-64.
Start with one arm64 label and add Intel only if a consumer asks — the
`arm64`/`x86_64` split is the more interesting axis than the OS version, and
neither has been exercised.

---

# Phase 0 — Get onto GitHub

Nothing else can start until this is done, and it is the phase with the most
**[manual]** in it.

**[manual] 0.1 — Push the submodule forks first.** This is the step whose
omission produces a confusing CI failure later. Two of the five submodules are
your own forks, each pinned to a `libpathime` branch:

| Submodule | Remote | Pinned at |
|---|---|---|
| `engines/libhangul` | `libhangul/libhangul` (upstream) | `a34aef7` |
| `engines/anthy-unicode` | `bjj/anthy-unicode` (fork, branch `libpathime`) | `b3f0bd6` |
| `engines/pyzy` | `bjj/pyzy` (fork, branch `libpathime`) | `82afe13` |
| `engines/ibus-table-chinese` | `mike-fabian/ibus-table-chinese` (upstream) | `d261412` (1.8.14) |
| `demo/cpp-terminal` | `jupyter-xeus/cpp-terminal` (upstream) | `c64ca6f` |

All five remotes are already public and reachable. What is not guaranteed is
that the *exact pinned commits* are pushed — a commit made locally on a
`libpathime` branch and never pushed will fail `git submodule update` on a
runner with a message about a missing object. For each fork:

```bash
cd engines/anthy-unicode && git push origin libpathime && cd -
cd engines/pyzy         && git push origin libpathime && cd -
```

**[manual] 0.2 — Create the repository and push.**

```bash
gh repo create bjj/libpathime --public --source=. --remote=github --push
```

Keep the private remote as `origin` for now; nothing forces a rename, and having
two remotes costs nothing.

**[manual] 0.3 — Verify a cold clone, before writing any workflow.** This is
five minutes that saves an hour of debugging a runner:

```bash
cd $(mktemp -d)
git clone --recurse-submodules https://github.com/bjj/libpathime
cd libpathime && cmake -S . -B b -G Ninja -DLIBPATHIME_BUILD_TESTS=ON \
                       -DLIBPATHIME_REQUIRE_BACKENDS=ON
cmake --build b && ctest --test-dir b --output-on-failure
```

If this passes, Phase 1 is mechanical. If it fails, it fails here where you can
see it, rather than in a runner log.

**[manual] 0.4 — Repository settings.** In the web UI:

- **Settings → Actions → General → Workflow permissions**: set to *Read
  repository contents and packages permissions*. Grant more per-job with an
  explicit `permissions:` block. This is the default-deny posture and it is much
  easier to adopt now than to retrofit.
- **Settings → Code security**: enable Dependabot alerts and Dependabot security
  updates.
- **Do not add branch protection yet.** Requiring status checks that do not exist
  is the classic first-timer trap: it blocks every merge, including the one that
  would add the checks. Branch protection is step 1.5, after CI is green.

---

# Phase 1 — Core CI

One workflow, `.github/workflows/ci.yml`, on `push` and `pull_request`, with a
`concurrency` group keyed on the ref so superseded runs cancel themselves.

Every job calls a **preset** rather than a hand-written `cmake` line — the
presets are the contract, and a workflow that drifts from them stops testing
what developers run. Every job also sets `-DLIBPATHIME_REQUIRE_BACKENDS=ON`, the
option `BUILD.md` documents as existing for exactly this: without it a runner
that quietly lost `libglib2.0-dev` reports green while testing three backends.

Matrix:

| Job | Configuration | What it is for |
|---|---|---|
| `linux-gcc-release` | Release, shared | the baseline |
| `linux-gcc-debug` | Debug | assertions |
| `linux-clang-release` | Clang | a second front end sees different things |
| `linux-static` | `BUILD_SHARED_LIBS=OFF` | exercises `PATHIME_STATIC` and the `module_path.cc` fallback that coverage cannot reach |
| `linux-asan-ubsan` | per `docs/testing.md` | documented clean today |
| `linux-uninit` | `-ftrivial-auto-var-init=pattern` | guards the `pathime_init_params_t` class of bug |
| `windows-ninja` | `windows-ninja` preset | clang-cl; the Windows job to lean on |
| `windows-msvc` | `windows-msvc` preset | keeps the Visual Studio generator honest; serial, and needs vcpkg binary caching |

Three details that are easy to get wrong:

- **The sanitizer job needs `ASAN_OPTIONS=detect_leaks=0` at *build* time and
  not at test time.** anthy's dictionary codegen tools exit without freeing, as
  build-time programs reasonably do, and LeakSanitizer fails the build over it.
  `docs/testing.md` has the reason.
- **`anthy.vendor.main` is not leak-clean** and is vendored code testing vendored
  code. Either exclude it in the sanitizer job or accept the suppression.
- **`windows-ninja` is the Windows job to lean on, and the one to reach for when
  a Windows job needs to be added or made faster.** `windows-msvc` builds
  serially — `docs/windows-port.md`, "Known build limitation", has the reason —
  so it is there to keep the Visual Studio generator honest, not to be the
  Windows workhorse. Anything wanting wall-clock time, or wanting tests and the
  demo in one job, belongs on Ninja.

Verified by hand on Windows 2026-07-29, so the matrix above is describing
something known to pass rather than something hoped for: both presets, both link
modes, 39/39 suites each. Two Windows facts the matrix should not have to
rediscover — `LIBPATHIME_REQUIRE_BACKENDS=ON` is satisfiable on a vcpkg host
with `glib` and `sqlite3` only, and the fully static vcpkg triplet
(`x64-windows-static-md`) has **never been configured**, because no packages are
built for it. A `BUILD_SHARED_LIBS=OFF` Windows job therefore means the dynamic
triplet, and should say so.

**1.1 — Pin CMake.** Configuring with CMake 4.2.3 already emits a deprecation
warning from `engines/libhangul/hangul/CMakeLists.txt:18`, which asks for
compatibility with CMake < 3.10. That is a warning now and an error in a future
release, and GitHub runner images track CMake closely — so this breaks on a
runner-image refresh, not on a commit. Pin the CMake version in CI
(`jwlawson/actions-setup-cmake`) so an image bump cannot break the build
overnight. libhangul is unmodified upstream and should stay that way, so if the
real fix is needed it is `CMAKE_POLICY_VERSION_MINIMUM` in our own build.

**1.2 — Pin every third-party action to a commit SHA**, not a tag. Tags are
mutable.

**[manual] 1.3 — Turn on branch protection, once the checks are green.**
Settings → Rules → Rulesets → New branch ruleset, targeting the default branch:
require a pull request, and require the status checks that now exist and pass.

---

# Phase 2 — macOS

**The goal of this phase is a green macOS build and `ctest` run, landed as one
reviewed change on the public repository.** Deliberately after Phase 1, so that
when the macOS job goes red you are looking at one variable rather than two.

## 2.0 — Pick the iteration route first

The port is small but it is guaranteed to take several rounds, because none of
it has ever been compiled on the platform. Where those rounds happen is the
decision, and it is worth taking before starting rather than during.

**Route A — public repository, iterate in the open.** Push the CI, watch it go
red, push again. It works, it needs no setup, and the whole world can watch you
fail to spell `_NSGetExecutablePath`. Slow feedback (every round is a push, a
queue and a runner boot) and a commit history full of "fix macOS take 7" unless
the branch is squashed at the end.

**Route B — interactive session on a runner.** A tmate step gives a real macOS
shell over SSH, held to the six-hour job limit, and turns the loop from
push-and-wait into an ordinary terminal. Two caveats. On a *public* repository
that SSH address is in a public log, so the session is world-reachable while it
is open — which is the exposure this route was meant to avoid. And it depends on
outbound SSH, which a locked-down development network may not permit. Verify
before planning around it.

**Route C — private scratch repository, driven over HTTPS.** Push a
`libpathime-macos-spike` private repo, iterate there with ordinary CI pushes, and
land the finished diff as one clean PR on the public repo. This is the
recommended route, and it is the one neither of the obvious two suggests:

- **It is private**, so nothing is hanging out.
- **It needs no SSH.** Everything happens over HTTPS to `github.com`, so the
  whole loop — push, poll `gh run watch`, read the log, fix, push again — runs
  from an ordinary terminal, even on a network that only passes HTTP and HTTPS.
- **It is affordable.** Private repositories bill against the Free plan's 2,000
  Linux-equivalent minutes per month, and macOS runs at a 10× multiplier, so the
  allowance is 200 macOS-minutes. A libpathime macOS job is a couple of minutes
  including `brew install`, so that is dozens of rounds a month — ample for a
  spike, and it ends when the spike does.
- Public submodules work fine from a private parent repository.

Route B remains the right tool for one specific thing: a problem that needs
poking at the machine rather than re-running a build — a linker error whose
cause is in the SDK layout, say. Reach for it then, from the private repo, where
the log is not public.

**2.1 — Do the port.** In whichever route, work through the four items in "What
actually has to change" above until a configure, build and `ctest` pass.

**2.2 — Land the source changes**: the `_NSGetExecutablePath` fallback in
`module_path.cc`, and the `APPLE` arm in both uuid probes.

**2.3 — Decide the glyph-coverage map** for macOS and record the reasoning where
the default is set, alongside the existing Windows/Noto note.

**2.4 — Add `macos-26` (arm64) to the CI matrix**, with the same
`REQUIRE_BACKENDS=ON` posture. Add a static-build macOS job too — that is the
configuration the `module_path.cc` change exists for, and without it the change
is untested.

**2.5 — Update the documentation.** `README.md` and `BUILD.md` both say Linux
and Windows; `BUILD.md` gains a Homebrew dependency line beside the apt and
vcpkg ones. `docs/windows-port.md` stays as it is — the macOS port is not a
compat layer and does not belong in it.

---

# Phase 3 — Quality signals

Cheap, and each one is a thing you would otherwise have to remember to do.

- **`coverage.yml`** — the `linux-coverage` preset. `gcovr` already writes
  Cobertura `coverage.xml` for this purpose. Upload to Codecov for a PR comment
  with the delta. Report **branch** coverage as well as lines: at 88.3 % lines
  against roughly 57 % branches, the line figure is the less honest of the two,
  and `docs/testing.md` says so.
- **`codeql.yml`** — CodeQL for `c-cpp` with a manual build step. Free on public
  repositories and genuinely good at C memory bugs.
- **`.github/dependabot.yml`** — for `github-actions` *and* `gitsubmodule`. The
  latter opens a PR when libhangul or cpp-terminal move upstream, which is
  exactly the "a bump is a rebase" workflow the project already intends.
- **A reproducibility job.** `BUILD.md` claims two builds of the same commit with
  the same map produce byte-identical tables, checked across MSVC and clang-cl.
  Build the tables twice and `cmp` them. That claim is load-bearing for the
  checked-in coverage maps and nothing currently enforces it. Re-verified by hand
  2026-07-29 — all five default tables *and* `anthy.dic` hash identically between
  the `windows-msvc` and `windows-ninja` installs, so the job is codifying a
  measurement rather than testing a hope. `anthy.dic` matching is not a documented
  claim and should not become one on this evidence alone; it is one data point,
  not a promise about anthy's codegen.

---

# Phase 4 — Make the library consumable

This is the real work, and every publishing channel depends on it.

**4.1 and 4.2 have landed.** `cmake/LibpathimeInstall.cmake` owns the installed
layout: the export set and the generated `pathime-config.cmake`, a `pathime.pc`,
`SOVERSION` on the `pathime` target, and per-target `INSTALL_RPATH` (`$ORIGIN` /
`@loader_path`) rather than the global `CMAKE_INSTALL_RPATH` this plan assumed,
so a tree pulling libpathime in with `add_subdirectory` keeps its own policy.
The vendored libraries install into a private `lib/pathime/` and their headers
are not installed at all; `BUILD.md`, "What gets produced", has the reasoning.
Item 4 of the macOS list is done with them.

- **4.3** — Add a CI job that installs to a prefix and builds a small standalone
  consumer, in all four combinations: `find_package(pathime)` and `pkg-config`,
  each against a shared and a static install. All four were verified by hand on
  Linux when the install landed, and again on **Windows/MSVC 2026-07-29**, where
  all four also *ran* — but nothing enforces them. The suites under
  `tests/` link the build tree and structurally cannot catch a broken install
  layout. Two specific things for the job to pin: a static **C** consumer needs
  `enable_language(CXX)` or the link fails on `std::` symbols, and a static
  consumer's `find_package` re-finds SQLite3, GLib and libuuid, so a runner
  missing one of those `-dev` packages must fail loudly rather than skip.

  **The consumer must be run, not only linked**, and it must call
  `pathime_init()` with `resource_dir` NULL and then assert
  `pathime_has_engine()` for all five ids. That one assertion is what tests the
  data half of the layout: `pathime_has_engine()` is false for a backend whose
  data is missing, so a consumer that links and exits proves nothing about
  whether `pathime-data/` landed where the module resolves it. It is also how the
  static rule gets tested at all — a static consumer resolves the directory from
  its *own* executable, so the job has to place the binary beside the installed
  `bin/pathime-data` and would silently pass a wrong `LIBPATHIME_INSTALL_DATADIR`
  otherwise.

  Two MSVC-specific notes for whoever writes the job. `pkg-config` output needs
  `-l`/`-L` translated to `.lib` and `/LIBPATH:` for a `cl`-family driver, and
  the translation must be case-sensitive or `-lpathime` matches `-L`. And
  `BUILD.md`'s "without `--static` the link fails on every hangul, anthy, pyzy
  **and `std::`** symbol" is Unix-only: MSVC auto-links the C++ runtime through
  `#pragma comment(lib)`, so only the backend symbols fail there, and
  `Libs.private` correctly names no C++ runtime on that toolchain.

- **4.3a — The shared Windows install is not loadable, and this blocks 6.3.**
  Found 2026-07-29; resolved 2026-07-31 by 4.3c's rule, now implemented in
  `cmake/LibpathimeInstall.cmake`. A consumer against a shared Windows install
  died at load with
  `0xC0000135 STATUS_DLL_NOT_FOUND`. `pathime.dll` imports `sqlite3.dll`
  directly and `pyzy-1.0.dll` imports `sqlite3.dll` and `glib-2.0-0.dll`, but
  none of the five vcpkg runtime DLLs the build depends on
  (`sqlite3`, `glib-2.0-0`, `iconv-2`, `intl-8`, `pcre2-8`) was installed. The
  build tree had them only because vcpkg's applocal step stages them beside the
  built binaries; that step never touches the install tree.

  This is long-standing rather than new — no such rule has ever existed — and it
  is invisible on Linux, where the same libraries come from the system and are on
  the loader's path. It surfaces now because it is exactly what a Windows library
  artifact would ship. Copying those five files in by hand makes the install pass
  every consumer check, so the gap is the packaging rule and nothing more.
  **4.3c is that rule.**

  It does not arise for a static install in the same way: there the external
  libraries are the consumer's own declared `Requires.private` dependencies and
  theirs to deploy — and per "Decisions already taken", no static artifact ships
  regardless.

- **4.3b — `pathime.pc` is relocatable; what remains is the CI assertion.**
  The `.pc` now derives its prefix from `${pcfiledir}` whenever the
  GNUInstallDirs layout is prefix-relative (any absolute directory pins the
  file to configure-time paths, wholesale); `BUILD.md` states the resulting
  rule — the installed tree self-locates and can be moved, `--prefix`-ed or
  unpacked anywhere. Verified by hand on Linux: all four 4.3 combinations
  (`find_package`/`pkg-config` × shared/static) built *and ran* the
  five-engine consumer against a prefix moved after install, and a shared
  install relocated with `cmake --install --prefix` to a never-configured
  prefix did the same. What remains is 4.3's job asserting it: the consumer
  must **consume from a prefix that was moved after install** — installing and
  consuming at the same prefix structurally cannot catch this class of bug,
  for either the `.pc` or the CMake config.

- **4.3c — The Windows runtime-DLL install rule, resolving 4.3a.** Implemented
  2026-07-31 in `cmake/LibpathimeInstall.cmake`, on `WIN32` shared builds
  only: every installed runtime target joins a `RUNTIME_DEPENDENCY_SET`, and
  an `install(RUNTIME_DEPENDENCY_SET ...)` whose filters state the actual
  policy — include what resolves from the vcpkg installed tree, exclude the OS
  (`PRE_EXCLUDE_REGEXES` for `api-ms-`/`ext-ms-`, `POST_EXCLUDE_REGEXES` for
  system32) — installs the closure. Not a hand-copied list of the five current
  names: that list goes stale silently on a vcpkg baseline bump — pcre2 is
  exactly the kind of transitive dependency that changes — where the
  dependency-set form encodes the rule and fails loudly. Verified by hand on
  Windows 2026-07-31, under both presets: the closure installs exactly the
  five DLLs, byte-identical to the vcpkg tree, and `pathime.dll` loads from
  the installed `bin/` in a process whose PATH names only the system
  directories — under plain `LoadLibrary` and under
  `LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`
  (Python ctypes' default), with an exported function called through the
  handle. Removing `glib-2.0-0.dll` from the tree reproduces the original
  error, which is what makes the check sensitive. What remains of 4.3a is
  4.3's CI job asserting the same from a clean runner.

  The system32 exclusion also sweeps in the MSVC redistributable runtime —
  `pathime.dll` imports `msvcp140.dll`, `vcruntime140.dll` and
  `vcruntime140_1.dll`, which are not in-box Windows (the ucrt is; these are
  not). **The redist is a prerequisite, not a shipped component**: a consumer
  of an MSVC-built library is expected to have it. Note that 4.3's CI job
  cannot demonstrate otherwise on a hosted runner, which has Visual Studio and
  therefore the redist.

  On whether the DLLs are vcpkg's fault, since the question comes up: no. They
  follow from Windows having no system-wide home for third-party libraries and
  from pyzy requiring glib; no dependency source changes either fact.
  Considered and rejected:

  - **MSYS2/MinGW glib** — a different toolchain family. The ucrt64 flavour
    makes CRT mixing technically workable for a C library, but it adds the GCC
    runtime DLLs to the closure, comes from a rolling repository that cannot be
    pinned in a manifest, and puts a second package ecosystem in CI. More DLLs
    and less reproducibility, not fewer.
  - **gvsbuild** — the MSVC-native GTK-stack builder. Same output (glib DLLs to
    ship), another tool to pin, no `find_package` integration; it earns its
    keep when the whole GTK stack is needed, which this build does not.
  - **Conan** — vcpkg's peer, with no advantage when vcpkg is already
    load-bearing for the Windows CI.
  - **Static glib** (`x64-windows-static-md`) — the one option that removes
    files: glib, iconv, intl and pcre2 fold into `pyzy-1.0.dll`, which is fine
    for licence purposes (LGPL code inside an LGPL DLL that remains a separate
    replaceable file). Costs: the triplet has never been configured (Phase 1
    notes no packages exist for it), glib's static Windows build is its
    historically fragile corner, and Windows would diverge in shape from every
    other platform. Worth revisiting only if the DLL count itself becomes a
    problem; it is not a v1 blocker.

  What vcpkg keeps: MSVC-ABI binaries matching the toolchain, manifest
  pinning, binary caching, and an applocal step that is what surfaced the
  exact five-DLL closure in the first place. Shipping the DLLs beside the
  binaries is ordinary Windows practice — every glib-using application on
  Windows does exactly this. One consequence to carry forward: the five DLLs
  become **shipped components**, so they need `THIRD-PARTY.md` rows and
  licence texts (4.8, 6.4) — glib, libiconv and libintl are LGPL, pcre2 is
  BSD-3-Clause; confirm the exact versions from vcpkg's per-port copyright
  files when writing the entries.

## The two packages

The library package needs little beyond 4.3b and 4.3c, because the install
rules already describe it. `tests/`, `tools/` and `demo/` contain no
`install()` at all, so `cmake --install` today produces exactly the developer
package and nothing else: headers, libraries, `pathime-data/`. The demo package
is the one that needs building, and it needs a functional change as well as a
packaging one.

- **4.4 — Give the demo a real user-data directory.** `demo/CMakeLists.txt:159`
  bakes `${CMAKE_CURRENT_BINARY_DIR}/data` in as `PATHIME_DEMO_DATA_DIR`, and
  `demo/src/main.cc:142` uses it as the default place to keep what the engines
  learn. That is exactly right for a build-tree demo — it is the same isolation
  the api tests get, and it keeps a demo run out of the developer's real
  `~/.config/anthy`. It is wrong for a *shipped* demo, which would default to an
  absolute path on the machine that built it. A downloaded demo needs the
  platform's per-user location instead (`$XDG_DATA_HOME` or `~/.local/share`,
  `~/Library/Application Support`, `%LOCALAPPDATA%`), with the build-tree path
  kept for an uninstalled build and `--data-dir` still overriding both. **This is
  a prerequisite for shipping the demo, not a nicety** — without it the first
  thing a new user meets is a write failure. The shipped demo must also
  *create* the directory on first run — `%LOCALAPPDATA%\pathime` does not
  exist until something makes it.
- **4.5 — Add component-scoped install rules, and know the CPack wrinkle
  before writing them.** Under `CPACK_COMPONENTS_GROUPING IGNORE` a component
  lands in exactly one archive, and a component can belong to only one group —
  so "`pathime-data/` belongs to both packages" cannot be expressed as one
  component per archive. Four components, and two `cpack` invocations that
  select them:

  - `runtime` — `libpathime`, the vendored libraries, and on Windows the
    4.3c DLL closure. The `.so` namelink stays out via
    `NAMELINK_COMPONENT devel` on the `install(TARGETS pathime LIBRARY ...)`
    rule, so a demo archive carries the SONAME chain but no dev symlink.
  - `devel` — headers, the namelink, `lib/cmake/pathime/`, `pathime.pc`.
  - `data` — `pathime-data/`.
  - `demo` — `pathime-demo` and its licence additions (4.8).

  Then the release job runs `cpack` twice with
  `CPACK_ARCHIVE_COMPONENT_INSTALL ON` and grouping `ALL_COMPONENTS_IN_ONE`:
  once with `-D CPACK_COMPONENTS_ALL="runtime;devel;data"` for the library
  package and once with `-D CPACK_COMPONENTS_ALL="runtime;demo;data"` for the
  demo package. The demo executable also needs what nothing has needed before —
  an install RPATH of its own, `$ORIGIN/../lib` plus `$ORIGIN/../lib/pathime`,
  which is one call to the existing `libpathime_set_install_rpath()`.
- **4.6 — Build cpp-terminal static in the demo package.** It is already forced
  static on Windows for a linkage reason `demo/CMakeLists.txt` documents at
  length. Elsewhere it follows `BUILD_SHARED_LIBS` and would make the demo
  package carry an extra shared object for no benefit. cpp-terminal is MIT, so
  static linking costs nothing legally — unlike the three LGPL backends, which
  stay shared.
- **4.7 — Assert that nothing ships the tests.** Add a CI step that configures
  with `LIBPATHIME_BUILD_TESTS=ON` *and* `LIBPATHIME_BUILD_DEMO=ON`, installs,
  and fails if any test executable or `pathime-table-compile` appears in the
  install tree. This is true today and free to keep true; it is the kind of thing
  that silently stops being true the first time someone adds an `install()` to
  get a binary somewhere convenient.
- **4.8 — Install the licence texts.** MIT and the LGPL/GPLs all require the
  licence text to accompany a distribution, and today an install contains
  none — the texts live in submodule `COPYING` files that a binary archive
  does not include. Install `LICENSE`, `THIRD-PARTY.md` and each shipped
  component's licence text into `share/doc/pathime/licenses/`, sourced from
  the submodules at build time and component-scoped so the set stays honest by
  construction: cpp-terminal's MIT text travels with `COMPONENT demo`, the
  glib/iconv/intl/pcre2 texts (from vcpkg's per-port `copyright` files) only
  on Windows with the DLLs they cover, and a `LIBPATHIME_WITH_ANTHY=OFF` build
  drops the GPL-2 text along with the dictionary.

---

# Phase 5 — The formatter: measured and rejected (2026-07-31)

**There is no formatting job and no `.clang-format`.** This section is kept
because the measurement is the argument, and without it the idea returns.

The plan was to negotiate a configuration by looking at its output on this tree
rather than adopting one off the shelf, on the rule that **a config whose diff
is enormous is the wrong config, not a mandate to reformat**. Eleven candidates
were measured against the tree with clang-format 19. The floor is 18% of every
non-vendored, non-generated line — 6,786 of 36,890 — and the rule decides it.

### What the style measures as, and why it resists

Spaces only, no tabs anywhere. Stroustrup braces. Pointers bind right. Trailing
comments are two spaces out in **122 of 122** cases; short `if (x) stmt;`
appears 129 times; nested preprocessor is indented. Those all map onto
configuration exactly and cost nothing.

What does not map is the column behaviour. The 95th-percentile line is 79
characters, but roughly 1,400 lines run 81–90, and they are deliberate — a
`ColumnLimit` of 80 rewraps every one of them, while 100 goes the other way and
*rejoins* lines the author broke on purpose. 90 is the best of the three and
still leaves the bulk of the diff:

| Candidate | Changed lines |
|---|---|
| 80 columns | 9,780 |
| 90 columns | 6,902 |
| 100 columns | 7,737 |
| 90 + `ReflowComments: false` | 6,860 |
| ↑ + `IndentPPDirectives`, `AlignEscapedNewlines: Left` | **6,786** |
| ↑ + `AlignConsecutive*` | 7,986 |
| ↑ + `BinPack{Arguments,Parameters}: false` | 7,452 |

The last two matter: settings that look like they would *preserve* hand
alignment make it worse, because they align where the author did not.

### The two regressions that settle it

The remainder is not a set of improvements waiting to be accepted. In the
public header, the status enum — an aligned table with aligned doc comments —
becomes:

```c
    PATHIME_ERROR_INVALID_ARGUMENT =
        1, /**< NULL handle, bad index, bad UTF-8, bad struct_size. */
    PATHIME_ERROR_UNKNOWN_ENGINE = 2, /**< Engine not available in this library. */
```

And `src/engines/anthy/romaji.cc`'s romaji table collapses from **286 lines,
one row per line, to 73 packed lines**. No setting prevents it —
`BinPackArguments: false` does not apply to braced init lists — so the only
remedy is `// clang-format off`, and the diff is too diffuse for that to be
cheap: the top ten files are only 48% of it.

The style measures as coherent because it is coherent. What clang-format wants
to change is mostly deliberate hand alignment.

### What the exercise found that outlives it

- **Generated headers must be excluded from any whole-tree text operation.**
  `variants_data.h`, `coverage_data_noto.h` and `coverage_data_windows.h` were
  **64% of the raw diff** on their own. Reformatting them would mean the
  checked-in file no longer matches what `tools/generate-*.py` produces, which
  is exactly the property Phase 3's reproducibility job exists to enforce. The
  exclusion that was written down covered only the vendored trees.
- **The vendored trees stay excluded** regardless — `engines/` and
  `demo/cpp-terminal` are submodules, and a reformat is not a fix.
- **`.git-blame-ignore-revs` is now moot**, since nothing reformats the tree.

If this is ever reopened, the honest form is a check over **new files only**, so
the tree stays as written and only drift is caught. The measurements above are
the thing to re-run first; they are cheap.

---

# Phase 6 — Releasing

**6.1 — Decide the version and tag policy.** The project is at `0.1.0` in
`CMakeLists.txt:18` and has never released. Note the tension with the house
rule that the library is unreleased and carries no dated
changelog: releasing is precisely what ends that, and a `CHANGELOG.md` becomes
correct rather than forbidden. Decide that deliberately.

**6.2 — `release.yml` on `v*` tags.** Build both packages for every supported
platform–architecture pair, attach them, generate notes. Consider
`actions/attest-build-provenance`, which is nearly free on GitHub and
increasingly expected of a project that ships binaries.

The supported set for a first release, each one matrix line on a free hosted
runner: **linux-x86_64**, **linux-aarch64** (`ubuntu-24.04-arm` — free on
public repositories), **windows-x64**, **macos-arm64**. Windows arm64
(`windows-11-arm`) and macOS Intel wait for a consumer to ask.

Two Linux-only decisions to take deliberately rather than inherit:

- **The glibc baseline.** A binary built on `ubuntu-latest` links the runner's
  glibc and libstdc++ (≥ 2.39 on 24.04) and will not run on anything older.
  Build the release artifacts on the oldest supported runner image — or a
  manylinux-style container if that proves too new — and **state the floor in
  the release notes**. Silence here is how "doesn't run on Debian stable"
  becomes the first issue filed.
- The external libraries (glib, sqlite3, libuuid) are runtime dependencies the
  release notes must name, since the archives deliberately do not carry them —
  one apt/dnf line, the same one `BUILD.md` already prints.

**6.3 — What a release contains.** Two binary artifacts per
platform–architecture pair, named
`libpathime-<ver>-<os>-<arch>.tar.gz` / `.zip` and
`pathime-demo-<ver>-<os>-<arch>.tar.gz` / `.zip`, plus the 6.5 source tarball.
Neither binary artifact holds the tests or `pathime-table-compile`; the demo
package holds no headers or CMake config; the library package holds no demo.
Spelled out, so the release job's output can be checked against something —
these trees follow from the existing install rules plus 4.3c, 4.5 and 4.8:

**The library package, Linux** (macOS is identical in shape, with
`libpathime.0.dylib`, an `@loader_path` RPATH, and Homebrew supplying glib):

```
libpathime-0.1.0-linux-x86_64/
├── include/pathime/
│   ├── pathime.h
│   └── config.h                       # generated; records this build's backends
├── lib/
│   ├── libpathime.so → libpathime.so.0          # namelink — component devel
│   ├── libpathime.so.0 → libpathime.so.0.1.0    # SONAME
│   ├── libpathime.so.0.1.0
│   ├── pathime/                       # private; reached only via $ORIGIN/pathime
│   │   ├── libhangul.so.1            (chains to .so.1.1.0)
│   │   ├── libanthy-unicode.so.0     (+ libanthydic-unicode.so.0,
│   │   │                                libanthyinput-unicode.so.0)
│   │   └── libpyzy-1.0.so.1
│   ├── pathime-data/
│   │   ├── anthy/anthy.dic            # ~20 MB, GPL-2
│   │   ├── pyzy/main.db               # LGPL-2.1
│   │   ├── pyzy/phrases.txt
│   │   └── table/{cangjie5,quick5,stroke5,wubi-jidian86,zhuyin}.db  # GPL-3
│   ├── cmake/pathime/
│   │   ├── pathime-config.cmake
│   │   ├── pathime-config-version.cmake
│   │   ├── pathime-targets.cmake
│   │   └── pathime-targets-release.cmake
│   └── pkgconfig/pathime.pc           # relocatable per 4.3b
└── share/doc/pathime/
    ├── LICENSE                        # libpathime's MIT
    ├── THIRD-PARTY.md
    └── licenses/                      # per 4.8: LGPL-2.1, GPL-2, GPL-3, …
```

**The library package, Windows.** Same headers, data, CMake config and docs;
the differences are where binaries live (no RPATH, so everything a DLL loads
sits beside it in `bin/`) and the 4.3c closure:

```
libpathime-0.1.0-windows-x64/
├── include/pathime/{pathime.h, config.h}
├── bin/
│   ├── pathime.dll
│   ├── hangul.dll                     # vendored; one anthy DLL on Windows,
│   ├── anthy-unicode.dll              #   not the three .so of the ELF build
│   ├── pyzy-1.0.dll
│   ├── sqlite3.dll                    # ── the vcpkg runtime closure (4.3c);
│   ├── glib-2.0-0.dll                 #    exact set is whatever the rule
│   ├── iconv-2.dll                    #    resolves, these five today
│   ├── intl-8.dll
│   ├── pcre2-8.dll
│   └── pathime-data/…                 # same contents as Linux
├── lib/
│   ├── pathime.lib                    # the one import library a consumer links
│   ├── pathime/{hangul.lib, anthy-unicode.lib, pyzy-1.0.lib}   # private
│   ├── cmake/pathime/…
│   └── pkgconfig/pathime.pc
└── share/doc/pathime/…                # + glib/iconv/intl/pcre2 licence texts
```

**The demo package, Linux** (macOS again identical in shape). The `runtime`
and `data` components verbatim, `devel` absent — note no `.so` namelink, no
headers, no CMake config:

```
pathime-demo-0.1.0-linux-x86_64/
├── bin/
│   └── pathime-demo                   # RPATH $ORIGIN/../lib:$ORIGIN/../lib/pathime;
│                                      #   cpp-terminal statically inside (4.6)
├── lib/
│   ├── libpathime.so.0 → libpathime.so.0.1.0
│   ├── libpathime.so.0.1.0
│   ├── pathime/…                      # the same five vendored libraries
│   └── pathime-data/…
└── share/doc/pathime/…                # + cpp-terminal's MIT text
```

**The demo package, Windows.** Everything in `bin/`, because the directory of
the loading module is the Windows search rule; a user unzips and runs
`bin\pathime-demo.exe`:

```
pathime-demo-0.1.0-windows-x64/
├── bin/
│   ├── pathime-demo.exe
│   ├── pathime.dll, hangul.dll, anthy-unicode.dll, pyzy-1.0.dll
│   ├── sqlite3.dll, glib-2.0-0.dll, iconv-2.dll, intl-8.dll, pcre2-8.dll
│   └── pathime-data/…
└── share/doc/pathime/…
```

**The source tarball** (6.5): the repository tree at the tag with every
submodule populated at its pinned commit — `engines/{libhangul, anthy-unicode,
pyzy, ibus-table-chinese}` and `demo/cpp-terminal` present in full — and no
`.git`, no `refs/`, no build directories.

**6.4 — Licensing, which the artifacts change.** The decision is that everything
ships all data, and that has two consequences to state on the release page
rather than leave for a consumer to discover:

| Component | Licence | In library pkg | In demo pkg |
|---|---|---|---|
| libpathime | MIT | ✓ | ✓ |
| libhangul, anthy-unicode, pyzy (shared) | LGPL-2.1 | ✓ | ✓ |
| `pathime-data/pyzy/*` | LGPL-2.1 | ✓ | ✓ |
| `pathime-data/anthy/anthy.dic` | **GPL-2** | ✓ | ✓ |
| `pathime-data/table/*.db` | **GPL-3** | ✓ | ✓ |
| cpp-terminal (static) | MIT | — | ✓ |

So **both artifacts are GPL-3 as a whole** — roughly 25 MB of their 29 MB of
data is copyleft — even though the library's own `LICENSE` is MIT. Link
`THIRD-PARTY.md` from the release description; it already lays out the
per-component terms and the `LIBPATHIME_WITH_ANTHY=OFF` /
`LIBPATHIME_WITH_TABLE=OFF` escape hatch for a consumer who wants different
terms.

**`THIRD-PARTY.md` needs a restructure before the first release**, not just
additions. Per "Decisions already taken", one file serves every artifact, which
means it stops making tense- and context-dependent claims and instead says
where each component lands — in practice, promote the table above into the
document, with columns for the library package, the demo package, and
source-only. Three concrete deltas the restructure must carry:

1. A "What a binary release contains" section, describing the two artifacts.
2. **Its cpp-terminal entry becomes wrong.** That file currently lists
   cpp-terminal under "Development-time only" and states it is "not shipped in
   an install" — true today, and false the moment a demo package exists.
   cpp-terminal moves to a shipped component, MIT, statically linked into
   `pathime-demo` only. Written artifact-relative ("shipped in: demo package"),
   the statement cannot go stale this way again.
3. **The Windows runtime DLLs become shipped components** (4.3c): rows for
   glib (LGPL), libiconv (LGPL), libintl/gettext (LGPL) and pcre2
   (BSD-3-Clause), marked Windows-artifacts-only, with the exact licence
   versions confirmed from vcpkg's per-port copyright files. The existing
   "External dependencies" table's "shared; required by pyzy" is true on Linux
   and an understatement on Windows.

The LGPL relinking obligation attaches to a `BUILD_SHARED_LIBS=OFF` artifact in a
way it does not to the default shared one, and that is half of why **static
builds are out of scope for packaging** — settled under "Decisions already
taken". Both packages stay on the shared path even though a static demo would be
a tidier single file. Nothing here forbids a static build; it forbids shipping
one, so the table above needs no static row and 6.6's vcpkg port should offer no
static feature until someone asks and the licence question is answered.

**6.5 — Generate the real source tarball.** Settled under "Decisions already
taken": this is a required artifact, both because GitHub's auto-generated
"Source code (tar.gz)" **does not include submodules** — so for this project it
is not a buildable source release — and because it is the corresponding-source
offer that sits on the same page as the GPL-3 binaries. Mechanics:

**Implemented** as `tools/make-source-tarball.sh`. Run it from a checkout of
the tag; `-o` names the output directory.

- **Generation**: `git archive` of the superproject, concatenated with a
  `git archive` of each submodule at its pinned commit. Not the
  `git ls-files --recurse-submodules | tar -T -` this plan first proposed:
  that archives the *working tree*, so the output depends on the builder's
  `core.autocrlf` and smudge filters, and a release cut on Windows could ship
  CRLF into anthy's dictionary codegen, which BUILD.md requires to be LF.
  Reading blobs makes the content a function of the commit alone — verified
  here by comparing every extracted file against `git show`. `.git` is
  excluded by construction, and `refs/` and build directories are gitignored.
- **Reproducibly**: git archive emits tree order, `0:0` ownership and the
  commit date of the repository each file came from; `gzip -9n` records no
  timestamp. Two runs are byte-identical, checked. The caveat is that it is
  reproducible per git version, not across them, since the tar framing is
  git's.
- **Verified**: a CI job that configures, builds and tests **from the generated
  tarball**, not from the checkout. Still to write, and it must run on Linux:
  `engines/pyzy/src/main.db` is a symlink, so extracting the tarball on Windows
  without symlink privileges silently drops it.  That job is the only thing
  that keeps "buildable source release" true — it is also what catches any
  configure-time step that quietly assumed `.git` exists. Nothing in the build
  reads `.git` today, checked.

**6.6 — vcpkg port.** A `vcpkg.json` plus `portfile.cmake`, submitted as a PR to
`microsoft/vcpkg`. This is the natural first channel: the audience is C and C++
embedders, and the Windows build already depends on vcpkg. It packages the
library only — the demo is not a thing a vcpkg consumer wants. Requires Phase 4.

**6.7 — Conan.** Same shape, second audience, only if asked for.

---

# Suggested order

Phases 0 and 1 are worth having even if nothing else happens. Phase 2 is
independent of 3 and 4 and can be slotted wherever the interest is. Phase 4
gates all of Phase 6.

```
0 ──▶ 1 ──┬──▶ 2  (macOS)
          ├──▶ 3  (coverage, CodeQL, dependabot)
          └──▶ 4  (install/export) ──▶ 6 (releases, vcpkg)

5 (formatter) — measured and rejected; nothing to schedule.
```
