/*
 * Filesystem helpers shared by the adapters.
 *
 * The first two are here because more than one adapter has to build a path
 * beneath pathime_init_params_t::resource_dir and then decide whether the file
 * at the end of it is really there. The third has one caller and is here
 * anyway: listing a directory is a fact about the platform rather than about
 * tables, and putting a `#ifdef _WIN32` inside an engine to do something no
 * engine concept is involved in would be the wrong kind of specialization.
 * None of them interprets the strings it is given:
 * a path with spaces, shell metacharacters or non-ASCII in it comes out of
 * path_join() exactly as it went in, which is the property the whole
 * resource-directory arrangement rests on.
 */

#ifndef LIBPATHIME_SRC_PATHS_H
#define LIBPATHIME_SRC_PATHS_H

#include <string>
#include <vector>

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

/**
 * The names of the regular files directly in @a directory, sorted, without
 * their directory part. Subdirectories, dot entries and anything that is not a
 * regular file are left out.
 *
 * Sorted because a caller presenting the result to a user wants it stable
 * between runs, and readdir() order is not: it is whatever the filesystem
 * happens to return. A directory that does not exist or cannot be read is an
 * empty list rather than an error — the one caller is enumerating what an
 * installation happens to hold, where "nothing" is a normal answer.
 */
std::vector<std::string> list_directory(const std::string &directory);

}  // namespace pathime

#endif /* LIBPATHIME_SRC_PATHS_H */
