/* Where pyzy reads its shipped data from.
 *
 * Added to the pyzy library by libpathime's port; not part of upstream pyzy.
 *
 * Upstream names its data files two ways, and neither survives being shipped
 * inside another program: three absolute paths beneath the PKGDATADIR that was
 * compiled in, and then bare "main.db" and "phrases.txt" resolved against the
 * process's working directory. This replaces both with one directory the
 * embedding program names at runtime, before PyZy::InputContext::init().
 *
 * The path is taken verbatim — no expansion, no splitting, no assumption about
 * separators beyond appending one — so any directory the platform accepts
 * works. It is only ever read from.
 */

#ifndef __PYZY_DATA_DIR_H_
#define __PYZY_DATA_DIR_H_

#ifdef __cplusplus
#include <string>
extern "C" {
#endif

/**
 * Set the directory holding main.db and phrases.txt. @a dir is copied; NULL or
 * "" means no directory is configured, which leaves pyzy with no database, the
 * state it already reports through a g_warning from Database::open().
 */
void pyzy_set_data_dir (const char *dir);

#ifdef __cplusplus
}

namespace PyZy {

/**
 * @a name resolved against the directory pyzy_set_data_dir() was given, or an
 * empty string when no directory is configured — which every caller must treat
 * as "the file is not there", since "" names nothing.
 */
std::string dataPath (const char *name);

}  // namespace PyZy
#endif

#endif  /* __PYZY_DATA_DIR_H_ */
