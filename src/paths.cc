#include "paths.h"

#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#  include "win32_utf.h"
#endif

namespace pathime {
namespace {

/** The platform's path separator, matching what init.cc's defaults use. */
#if defined(_WIN32)
const char kPathSeparator = '\\';
#else
const char kPathSeparator = '/';
#endif

}  // namespace

std::string path_join(const std::string &base, const char *leaf)
{
    std::string joined = base;
    if (!joined.empty() && joined.back() != kPathSeparator) {
        joined += kPathSeparator;
    }
    joined += leaf;
    return joined;
}

bool is_regular_file(const std::string &path)
{
#if defined(_WIN32)
    /* _wstat, not _stat: the narrow form decodes its argument in the active
     * code page and would report a perfectly good file missing whenever the
     * path contains a character outside it. */
    const std::wstring wide = utf8_to_utf16(path);
    if (wide.empty()) {
        return false;
    }
    struct _stat st;
    if (_wstat(wide.c_str(), &st) != 0) {
        return false;
    }
    return (st.st_mode & _S_IFMT) == _S_IFREG;
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode);
#endif
}

}  // namespace pathime
