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
 * (tools/generate-coverage.py → coverage_data.h), not a font consulted at build
 * time. That is what keeps a compiled `.db` a function of the commit rather than
 * of which fonts the build machine happens to have installed. Nothing here is
 * reachable from the library: only tools/table-compile links it.
 *
 * The map is taken from a deliberately *inclusive* font, because it is the upper
 * bound on what a shipped table can offer. An embedder needing something
 * narrower should be able to remove more at runtime — that half is deferred, and
 * TODO.md records the four questions it has to answer first — and runtime
 * narrowing only works if nothing has to be added back to a table that no longer
 * carries it.
 */

#ifndef LIBPATHIME_SRC_ENGINES_TABLE_COVERAGE_H
#define LIBPATHIME_SRC_ENGINES_TABLE_COVERAGE_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "engines/table/table_source.h"

namespace pathime {
namespace table {

/** True if the generated coverage map contains @a scalar. */
bool is_covered(uint32_t scalar);

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
 * and this pulls in coverage_data.h. Keeping it on this side of the line means
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
