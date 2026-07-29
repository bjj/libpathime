#include "engines/table/ranking.h"

#include <algorithm>

#include "engines/table/variants.h"
#include "utf8.h"

namespace pathime {
namespace table {

namespace {

/** The scalar a phrase starts with, for the final tie-break (§8.2 key 9). */
uint32_t first_scalar(const std::string &phrase)
{
    size_t offset = 0;
    uint32_t scalar = 0;
    if (!utf8_next_scalar(phrase.data(), phrase.size(), &offset, &scalar)) {
        return 0;
    }
    return scalar;
}

/** Everything about one match that the sort compares, computed once. */
struct SortKey {
    int exact = 0;    /* 1 if the typed run equals the candidate's tabkeys */
    int64_t user_freq = 0;
    int boost = 0;    /* the variant preference, 1 for the favoured script */
    int64_t freq = 0;
    size_t length = 0;
    const std::string *tabkeys = nullptr;
    uint32_t scalar = 0;
    size_t index = 0; /* the position in the result set, to keep the sort total */
};

}  // namespace

void rank_candidates(const std::string &keys,
                     pathime_chinese_variant_t variant,
                     bool is_chinese,
                     std::vector<PhraseMatch> *matches)
{
    /*
     * Filtering first, so that the exclusive variant modes remove rows before
     * anything is ranked. A candidate the mode excludes is not a low-ranked
     * candidate, it is not a candidate.
     */
    if (is_chinese) {
        matches->erase(
            std::remove_if(matches->begin(), matches->end(),
                           [variant](const PhraseMatch &match) {
                               return !variant_admits(variant,
                                                      phrase_variant_mask(match.phrase));
                           }),
            matches->end());
    }

    std::vector<SortKey> keys_for;
    keys_for.reserve(matches->size());
    for (size_t i = 0; i < matches->size(); ++i) {
        const PhraseMatch &match = (*matches)[i];
        SortKey key;
        key.exact = (match.tabkeys == keys) ? 1 : 0;
        key.user_freq = match.user_freq;
        key.boost = is_chinese ? variant_boost(variant, phrase_variant_mask(match.phrase)) : 0;
        key.freq = match.freq;
        key.length = match.tabkeys.size();
        key.tabkeys = &match.tabkeys;
        key.scalar = first_scalar(match.phrase);
        key.index = i;
        keys_for.push_back(key);
    }

    /*
     * §8.2's keys in order. Two of the nine are deliberately absent rather
     * than silently omitted: the pinyin tone penalty (key 2) needs the pinyin
     * table, which has no source data here, and the Big5 ordering (key 8)
     * needs a Big5 mapping this library does not carry. Both affect only ties
     * that the remaining keys already order plausibly.
     */
    std::sort(keys_for.begin(), keys_for.end(), [](const SortKey &a, const SortKey &b) {
        if (a.exact != b.exact) {
            return a.exact > b.exact;
        }
        if (a.user_freq != b.user_freq) {
            return a.user_freq > b.user_freq;
        }
        if (a.boost != b.boost) {
            return a.boost > b.boost;
        }
        if (a.freq != b.freq) {
            return a.freq > b.freq;
        }
        if (a.length != b.length) {
            return a.length < b.length;  /* shorter tabkeys first */
        }
        if (*a.tabkeys != *b.tabkeys) {
            return *a.tabkeys < *b.tabkeys;
        }
        if (a.scalar != b.scalar) {
            return a.scalar < b.scalar;
        }
        return a.index < b.index;
    });

    std::vector<PhraseMatch> ordered;
    ordered.reserve(std::min(keys_for.size(), kCandidateCap));
    for (const SortKey &key : keys_for) {
        if (ordered.size() >= kCandidateCap) {
            break;
        }
        ordered.push_back((*matches)[key.index]);
    }
    matches->swap(ordered);
}

}  // namespace table
}  // namespace pathime
