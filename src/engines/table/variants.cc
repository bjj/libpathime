#include "engines/table/variants.h"

#include <algorithm>

#include "engines/table/variants_data.h"
#include "utf8.h"

namespace pathime {
namespace table {

uint8_t variant_mask(uint32_t scalar)
{
    /*
     * Binary search over the generated ranges, which are sorted and disjoint by
     * construction (tools/generate-variants.py coalesces a sorted map). The
     * table lists only the characters simplification changed, so a miss is the
     * common case and answers "both".
     */
    size_t low = 0;
    size_t high = kVariantRangeCount;
    while (low < high) {
        const size_t mid = low + (high - low) / 2;
        const VariantRange &range = kVariantRanges[mid];
        if (scalar < range.first) {
            high = mid;
        } else if (scalar > range.last) {
            low = mid + 1;
        } else {
            return range.mask;
        }
    }
    return kVariantBoth;
}

uint8_t phrase_variant_mask(const std::string &phrase)
{
    size_t offset = 0;
    uint32_t scalar = 0;
    if (!utf8_next_scalar(phrase.data(), phrase.size(), &offset, &scalar)) {
        return kVariantBoth;
    }
    return variant_mask(scalar);
}

bool variant_admits(pathime_chinese_variant_t variant, uint8_t mask)
{
    switch (variant) {
    case PATHIME_CHINESE_SIMPLIFIED_ONLY:
        return (mask & kVariantSimplified) != 0;
    case PATHIME_CHINESE_TRADITIONAL_ONLY:
        return (mask & kVariantTraditional) != 0;
    default:
        return true;
    }
}

int variant_boost(pathime_chinese_variant_t variant, uint8_t mask)
{
    switch (variant) {
    case PATHIME_CHINESE_SIMPLIFIED_FIRST:
        return (mask & kVariantSimplified) != 0 ? 1 : 0;
    case PATHIME_CHINESE_TRADITIONAL_FIRST:
        return (mask & kVariantTraditional) != 0 ? 1 : 0;
    default:
        return 0;
    }
}

}  // namespace table
}  // namespace pathime
