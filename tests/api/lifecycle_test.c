/*
 * Lifecycle coverage — registered ahead of the implementation; exits the skip
 * code until pathime_init() and the handle lifecycles exist. Planned checks:
 *
 *  - pathime_init()/pathime_shutdown() pairing; the _NOT_INITIALIZED and
 *    _ALREADY_INITIALIZED rejections; init params validation (struct_size,
 *    data_dir).
 *  - pathime_has_engine() agreeing with the PATHIME_WITH_* config.h macros
 *    once init has succeeded (abi_test covers only its pre-init falsity);
 *  - pathime_engine_create()/destroy() for every engine config.h says is
 *    compiled in; _UNKNOWN_ENGINE for the ones that are not; engine id and
 *    requirements round-trips (only hangul's PREEDIT_NONE mode sets a
 *    PATHIME_REQUIRES_* bit).
 *  - pathime_context_create() against a client table missing a required
 *    callback → _MISSING_CALLBACK; user_data and engine accessors;
 *    destruction ordering (contexts before their engine, engines before
 *    shutdown).
 *  - The focus rules: mutating calls on an unfocused context →
 *    _NOT_FOCUSED; focus loss preserving composition state.
 */

#include "api_test_util.h"

int main(void)
{
    return pt_skip("api.lifecycle",
                   "pathime_init() and handle lifecycles not implemented yet");
}
