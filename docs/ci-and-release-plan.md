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
- **GitHub is the source of truth.** `origin` is `github.com/bjj/libpathime`;
  the private orion remote is kept as `local` and stops mattering.
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

**Done 2026-08-01.** The repository is `github.com/bjj/libpathime`, public,
remote `origin`. The fork branches were verified pushed at the pinned commits;
a cold clone from GitHub built and passed 39/39 on Windows under
`windows-ninja`. Workflow permissions are default-read, Dependabot alerts and
security updates are on. Branch protection is deliberately deferred to 1.3 —
requiring status checks that do not exist yet blocks every merge, including
the one that would add the checks.

---

# Phase 1 — Core CI

**Done 2026-08-01.** `.github/workflows/ci.yml` carries the planned
eight-job matrix plus Phase 3's reproducibility job, all green; the
reasoning that used to live here is now comments in the workflow, next to
what it explains. What the runners taught beyond the plan: the
`windows-2025` image ships only Visual Studio 18, so `windows-msvc` runs on
`windows-2022`; CMake is pinned at 4.2.3 because 3.31 cannot find Visual
Studio on the 2025 image; Windows jobs force `core.autocrlf=false` before
checkout; vcpkg's applocal step races with itself under parallel Ninja
links, answered with a link pool of one (applocal must stay on — the table
compiler runs at build time against those DLLs); and the vcpkg cache uses a
per-image-label prefix fallback because the fleet rolls images gradually and
the exact rev rarely repeats. Branch protection (1.3) is a ruleset on the
default branch — require a PR, require the eleven checks — with a
repository-admin bypass so the maintainer's direct pushes still work.

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

**Done 2026-08-01.** `coverage.yml` (the `linux-coverage` preset, Codecov
upload — tokenless today; **[manual]** adding a `CODECOV_TOKEN` secret from
codecov.io removes the rate-limit flakiness), `codeql.yml` (c-cpp, manual
build, vendored trees path-ignored), `.github/dependabot.yml`
(`github-actions` and `gitsubmodule` — its first submodule-bump PR arrived
within a minute of landing), and the `reproducible-tables` job in `ci.yml`
(tables only, twice, hashes diffed; deliberately no `anthy.dic` comparison,
which is one data point and not a documented claim). All green.

---

# Phase 4 — Make the library consumable

**Done 2026-08-01.** The install is four components — `runtime`, `devel`,
`data`, `demo` — and `cmake/LibpathimePackage.cmake` turns them into the two
archives with two `cpack` invocations; both were built on Windows and matched
the 6.3 trees, and the unpacked demo package ran with a system-only PATH.
The demo takes its baked build-tree data directory only when it exists on
disk, falling through to the library's own per-user default anywhere else
(4.4); cpp-terminal was already static everywhere (4.6); the licence texts
install per component into `share/doc/pathime/licenses/` (4.8), including
the vcpkg copyright files for the Windows DLL closure. CI's
`install-consumer` job is 4.3 and 4.7 together: both link modes installed,
prefixes **moved**, `examples/install-check` built and *run* through
`find_package` and `pkg-config` — the static consumers executing from beside
`bin/pathime-data` — plus the assert that nothing ships the tests or the
table compiler. The MSVC-specific consumer notes (pkg-config `-l`/`-L`
translation for `cl`; MSVC auto-links the C++ runtime) were verified by hand
2026-07-29 and stay recorded in git history for whoever adds a Windows
consumer job.

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

**6.1 — Decided 2026-08-01.** The first tag is `v0.1.0`, matching
`CMakeLists.txt` (release.yml fails a tag whose version disagrees).
`CHANGELOG.md` now exists — one dated entry per release, and it is the only
document that speaks in dates; everything else keeps the present-tense rule.

**6.2 — `release.yml` exists; the first tag is its proof.** Both packages for
**linux-x86_64** and **linux-aarch64** (built on `ubuntu-22.04`/`-arm`, so
the stated glibc floor is 2.35) and **windows-x64**; the notes state the
floor, the runtime-dependency apt line, the MSVC-redist prerequisite and the
GPL-3 whole-artifact terms; provenance is attested; the release is created
as a **draft**, so publishing stays a human act on the release page.
**macos-arm64 joins the matrix when Phase 2 lands.** Windows arm64 and macOS
Intel wait for a consumer to ask.

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

**The `THIRD-PARTY.md` restructure landed 2026-08-01**: artifact-relative,
with the where-each-component-lands table, the artifacts section,
cpp-terminal as a shipped component of the demo package, and the Windows DLL
closure rows with versions confirmed from the vcpkg copyright texts.

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
- **Verified**: release.yml's `source` job builds and tests **from the
  generated tarball**, not from the checkout, on Linux (the tree carries a
  symlink Windows extraction can drop). That job is what keeps "buildable
  source release" true — it also catches any configure-time step that quietly
  assumed `.git` exists. Nothing in the build reads `.git` today, checked.

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
