#!/usr/bin/env python3
"""Regenerate a glyph-coverage map in src/engines/table/ from one or more fonts.

Why this exists
---------------

A table method is not really a candidate-driven input method: the whole point of
Cangjie is that it is deterministic, and it produces text without ever consulting
a completion. Candidates get shown anyway, because we have them -- and the stock
candidates for a partially typed code are frequently *obscure*. The stock Cangjie
table carries roughly twice as many characters as an ordinary CJK font, so a user
one keystroke into the weeds gets a candidate list of tofu.

Two things fix that, and they are halves of one purpose. Frequency augmentation
(handled elsewhere, in apply_frequency_transfer) keeps useful characters at the
front. Coverage filtering -- this -- keeps unrenderable ones out entirely.

Why a checked-in table rather than a build-time font lookup
-----------------------------------------------------------

The fork this library's tables come from (bjj/ibus-table-chinese) shells out to
fc-query while preprocessing, which makes the produced table a function of which
fonts happen to be installed on the build machine: two builds of the same commit
produce different data. Here the map is generated once, checked in, and consumed
by tools/table-compile with no font read at build time.

That is the same bargain src/engines/table/variants_data.h already makes with
Unicode data, and tools/generate-variants.py is its generator. Regeneration is a
deliberate, occasional act:

    python3 tools/generate-coverage.py --map noto \\
        --output src/engines/table/coverage_data_noto.h \\
        /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc

Two maps ship, and why
----------------------

There is no single answer to "what can a font render", because the two platforms
this library targets have materially different CJK font landscapes. Measured
against the five shipped tables (305,150 rows, 70,948 distinct characters):

    map                          code points   rows dropped
    Noto Sans CJK                     44,810   111,827 (36.6%)   cangjie5 52.4%
    Windows in-box CJK                30,360   117,626 (38.5%)   cangjie5 55.1%
    + SimSun-ExtB / MingLiU-ExtB     113,496         0 ( 0.0%)

The filter is, in practice, "drop CJK Extension B and beyond": of the 40,686
distinct characters the Noto map drops, 40,603 are supplementary-plane and the
remaining 83 are private-use. So the map that is right for a target depends
entirely on whether that target has a supplementary-plane font, and Windows is
the case where it might: a system with the Chinese language feature installed
carries SimSun-ExtB, which alone covers 60,349 supplementary code points and
makes the whole filter a no-op. That is what LIBPATHIME_TABLE_COVERAGE=none is
for; BUILD.md, "Glyph coverage", has the guidance.

Neither map is a superset of the other, so neither is "the default with an
override". Which one a build uses is an explicit, recorded option rather than a
property of the machine that ran the build, which is the whole point of checking
them in.

Reading a font's coverage
-------------------------

read_charset() parses the font's own `cmap` table, formats 4 (BMP) and 12 (full
range), with no platform library involved. That is deliberate, and replaces an
earlier plan to call fontconfig on Linux and GDI on Windows:

  - fc-query works, but makes the generator Linux-only for no gain -- its charset
    comes from FreeType reading the same `cmap` this does.

  - GDI's GetFontUnicodeRanges, the obvious Windows equivalent, **cannot express
    supplementary-plane coverage at all**, which is precisely the range this map
    is deciding about. GLYPHSET/WCRANGE are UTF-16 code *units*. Measured on
    Windows 11 against SimSun-ExtB, whose cmap covers 60,349 supplementary code
    points: GetFontUnicodeRanges reported 97 code units and *zero* surrogates.
    Not surrogate halves needing recombination -- simply absent. (DirectWrite's
    IDWriteFontFace::GetUnicodeRanges uses UINT32 and does express it, but that
    is COM through ctypes for a script that runs twice a year.)

Parsing `cmap` is about fifty lines, is correct above the BMP, needs nothing
installed, and reads a font file that need not even be a system font. The
contract is unchanged from when this was platform-specific: given a font path,
return a set of ints.
"""

import argparse
import os
import struct
import sys


# ---------------------------------------------------------------------------
# Font parsing
# ---------------------------------------------------------------------------


def _read_table_directory(data, index):
    """Offsets of the SFNT tables of face @index, as {tag: (offset, length)}.

    Handles both a bare font and a TrueType collection, whose header is a list of
    offsets to per-face table directories that share the underlying table data.
    """
    if data[:4] == b"ttcf":
        count = struct.unpack(">I", data[8:12])[0]
        if index >= count:
            raise ValueError("face index %d out of range (%d faces)" % (index, count))
        base = struct.unpack(">%dI" % count, data[12:12 + 4 * count])[index]
    elif index != 0:
        raise ValueError("face index %d requested but this is not a collection" % index)
    else:
        base = 0

    numTables = struct.unpack(">H", data[base + 4:base + 6])[0]
    tables = {}
    for i in range(numTables):
        entry = base + 12 + 16 * i
        tag = data[entry:entry + 4]
        tables[tag] = struct.unpack(">II", data[entry + 8:entry + 16])
    return tables


def _cmap_format4(data, offset):
    """Code points of a format 4 subtable: segmented mapping, BMP only."""
    points = set()
    segCountX2 = struct.unpack(">H", data[offset + 6:offset + 8])[0]
    segments = segCountX2 // 2
    ends = offset + 14
    starts = ends + segCountX2 + 2  # +2 for the reservedPad between the arrays
    for i in range(segments):
        end = struct.unpack(">H", data[ends + 2 * i:ends + 2 * i + 2])[0]
        start = struct.unpack(">H", data[starts + 2 * i:starts + 2 * i + 2])[0]
        # The final segment is the required 0xFFFF terminator, not coverage.
        if start == 0xFFFF:
            continue
        points.update(range(start, min(end, 0xFFFE) + 1))
    return points


def _cmap_format12(data, offset):
    """Code points of a format 12 subtable: segmented coverage, full range."""
    points = set()
    nGroups = struct.unpack(">I", data[offset + 12:offset + 16])[0]
    for i in range(nGroups):
        group = offset + 16 + 12 * i
        start, end, _glyph = struct.unpack(">III", data[group:group + 12])
        points.update(range(start, end + 1))
    return points


def read_charset(font_path, index=0):
    """The set of code points face @index of @font_path covers.

    Both Unicode subtables are unioned when present rather than one being
    preferred: format 12 is a superset of format 4 in every well-formed font, so
    the union costs nothing, and a font whose format 4 subtable carries something
    its format 12 one omits is a font we would rather over-cover than under-cover.
    A map that is too generous leaves a rare character in a candidate list; one
    that is too mean removes a character the user can actually see.
    """
    with open(font_path, "rb") as handle:
        data = handle.read()

    tables = _read_table_directory(data, index)
    if b"cmap" not in tables:
        raise ValueError("%s has no cmap table" % font_path)

    cmap = tables[b"cmap"][0]
    numSubtables = struct.unpack(">H", data[cmap + 2:cmap + 4])[0]

    points = set()
    for i in range(numSubtables):
        record = cmap + 4 + 8 * i
        platform, encoding, offset = struct.unpack(">HHI", data[record:record + 8])
        offset += cmap
        fmt = struct.unpack(">H", data[offset:offset + 2])[0]
        # (3,1) is Windows BMP; (3,10) is Windows full repertoire. (0,*) is the
        # Unicode platform, which uses the same subtable formats.
        if fmt == 4 and (platform, encoding) in ((3, 1), (0, 3)):
            points |= _cmap_format4(data, offset)
        elif fmt == 12 and (platform, encoding) in ((3, 10), (0, 4), (0, 6)):
            points |= _cmap_format12(data, offset)

    if not points:
        raise ValueError("%s face %d: no usable Unicode cmap subtable"
                         % (font_path, index))
    return points


def font_description(font_path, index=0):
    """A human-readable identifier for a face, for the generated provenance.

    Family name and font revision, formatted the way `fc-query
    --format='%{family[0]} %{fontversion}'` formatted them, so a header
    regenerated by this parser is comparable with one the fontconfig version
    produced. fontconfig reports the revision as head.fontRevision's raw 16.16
    fixed-point value, not as a decimal, which is why 2.004 appears as 131334.
    """
    with open(font_path, "rb") as handle:
        data = handle.read()

    tables = _read_table_directory(data, index)

    revision = ""
    if b"head" in tables:
        head = tables[b"head"][0]
        revision = " %d" % struct.unpack(">I", data[head + 4:head + 8])[0]

    family = "unknown"
    if b"name" in tables:
        name = tables[b"name"][0]
        count, string_offset = struct.unpack(">HH", data[name + 2:name + 6])
        strings = name + string_offset
        # nameID 1 is the family. Prefer Windows/UTF-16BE/en-US, because the CJK
        # fonts this is pointed at carry a localised family name too and picking
        # by platform alone yields whichever record comes first -- Microsoft
        # JhengHei reports 微軟正黑體, which is correct but unhelpful in a
        # provenance line an English-reading diff has to check.
        english = None
        fallback = None
        for i in range(count):
            record = name + 6 + 12 * i
            platform, encoding, language, name_id, length, offset = struct.unpack(
                ">HHHHHH", data[record:record + 12])
            if name_id != 1:
                continue
            raw = data[strings + offset:strings + offset + length]
            if (platform, encoding) == (3, 1):
                decoded = raw.decode("utf-16-be", "replace")
                if language == 0x0409:
                    english = decoded
                    break
                fallback = fallback or decoded
            elif platform == 1 and language == 0:
                fallback = fallback or raw.decode("mac-roman", "replace")
        family = english or fallback or family

    return family + revision


# ---------------------------------------------------------------------------
# Emitting the header
# ---------------------------------------------------------------------------


def ranges(points):
    """Coalesce a set of code points into (first, last) runs."""
    out = []
    for point in sorted(points):
        if out and out[-1][1] == point - 1:
            out[-1][1] = point
        else:
            out.append([point, point])
    return out


def parse_font_argument(spec):
    """Split a `path` or `path#index` font argument.

    A collection's faces are usually the same glyph set under different names --
    SimSun and NSimSun, Microsoft YaHei and Microsoft YaHei UI -- so face 0 is
    representative and is the default. `#N` is there for the collection whose
    faces genuinely differ.
    """
    if "#" in spec:
        path, _, index = spec.rpartition("#")
        return path, int(index)
    return spec, 0


def guard_for(output_path):
    """The include guard matching @output_path, spelled the way the tree does."""
    stem = os.path.basename(output_path or "coverage_data.h")
    stem = stem.replace(".", "_").replace("-", "_").upper()
    return "LIBPATHIME_SRC_ENGINES_TABLE_" + stem


def emit(write, table, guard, map_name, provenance, point_count):
    write("/*\n")
    write(" * Generated by tools/generate-coverage.py. Do not edit.\n")
    write(" *\n")
    write(" * The character coverage of %s, used by tools/table-compile\n" % map_name)
    write(" * to drop table entries whose characters could not be rendered on the\n")
    write(" * target this map describes. Which map a build uses is\n")
    write(" * LIBPATHIME_TABLE_COVERAGE; BUILD.md, \"Glyph coverage\", is the guidance,\n")
    write(" * and the generator's docstring carries the measurements behind it.\n")
    write(" *\n")
    for line in provenance:
        write(" * Source font: %s\n" % line)
    write(" * Code points: %d in %d ranges\n" % (point_count, len(table)))
    write(" */\n\n")
    write("#ifndef %s\n" % guard)
    write("#define %s\n\n" % guard)
    # coverage.h owns CoverageRange so that the two generated maps can declare
    # the same symbols without either having to redefine the type.
    write("#include \"engines/table/coverage.h\"\n\n")
    write("namespace pathime {\n")
    write("namespace table {\n\n")
    write("constexpr const char kCoverageMapName[] = \"%s\";\n\n" % map_name)
    write("constexpr CoverageRange kCoverageRanges[] = {\n")
    for first, last in table:
        write("    {0x%04X, 0x%04X},\n" % (first, last))
    write("};\n\n")
    write("constexpr size_t kCoverageRangeCount =\n")
    write("    sizeof(kCoverageRanges) / sizeof(kCoverageRanges[0]);\n\n")
    write("}  // namespace table\n")
    write("}  // namespace pathime\n\n")
    write("#endif /* %s */\n" % guard)


def main():
    parser = argparse.ArgumentParser(
        description="Generate a glyph-coverage map from one or more fonts.",
        epilog="A font may be given as PATH or PATH#FACE to pick a face out of a "
               "TrueType collection. Several fonts produce the union of their "
               "coverage, which is how the Windows map is built: no single "
               "in-box face covers what the platform can draw.")
    parser.add_argument("fonts", nargs="+", metavar="FONT",
                        help="font file, optionally PATH#FACE")
    parser.add_argument("--map", default="a CJK font", metavar="NAME",
                        help="what this map describes, recorded in the header "
                             "and compiled in as kCoverageMapName so the compile "
                             "tool can report which map trimmed a table")
    parser.add_argument("--output", metavar="PATH",
                        help="write here instead of stdout. Preferred over shell "
                             "redirection, which CMake's custom-command handling "
                             "does not treat identically across generators.")
    args = parser.parse_args()

    points = set()
    provenance = []
    for spec in args.fonts:
        path, index = parse_font_argument(spec)
        if not os.path.exists(path):
            sys.exit("no such font: %s" % path)
        try:
            points |= read_charset(path, index)
            provenance.append("%s (%s)" % (font_description(path, index),
                                           os.path.basename(path)))
        except (ValueError, struct.error) as error:
            sys.exit("%s: %s" % (path, error))

    table = ranges(points)
    guard = guard_for(args.output)

    if args.output:
        with open(args.output, "w", encoding="utf-8", newline="\n") as handle:
            emit(handle.write, table, guard, args.map, provenance, len(points))
    else:
        emit(sys.stdout.write, table, guard, args.map, provenance, len(points))


if __name__ == "__main__":
    main()
