/*
 * Candidate ordering: the multi-key sort of docs/ibus-table-mapping.md §8.2.
 *
 * Separate from the lookup because the order is not the database's opinion —
 * it depends on the typed key run, on the negotiated Chinese variant, and on
 * per-user frequencies, none of which the schema knows. The spec is explicit
 * that this order *is* the meaning of a candidate's position, since that
 * position is what a client passes back to select a candidate.
 */

#ifndef LIBPATHIME_SRC_ENGINES_TABLE_RANKING_H
#define LIBPATHIME_SRC_ENGINES_TABLE_RANKING_H

#include <cstddef>
#include <string>
#include <vector>

#include <pathime/pathime.h>

#include "engines/table/table_db.h"

namespace pathime {
namespace table {

/** The cap §8 fixes on a candidate list, independent of PATHIME_OPT_MAX_CANDIDATES. */
constexpr size_t kCandidateCap = 100;

/**
 * Sort @a matches into candidate order for the run @a keys, dropping what the
 * variant mode excludes and truncating to kCandidateCap.
 *
 * @a is_chinese comes from the table's LANGUAGES declaration: variant filtering
 * and boosting apply only to Chinese tables (§11.1), so a Zhuyin table and a
 * Japanese one are ordered by the remaining keys alone.
 */
void rank_candidates(const std::string &keys,
                     pathime_chinese_variant_t variant,
                     bool is_chinese,
                     std::vector<PhraseMatch> *matches);

}  // namespace table
}  // namespace pathime

#endif /* LIBPATHIME_SRC_ENGINES_TABLE_RANKING_H */
