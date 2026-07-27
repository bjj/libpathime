/*
 * Options coverage — registered ahead of the implementation; exits the skip
 * code until the options machinery (src/options.cc) exists. Planned checks:
 *
 *  - The introspection walk: pathime_option_count(), name round-trips for
 *    every option, pathime_engine_option_info() descriptors consistent with
 *    the header's documented kinds and levels.
 *  - Kind-typed setters: wrong-kind calls rejected; engine-level vs
 *    context-level set/get/reset/is_set semantics, including a context
 *    override shadowing and then un-shadowing an engine value.
 *  - Per-engine support: options report themselves unsupported where the
 *    header says so — PATHIME_OPT_MAX_CANDIDATES on hangul (no candidates at
 *    all since hanja was cut) is the canonical case.
 *  - PATHIME_OPT_TABLE_FILE rejected while pathime_has_engine(TABLE) is
 *    false.
 */

#include "api_test_util.h"

int main(void)
{
    return pt_skip("api.options",
                   "options machinery (src/options.cc) not implemented yet");
}
