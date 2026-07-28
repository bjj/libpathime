/*
 * module_dir(), one implementation per platform.
 *
 * Both of them ask the loader which file a *known address inside this library*
 * came from, rather than looking at argv[0] or the working directory. That is
 * what makes the answer survive the cases a client actually hits: a program
 * started through a symlink or from a different directory, a library loaded at
 * runtime by a plugin host, and a game whose install directory the user is
 * free to move. The address used is a static in this file, so it is inside
 * libpathime whether libpathime is a shared library or has been linked into
 * the program.
 *
 * Neither implementation goes near the process's encoding conventions: the
 * Windows path is fetched as UTF-16 and converted to UTF-8 explicitly, so a
 * profile or install directory outside the active code page arrives intact.
 */

#include "module_path.h"

#if defined(_WIN32)
#  include "win32_utf.h"
#  include <vector>
#else
#  ifndef _GNU_SOURCE
#    define _GNU_SOURCE  /* dladdr and Dl_info are GNU extensions. */
#  endif
#  include <dlfcn.h>
#  include <unistd.h>
#  include <vector>
#endif

namespace pathime {
namespace {

/*
 * The address the loader is asked about. A file-scope object rather than a
 * function, because taking a function's address can yield a thunk in another
 * section — an import stub or an incremental-linking jump table — which
 * resolves to the wrong module. A data object has no such indirection.
 */
const char kAnchor = 0;

/** Everything before the last separator, or "" if there is none. */
std::string parent_of(const std::string &path)
{
    const std::string::size_type slash = path.find_last_of(
#if defined(_WIN32)
        "\\/"
#else
        "/"
#endif
    );
    if (slash == std::string::npos) {
        return std::string();
    }
    /* A path whose only separator is the leading one has "/" as its parent,
     * not "". Losing that turns an absolute path into a relative one. */
    if (slash == 0) {
        return path.substr(0, 1);
    }
    return path.substr(0, slash);
}

#if defined(_WIN32)

std::string module_file()
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&kAnchor), &module)) {
        return std::string();
    }

    /*
     * GetModuleFileNameW truncates rather than telling you how much room it
     * wanted, and on the truncating path older Windows versions do not
     * NUL-terminate. Growing until the result fits strictly inside the buffer
     * is the only reading that is correct on every version. Paths are capped
     * at 32767 wide characters, so the loop terminates.
     */
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD len = GetModuleFileNameW(module, buffer.data(),
                                             static_cast<DWORD>(buffer.size()));
        if (len == 0) {
            return std::string();
        }
        if (len < buffer.size()) {
            return utf16_to_utf8(std::wstring(buffer.data(), len));
        }
        if (buffer.size() >= 32768) {
            return std::string();
        }
        buffer.resize(buffer.size() * 2);
    }
}

#else

std::string module_file()
{
    /*
     * dladdr answers for a shared library and, on most systems, for the
     * program too. Its dli_fname is whatever name the object was loaded
     * under, which for a shared library is the absolute path the dynamic
     * linker resolved.
     */
    Dl_info info;
    if (dladdr(const_cast<char *>(&kAnchor), &info) != 0 &&
        info.dli_fname != nullptr && info.dli_fname[0] == '/') {
        return std::string(info.dli_fname);
    }

    /*
     * The static-build fallback. With libpathime linked into the program there
     * is no separate object for dladdr to name, and what it reports for the
     * main map is the name the program was invoked under — which may be
     * relative, or empty. /proc/self/exe is the kernel's own answer and is
     * absolute, symlink-resolved, and immune to a later chdir().
     */
    std::vector<char> buffer(1024);
    for (;;) {
        const ssize_t len = readlink("/proc/self/exe", buffer.data(),
                                     buffer.size());
        if (len < 0) {
            return std::string();
        }
        /* readlink does not NUL-terminate and does not distinguish "exactly
         * filled" from "truncated", so a full buffer means try again. */
        if (static_cast<size_t>(len) < buffer.size()) {
            return std::string(buffer.data(), static_cast<size_t>(len));
        }
        if (buffer.size() >= 65536) {
            return std::string();
        }
        buffer.resize(buffer.size() * 2);
    }
}

#endif

}  // namespace

std::string module_dir()
{
    return parent_of(module_file());
}

}  // namespace pathime
