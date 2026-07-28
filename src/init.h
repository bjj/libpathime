/*
 * Process-global state — the top layer of the two-layer lifetime (docs/adapter-findings.md,
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

/**
 * The resolved resource directory: pathime_init_params_t::resource_dir as
 * given, or `pathime-data` beside the libpathime binary, which is what NULL
 * selects. Never NULL while initialized(); same ownership as data_dir().
 *
 * This is where every read-only file the library ships lives — the anthy
 * dictionary, the pyzy database — and it is the counterpart of data_dir():
 * one directory the library only reads, one it only writes.
 */
const char *resource_dir();

/**
 * True if the backend behind @a id brought its process-global prerequisites up
 * successfully during pathime_init().
 *
 * A backend that could not — anthy without its dictionary, pyzy without its
 * database — is reported unavailable through pathime_has_engine() rather than
 * failing the whole library, which is what the public header describes:
 * pathime_has_engine() is false both for an engine this build does not contain
 * and for "one whose runtime prerequisites, such as its dictionaries, are
 * unavailable". A client with three engines compiled in and one dictionary
 * missing gets the other two, and finds out which through the query that
 * exists for exactly that purpose.
 *
 * False before pathime_init() and after pathime_shutdown(), for every id.
 */
bool backend_ready(pathime_engine_id_t id);

}  // namespace pathime

#endif /* LIBPATHIME_SRC_INIT_H */
