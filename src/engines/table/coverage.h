/*
 * Glyph-coverage filtering: dropping table entries whose characters no font
 * could render.
 *
 * A table method is not really a candidate-driven input method — the whole
 * advantage of Cangjie over pinyin is that it is deterministic, and it produces
 * text without ever consulting a completion. Candidates get shown anyway,
 * because we have them, and the stock candidates for a partially typed code are
 * frequently obscure. The stock Cangjie table carries roughly twice as many
 * characters as the most capable CJK font, so a user one keystroke into the
 * weeds sees a candidate list of tofu.
 *
 * Frequency augmentation (table_source.h, apply_frequency_transfer) keeps useful
 * characters at the front; this keeps unrenderable ones out entirely. They are
 * two halves of one purpose and the fork this library's tables come from applies
 * both.
 *
 * ---------------------------------------------------------------------------
 * Build-time only, and deliberately so
 * ---------------------------------------------------------------------------
 *
 * The coverage map is generated data checked into the tree
 * (tools/generate-coverage.py → coverage_data_*.h), not a font consulted at
 * build time. That is what keeps a compiled `.db` a function of the commit and a
 * recorded option rather than of which fonts the build machine happens to have
 * installed. Nothing here is reachable from the library: only
 * tools/table-compile links it.
 *
 * ---------------------------------------------------------------------------
 * Two maps, chosen at build time
 * ---------------------------------------------------------------------------
 *
 * `LIBPATHIME_TABLE_COVERAGE` picks which generated header this compiles
 * against — `coverage_data_noto.h` or `coverage_data_windows.h` — and neither is
 * a superset of the other, so neither is a default with an override. The filter
 * is in practice "drop CJK Extension B and beyond", and whether that is right
 * depends on the target: a Windows system with the Chinese language feature
 * carries SimSun-ExtB and can draw all of it, which is what the `none` setting
 * is for. BUILD.md, "Glyph coverage", is the guidance and carries the
 * measurements.
 *
 * Each map is taken from a deliberately *inclusive* reading of its target,
 * because it is the upper bound on what a shipped table can offer. An embedder
 * needing something narrower should be able to remove more at runtime — that
 * half is deferred, and TODO.md records the four questions it has to answer
 * first — and runtime narrowing only works if nothing has to be added back to a
 * table that no longer carries it.
 */

#ifndef LIBPATHIME_SRC_ENGINES_TABLE_COVERAGE_H
#define LIBPATHIME_SRC_ENGINES_TABLE_COVERAGE_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "engines/table/table_source.h"

namespace pathime {
namespace table {

/**
 * One run of covered code points, inclusive at both ends.
 *
 * Declared here rather than in the generated headers so that the two of them can
 * define `kCoverageRanges` and `kCoverageRangeCount` under the same names — only
 * one is ever included, and duplicating the type in both would make including
 * both a hard error rather than merely pointless.
 */
struct CoverageRange {
    uint32_t first;
    uint32_t last;
};

/** True if the generated coverage map contains @a scalar. */
bool is_covered(uint32_t scalar);

/**
 * Which map this was compiled against, as the generator described it — "Noto
 * Sans CJK", "Windows in-box CJK". tools/table-compile prints it beside the row
 * count, so a compiled table records the policy that trimmed it rather than
 * leaving it to be inferred from the build log.
 */
const char *coverage_map_name();

/**
 * True if every character of @a phrase is covered. An empty phrase is covered:
 * there is nothing in it that could fail to render.
 *
 * Every character, not just the first — unlike the variant classification of
 * §11.1, which takes a compound's script from its opening character. A phrase is
 * committed whole, so one unrenderable character in the middle of it is enough
 * to make the whole candidate useless.
 */
bool phrase_is_covered(const std::string &phrase);

/**
 * Drop every phrase row holding an uncovered character, and return how many were
 * dropped.
 *
 * The transformation lives here rather than beside apply_frequency_transfer() in
 * table_source.h, even though the two are halves of one purpose and run one after
 * the other, for a linkage reason: table_source.cc is compiled into the library,
 * and this pulls in a coverage_data_*.h. Keeping it on this side of the line means
 * the shipped library carries no coverage table at all — only
 * tools/table-compile links coverage.cc.
 *
 * Only `phrases` is filtered. `goucima` is left alone deliberately: it is
 * word-formation input for user-phrase derivation rather than anything a user
 * is shown, and derive_goucima() reads the phrase rows anyway, so a dropped
 * phrase takes its goucima with it — which is why the filter must run *before*
 * derivation.
 *
 * The count is returned rather than logged so the compile tool can print it
 * beside the row count. A table losing a suspicious number of rows is then a
 * number someone can see rather than a silent difference.
 */
size_t apply_coverage_filter(TableSource *source);

}  // namespace table
}  // namespace pathime

#endif /* LIBPATHIME_SRC_ENGINES_TABLE_COVERAGE_H */
