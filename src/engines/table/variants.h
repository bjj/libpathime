/*
 * Chinese variant classification: which script a character belongs to, and what
 * PATHIME_OPT_CHINESE_VARIANT does with the answer (docs/ibus-table-mapping.md
 * §11.1).
 *
 * Per-adapter rather than in src/, by the rule docs/source-layout.md already
 * applies to the width and punctuation tables: the classification is content
 * about one language, not machinery shared between engines. It is not
 * duplicated from anywhere — pyzy has no classifier of its own to share,
 * because it collapses the option onto a single simplified-or-traditional flag
 * and lets its own data do the conversion (engines/pyzy/pyzy_backend.cc:175).
 * If pyzy's flag ever proves too coarse for the three mixed modes, this is what
 * gets hoisted.
 */

#ifndef LIBPATHIME_SRC_ENGINES_TABLE_VARIANTS_H
#define LIBPATHIME_SRC_ENGINES_TABLE_VARIANTS_H

#include <cstdint>
#include <string>

#include <pathime/pathime.h>

namespace pathime {
namespace table {

/** Bit 0: usable as simplified. Bit 1: usable as traditional. */
constexpr uint8_t kVariantSimplified = 1;
constexpr uint8_t kVariantTraditional = 2;
constexpr uint8_t kVariantBoth = kVariantSimplified | kVariantTraditional;

/**
 * The classification of @a scalar. Anything simplification never touched —
 * which is most Han characters, and every non-Han character — is kVariantBoth.
 */
uint8_t variant_mask(uint32_t scalar);

/**
 * The classification of a phrase, taken from its first character, which is what
 * §11.1 specifies. An empty phrase is kVariantBoth.
 *
 * Taking only the first character is ibus-table's rule rather than an
 * approximation this library chose, and it is worth knowing it is a rule with a
 * cost: a mixed-script compound is classified by whichever script its opening
 * character belongs to.
 */
uint8_t phrase_variant_mask(const std::string &phrase);

/**
 * True if @a mask survives @a variant's filter. Only the two exclusive modes
 * filter; the three mixed modes keep everything and express their preference
 * through the sort instead (ranking.h, key 4).
 */
bool variant_admits(pathime_chinese_variant_t variant, uint8_t mask);

/**
 * The sort boost @a mask earns under @a variant: 1 for the preferred script
 * under SIMPLIFIED_FIRST or TRADITIONAL_FIRST, 0 otherwise. Higher sorts first.
 */
int variant_boost(pathime_chinese_variant_t variant, uint8_t mask);

}  // namespace table
}  // namespace pathime

#endif /* LIBPATHIME_SRC_ENGINES_TABLE_VARIANTS_H */
