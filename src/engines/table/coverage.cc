#include "engines/table/coverage.h"

#include <utility>
#include <vector>

#include "engines/table/coverage_data.h"
#include "utf8.h"

namespace pathime {
namespace table {

bool is_covered(uint32_t scalar)
{
    /*
     * Binary search over the generated ranges, which are sorted and disjoint by
     * construction — tools/generate-coverage.py coalesces a sorted set. Same
     * shape as variant_mask(), but the polarity is opposite: that table lists
     * the exceptions and a miss means "both", while this one lists what the font
     * has and a miss means "not renderable".
     */
    size_t low = 0;
    size_t high = kCoverageRangeCount;
    while (low < high) {
        const size_t mid = low + (high - low) / 2;
        const CoverageRange &range = kCoverageRanges[mid];
        if (scalar < range.first) {
            high = mid;
        } else if (scalar > range.last) {
            low = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

bool phrase_is_covered(const std::string &phrase)
{
    size_t offset = 0;
    uint32_t scalar = 0;
    while (utf8_next_scalar(phrase.data(), phrase.size(), &offset, &scalar)) {
        if (!is_covered(scalar)) {
            return false;
        }
    }
    return true;
}

size_t apply_coverage_filter(TableSource *source)
{
    const size_t before = source->phrases.size();

    std::vector<PhraseRow> kept;
    kept.reserve(before);
    for (PhraseRow &row : source->phrases) {
        if (phrase_is_covered(row.phrase)) {
            kept.push_back(std::move(row));
        }
    }
    source->phrases = std::move(kept);

    return before - source->phrases.size();
}

}  // namespace table
}  // namespace pathime
