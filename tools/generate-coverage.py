#!/usr/bin/env python3
"""Regenerate src/engines/table/coverage_data.h from a font's character coverage.

Why this exists
---------------

A table method is not really a candidate-driven input method: the whole point of
Cangjie is that it is deterministic, and it produces text without ever consulting
a completion. Candidates get shown anyway, because we have them -- and the stock
candidates for a partially typed code are frequently *obscure*. The stock Cangjie
table carries roughly twice as many characters as the most capable CJK font, and
vastly more than an ordinary font with nothing like 30,000 glyphs, so a user one
keystroke into the weeds gets a candidate list of tofu.

Two things fix that, and they are halves of one purpose. Frequency augmentation
(handled elsewhere, in apply_frequency_transfer) keeps useful characters at the
front. Coverage filtering -- this -- keeps unrenderable ones out entirely.

Why a checked-in table rather than a build-time font lookup
----------------------------------------------------------

The fork this library's tables come from (bjj/ibus-table-chinese) shells out to
fc-query while preprocessing, which makes the produced table a function of which
fonts happen to be installed on the build machine: two builds of the same commit
produce different data. Here the map is generated once, checked in, and consumed
by tools/table-compile with no fontconfig involved at build time.

That is the same bargain src/engines/table/variants_data.h already makes with
Unicode data, and tools/generate-variants.py is its generator. Regeneration is a
deliberate, occasional act:

    python3 tools/generate-coverage.py \\
        /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc \\
        > src/engines/table/coverage_data.h

The build never runs this. LIBPATHIME_TABLE_REGENERATE_COVERAGE exists to wire it
up for someone who is deliberately refreshing the data.

Which font
----------

Noto Sans CJK, deliberately: the target is *inclusive*, not typical. This map is
the upper bound on what a shipped table can offer, and an embedder who needs
something narrower should be able to remove more at runtime rather than needing
characters added back to a table that no longer carries them. Choosing a narrow
font here would foreclose that.

Reading a font's coverage: the platform-specific part
-----------------------------------------------------

Everything below except read_charset() is platform-neutral. read_charset() is the
one piece that asks the operating system what a font covers, and it currently has
only the fontconfig answer.

    *** FOR THE WINDOWS SESSION ***

    Adding Windows means adding a second reader here and dispatching on
    sys.platform; nothing else in this file, and nothing in the C++, has to
    change. The contract is exactly: given a font path, return a set of ints.

    The Windows equivalent of `fc-query --format=%{charset}` is GetFontUnicodeRanges:

      1. AddFontResourceEx(path, FR_PRIVATE, 0) to load the file.
      2. Select it into a memory DC (CreateFontIndirect + SelectObject).
      3. GetFontUnicodeRanges(hdc, NULL) for the byte size, allocate a
         GLYPHSET, call it again to fill in.
      4. Walk GLYPHSET.ranges[]: each WCRANGE is {wcLow, cGlyphs}, so the
         covered points are wcLow .. wcLow + cGlyphs - 1.
      5. RemoveFontResourceEx.

    All reachable from ctypes with no build dependency. Two caveats worth
    knowing before you start:

      - GLYPHSET/WCRANGE are UTF-16 code *units*, so anything above the BMP
        arrives as surrogate halves. CJK Extension B and beyond live there and
        the shipped tables do reach into them, so surrogate pairs have to be
        recombined into scalars or that coverage is silently lost. fc-query has
        no such problem, which is why this note exists.
      - Pick the font by policy, not by mimicking this file's default: the
        Windows equivalent of "deliberately inclusive" is a different file, and
        which one is a decision rather than a lookup. Ship a second generated
        header and select between them, rather than overwriting this one --
        both platforms want their own map, and a single map generated on
        whichever machine ran last is the non-reproducibility this design
        exists to avoid.
"""

import subprocess
import sys


def read_charset(font_path):
    """The set of code points @font_path covers.

    The fontconfig answer. See the module docstring for the Windows one, which
    is the only other implementation this function should ever grow.

    `--index=0` picks the first face of a collection (.ttc). For Noto CJK the
    faces are the per-language variants of one shared glyph set, so the first is
    representative; a font whose faces genuinely differ would want a union
    instead.
    """
    result = subprocess.run(
        ["fc-query", "--index=0", "--format=%{charset}", font_path],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        sys.exit(
            "fc-query failed for %s: %s"
            % (font_path, result.stderr.strip() or "no such file?")
        )

    points = set()
    for token in result.stdout.split():
        bounds = token.split("-")
        first = int(bounds[0], 16)
        last = int(bounds[-1], 16)  # a bare point is its own end
        points.update(range(first, last + 1))
    return points


def font_description(font_path):
    """A human-readable identifier for the font, for the generated provenance."""
    result = subprocess.run(
        ["fc-query", "--index=0", "--format=%{family[0]} %{fontversion}", font_path],
        capture_output=True,
        text=True,
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def ranges(points):
    """Coalesce a set of code points into (first, last) runs."""
    out = []
    for point in sorted(points):
        if out and out[-1][1] == point - 1:
            out[-1][1] = point
        else:
            out.append([point, point])
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: generate-coverage.py FONT")

    font_path = sys.argv[1]
    points = read_charset(font_path)
    table = ranges(points)

    write = sys.stdout.write
    write("/*\n")
    write(" * Generated by tools/generate-coverage.py. Do not edit.\n")
    write(" *\n")
    write(" * The character coverage of a deliberately inclusive CJK font, used by\n")
    write(" * tools/table-compile to drop table entries whose characters could not be\n")
    write(" * rendered anywhere. TODO.md and the generator's docstring carry the\n")
    write(" * reasoning; the short version is that the stock Cangjie table holds about\n")
    write(" * twice as many characters as the most capable font, and a candidate list\n")
    write(" * of tofu is worse than a shorter one.\n")
    write(" *\n")
    write(" * Source font: %s\n" % font_description(font_path))
    write(" * Code points: %d in %d ranges\n" % (len(points), len(table)))
    write(" */\n\n")
    write("#ifndef LIBPATHIME_SRC_ENGINES_TABLE_COVERAGE_DATA_H\n")
    write("#define LIBPATHIME_SRC_ENGINES_TABLE_COVERAGE_DATA_H\n\n")
    write("#include <cstddef>\n")
    write("#include <cstdint>\n\n")
    write("namespace pathime {\n")
    write("namespace table {\n\n")
    write("struct CoverageRange {\n")
    write("    uint32_t first;\n")
    write("    uint32_t last;\n")
    write("};\n\n")
    write("constexpr CoverageRange kCoverageRanges[] = {\n")
    for first, last in table:
        write("    {0x%04X, 0x%04X},\n" % (first, last))
    write("};\n\n")
    write("constexpr size_t kCoverageRangeCount =\n")
    write("    sizeof(kCoverageRanges) / sizeof(kCoverageRanges[0]);\n\n")
    write("}  // namespace table\n")
    write("}  // namespace pathime\n\n")
    write("#endif /* LIBPATHIME_SRC_ENGINES_TABLE_COVERAGE_DATA_H */\n")


if __name__ == "__main__":
    main()
