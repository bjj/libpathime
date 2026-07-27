/*
 * Process-global state — the top layer of the two-layer lifetime (TODO.md §2,
 * Finding 3). init.cc owns the state; this header is how the other core files
 * ask the two questions they all have to ask.
 *
 * Nearly every public entry point begins by rejecting calls made before
 * pathime_init() has succeeded, so `initialized()` is the most-called function
 * in the library. The exceptions are the pre-init-safe ones the header lists:
 * version, version_string, status_string, option_count, option_name — and
 * pathime_has_engine, which is callable but answers false.
 */

#ifndef LIBPATHIME_SRC_INIT_H
#define LIBPATHIME_SRC_INIT_H

namespace pathime {

/** True once pathime_init() has succeeded and before pathime_shutdown(). */
bool initialized();

/**
 * The resolved data directory: pathime_init_params_t::data_dir as given, or
 * the platform default that NULL selects. Never NULL while initialized(); the
 * pointer is owned by init.cc and valid until pathime_shutdown().
 *
 * This is the whole of the library's persistent-storage surface. Every backend
 * that would otherwise pick its own location from the environment is
 * redirected beneath it — see pathime_init_params_t::data_dir for why.
 */
const char *data_dir();

}  // namespace pathime

#endif /* LIBPATHIME_SRC_INIT_H */
