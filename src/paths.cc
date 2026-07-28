#include "paths.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>

#if defined(_WIN32)
#  include "win32_utf.h"   /* pulls in <windows.h>, which FindFirstFileW needs */
#else
#  include <dirent.h>
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

std::vector<std::string> list_directory(const std::string &directory)
{
    std::vector<std::string> names;
    if (directory.empty()) {
        return names;
    }

#if defined(_WIN32)
    /*
     * FindFirstFileW rather than the ANSI form, for the reason every other
     * Windows path in this library uses the wide API: the directory may hold
     * characters the active code page cannot express, and a name that came back
     * mangled would be a name no later open could reopen.
     */
    const std::wstring pattern = utf8_to_utf16(path_join(directory, "*"));
    if (pattern.empty()) {
        return names;
    }

    WIN32_FIND_DATAW entry;
    const HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE) {
        return names;
    }
    do {
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        const std::string name = utf16_to_utf8(entry.cFileName);
        if (!name.empty()) {
            names.push_back(name);
        }
    } while (FindNextFileW(search, &entry) != 0);
    FindClose(search);
#else
    DIR *const dir = opendir(directory.c_str());
    if (dir == nullptr) {
        return names;
    }
    while (const struct dirent *entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        /*
         * stat() rather than trusting d_type, which is DT_UNKNOWN on several
         * filesystems and would silently drop every entry there.
         */
        if (is_regular_file(path_join(directory, name.c_str()))) {
            names.push_back(name);
        }
    }
    closedir(dir);
#endif

    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace pathime
