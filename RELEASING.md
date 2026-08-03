# Releasing

The order of operations for a release that spans this repository and the two
bindings ([libpathime-sharp](https://github.com/bjj/libpathime-sharp),
[libpathime-python](https://github.com/bjj/libpathime-python)). The
single-repository mechanics — what the workflows build, how the packages are
laid out — live in BUILD.md, "Releases and packaging"; this file only owns
the sequence, because the sequence is what goes wrong across three trees.

The three repositories release in lockstep: one version number, three tags
of the same name, each binding's `libpathime` submodule pinned at that tag.
A binding release whose submodule points anywhere else fails its own
version-check script.

1. **libpathime.** Set the `PATHIME_VERSION_*` macros in
   `include/pathime/pathime.h` (the version's single point of definition —
   everything else derives from it) and add the dated `CHANGELOG.md` entry.
   Push, wait for CI, then tag `v<version>` and push the tag. The workflow
   builds and **tests** every binary package, generates and build-verifies
   the source tarball, writes `SHA256SUMS`, attests provenance, and creates
   a draft release. Review the draft and publish it.

2. **Each binding, in either order.** Bump the `libpathime` submodule to the
   published tag. Set the binding's own version-bearing files to the same
   version (the binding's version-check script names them and fails on
   disagreement — run it locally before pushing). Push, wait for CI — which
   tests the packaged artifact, not the checkout — then tag the same
   `v<version>` and push the tag.

   - **libpathime-sharp:** the workflow packs and validates, drafts the
     GitHub release, then waits at the `nuget-production` environment for
     approval before pushing to NuGet.org. NuGet cannot delete, only
     unlist — approve only after reviewing the draft.
   - **libpathime-python:** the workflow builds, checks and tests the wheel
     from a clean install, drafts the GitHub release, and publishes to PyPI
     via Trusted Publishing. PyPI deletions are restricted the same way.

3. **Afterwards.** Publish the binding drafts, then check the release pages
   link to one another correctly — each binding's notes point at the
   libpathime release of the same tag.
