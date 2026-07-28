/*
 * Filesystem helpers shared by the adapters.
 *
 * Two functions, both there because more than one adapter has to build a path
 * beneath pathime_init_params_t::resource_dir and then decide whether the file
 * at the end of it is really there. Neither interprets the strings it is given:
 * a path with spaces, shell metacharacters or non-ASCII in it comes out of
 * path_join() exactly as it went in, which is the property the whole
 * resource-directory arrangement rests on.
 */

#ifndef LIBPATHIME_SRC_PATHS_H
#define LIBPATHIME_SRC_PATHS_H

#include <string>

namespace pathime {

/**
 * @a base and @a leaf joined with the platform's separator, inserting one only
 * where @a base does not already end in a separator.
 */
std::string path_join(const std::string &base, const char *leaf);

/**
 * Is @a path a regular file?
 *
 * The predicate is "regular file", not "can be opened": a directory must not
 * pass, and it would under an fopen-based test on glibc, where opening a
 * directory for reading succeeds and only the read fails. It also matches the
 * test pyzy applies to its own database candidates — glib's
 * G_FILE_TEST_IS_REGULAR, which is stat plus S_ISREG — so an adapter checking
 * for a file in front of a backend gets the same answer the backend will.
 */
bool is_regular_file(const std::string &path);

}  // namespace pathime

#endif /* LIBPATHIME_SRC_PATHS_H */
