#!/bin/sh
#
# Generate the source release tarball: every tracked file of the superproject
# and of all five submodules, at the commits this checkout pins.
#
# This artifact exists because GitHub's auto-generated "Source code (tar.gz)"
# omits submodules, and this project's submodules are three of its four
# backends — that download does not build. It is also the corresponding-source
# offer that has to sit beside the GPL-3 binaries; docs/ci-and-release-plan.md
# 6.5 has the reasoning.
#
# Usage:
#   tools/make-source-tarball.sh [-o OUTDIR]
#
# It archives HEAD and its pinned submodule commits. To cut a release, check
# the tag out first — ideally in a fresh clone, which is the only way to be
# sure no untracked state has been silently relied upon:
#
#   git clone --recurse-submodules <url> && cd libpathime
#   git checkout v0.1.0 && git submodule update --init --recursive
#   tools/make-source-tarball.sh -o /tmp/release
#
# Content comes from git, never from the working tree. That matters more than
# it looks: `git ls-files | tar` would archive whatever the builder's
# core.autocrlf and smudge filters left on disk, so a release cut on Windows
# could ship CRLF into anthy's dictionary codegen, which BUILD.md warns
# requires LF. Reading blobs makes the output a function of the commit alone.
#
# The result is byte-reproducible from the same commit and the same git
# version: git archive emits entries in tree order, owned by 0:0, stamped with
# the commit date of the repository they came from, and gzip records no
# timestamp of its own. Two runs must produce identical bytes, which is what
# lets the reproducibility job cover the tarball as well as the tables.

set -eu

outdir=.

while [ $# -gt 0 ]; do
    case $1 in
    -o | --output)
        [ $# -ge 2 ] || { echo "$0: $1 needs a directory" >&2; exit 2; }
        outdir=$2
        shift 2
        ;;
    -h | --help)
        sed -n '2,34p' "$0" | sed 's/^#//; s/^ //'
        exit 0
        ;;
    *)
        echo "$0: unknown argument '$1'" >&2
        exit 2
        ;;
    esac
done

root=$(git rev-parse --show-toplevel 2>/dev/null) || {
    echo "$0: not inside a git repository" >&2
    exit 1
}
cd "$root"

# GNU tar: --concatenate is what joins the per-repository archives, and BSD
# tar spells it differently.
if ! tar --version 2>/dev/null | head -n 1 | grep -q GNU; then
    echo "$0: GNU tar is required (found: $(tar --version 2>/dev/null | head -n 1))" >&2
    echo "  macOS: brew install gnu-tar, then PATH=\"\$(brew --prefix)/opt/gnu-tar/libexec/gnubin:\$PATH\"" >&2
    exit 1
fi

# Every submodule initialized. This is the check that matters most: an
# uninitialized submodule is not an error to archive, it just contributes
# nothing, and the result is a tarball that configures and then dies looking
# for a backend.
missing=$(git submodule status --recursive | sed -n 's/^-//p' | awk '{print $2}')
if [ -n "$missing" ]; then
    echo "$0: these submodules are not initialized:" >&2
    echo "$missing" | sed 's/^/  /' >&2
    echo "  run: git submodule update --init --recursive" >&2
    exit 1
fi

# Not fatal — the archive is of HEAD either way — but worth saying, because
# what lands in the tarball is then not what the developer is looking at.
dirty=$(git status --porcelain --untracked-files=no --ignore-submodules=none)
if [ -n "$dirty" ]; then
    echo "$0: warning: uncommitted changes; the archive is of HEAD, not of your tree" >&2
fi

commit=$(git rev-parse HEAD)

# Prefer the exact tag, so a release tarball is named for its release. A
# checkout that is not at a tag still produces a usable archive, named for
# what it actually is.
if version=$(git describe --tags --exact-match HEAD 2>/dev/null); then
    :
elif version=$(git describe --tags 2>/dev/null); then
    echo "$0: warning: HEAD is not at a tag; naming the archive '$version'" >&2
else
    version=$(git rev-parse --short HEAD)
    echo "$0: warning: no tags found; naming the archive '$version'" >&2
fi
version=${version#v}

name=libpathime-$version
mkdir -p "$outdir"
# Resolve after mkdir so a relative -o is not read against the repo root.
outdir=$(cd "$outdir" && pwd)
tarball=$outdir/$name.tar.gz

work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT INT TERM

git archive --format=tar --prefix="$name/" HEAD > "$work/main.tar"

# Each submodule archived from its own repository at its own pinned commit,
# then concatenated. `git archive` skips gitlinks, so the superproject archive
# leaves exactly the holes these fill. $sm_path and $displaypath are set by
# `submodule foreach`; the quoted body is evaluated by git, not by this shell.
git submodule foreach --recursive --quiet '
    safe=$(printf "%s" "$sm_path" | tr "/" "_")
    git archive --format=tar --prefix="'"$name"'/$displaypath/" HEAD \
        > "'"$work"'/sm-$safe.tar"
'

for sm in "$work"/sm-*.tar; do
    [ -e "$sm" ] || break
    tar --concatenate --file "$work/main.tar" "$sm"
done

gzip -9nc "$work/main.tar" > "$tarball"

count=$(tar -tzf "$tarball" | grep -cv '/$' || true)
printf '%s\n' "$tarball"
printf '  %s files from %s\n' "$count" "$commit"
if command -v sha256sum > /dev/null 2>&1; then
    printf '  sha256 %s\n' "$(sha256sum "$tarball" | cut -d' ' -f1)"
elif command -v shasum > /dev/null 2>&1; then
    printf '  sha256 %s\n' "$(shasum -a 256 "$tarball" | cut -d' ' -f1)"
fi
