/*
 * UTF-8 <-> UTF-16 for the Windows API, used by the two files in the library
 * that have to name a file to the platform rather than to a backend.
 *
 * Every path in libpathime is UTF-8 on every platform, which the public header
 * states and which the Windows narrow-character API does not honour: its `A`
 * entry points and the CRT's narrow open()/stat() decode their argument in the
 * process's active code page, so a resource or profile directory containing a
 * character outside that code page turns into a file that cannot be found. The
 * wide entry points have no such restriction, so everything the library opens
 * itself goes through a conversion here first.
 *
 * Header-only and Windows-only; there is nothing to convert anywhere else.
 */

#ifndef LIBPATHIME_SRC_WIN32_UTF_H
#define LIBPATHIME_SRC_WIN32_UTF_H

#if !defined(_WIN32)
#  error "win32_utf.h is Windows-only"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

namespace pathime {

/** UTF-16 to UTF-8. Empty if the conversion is refused. */
inline std::string utf16_to_utf8(const std::wstring &wide)
{
    if (wide.empty()) {
        return std::string();
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                           static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return std::string();
    }
    std::string out(static_cast<size_t>(needed), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                            static_cast<int>(wide.size()), &out[0], needed,
                            nullptr, nullptr) != needed) {
        return std::string();
    }
    return out;
}

/** UTF-8 to UTF-16. Empty if the conversion is refused. */
inline std::wstring utf8_to_utf16(const std::string &utf8)
{
    if (utf8.empty()) {
        return std::wstring();
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                           static_cast<int>(utf8.size()),
                                           nullptr, 0);
    if (needed <= 0) {
        return std::wstring();
    }
    std::wstring out(static_cast<size_t>(needed), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                            static_cast<int>(utf8.size()), &out[0],
                            needed) != needed) {
        return std::wstring();
    }
    return out;
}

}  // namespace pathime

#endif /* LIBPATHIME_SRC_WIN32_UTF_H */
