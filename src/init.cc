/*
 * Library lifetime — the process-global layer of the two-layer lifetime
 * (TODO.md §2, Finding 3). This file will own pathime_init() and
 * pathime_shutdown(): validating the init params, applying data_dir, and
 * running each compiled-in backend's one-time global initialization
 * (hangul_init's keyboard registry, anthy_init, pyzy's shared Database and
 * SpecialPhraseTable) through the backend.h lifetime hooks. It is documented
 * as the one slow call, so eager global work belongs here and nowhere else.
 *
 * The pre-init introspection — version and status strings — is implemented
 * already rather than stubbed: the answers are ABI-fixed statics, and
 * tests/api/abi_test.c exercises them to prove the library links.
 */

#include <pathime/pathime.h>

uint32_t pathime_version(void)
{
    return PATHIME_VERSION;
}

const char *pathime_version_string(void)
{
    return PATHIME_VERSION_STRING;
}

const char *pathime_status_string(pathime_status_t status)
{
    switch (status) {
    case PATHIME_OK:                        return "ok";
    case PATHIME_ERROR_INVALID_ARGUMENT:    return "invalid argument";
    case PATHIME_ERROR_UNKNOWN_ENGINE:      return "engine not available in this library";
    case PATHIME_ERROR_MISSING_CALLBACK:    return "client lacks a callback the engine requires";
    case PATHIME_ERROR_UNSUPPORTED:         return "engine does not implement this operation";
    case PATHIME_ERROR_NOT_INITIALIZED:     return "pathime_init() has not been called";
    case PATHIME_ERROR_ALREADY_INITIALIZED: return "pathime_init() has already succeeded";
    case PATHIME_ERROR_NOT_FOCUSED:         return "context is not focused";
    case PATHIME_ERROR_OUT_OF_MEMORY:       return "out of memory";
    case PATHIME_ERROR_BACKEND:             return "backend library or data file failure";
    }
    return "unknown status code";
}
