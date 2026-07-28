/* Added to the pyzy library by libpathime's port; not part of upstream pyzy.
 * See DataDir.h.
 */

#include "DataDir.h"

#include <string>

namespace {

/* Set once, before InputContext::init(), and read during it. A function-local
 * static so that nothing depends on the order in which this translation unit's
 * globals are constructed relative to any other's. */
std::string &data_dir ()
{
    static std::string dir;
    return dir;
}

}  // namespace

extern "C" void
pyzy_set_data_dir (const char *dir)
{
    data_dir () = (dir != NULL) ? dir : "";
}

namespace PyZy {

std::string
dataPath (const char *name)
{
    const std::string &dir = data_dir ();
    if (dir.empty ())
        return std::string ();

    std::string path = dir;
    const char last = path[path.size () - 1];
    if (last != '/' && last != '\\') {
#ifdef _WIN32
        path += '\\';
#else
        path += '/';
#endif
    }
    path += name;
    return path;
}

}  // namespace PyZy
