# Windows source-compatibility shims.
#
# anthy-unicode and pyzy include a few POSIX headers unconditionally
# (<sys/mman.h>, <uuid/uuid.h>, ...). Rather than patch the vendored sources,
# we keep replacement headers under cmake/compat/win32 and put that directory
# *ahead* of the system include path on Windows, so the vendored `#include`s
# resolve to our shims. The tiny implementations live in a static lib that
# consumers link.

set(LIBPATHIME_COMPAT_WIN32_DIR "${CMAKE_CURRENT_LIST_DIR}/compat/win32"
    CACHE INTERNAL "Windows POSIX shim include directory")

if(WIN32)
  add_library(libpathime_win32compat STATIC
    "${LIBPATHIME_COMPAT_WIN32_DIR}/mman.c"
    "${LIBPATHIME_COMPAT_WIN32_DIR}/uuid_win.c"
    "${LIBPATHIME_COMPAT_WIN32_DIR}/utsname_win.c")
  target_include_directories(libpathime_win32compat PUBLIC
    "${LIBPATHIME_COMPAT_WIN32_DIR}")
  # UuidCreate lives in Rpcrt4; the mmap shim uses core Win32 (kernel32).
  target_link_libraries(libpathime_win32compat PUBLIC Rpcrt4)
  add_library(libpathime::win32compat ALIAS libpathime_win32compat)
endif()

# Give <target> the POSIX shims on Windows; a no-op everywhere else so callers
# can invoke it unconditionally.
function(libpathime_add_win32_compat target)
  if(WIN32)
    target_include_directories(${target} BEFORE PRIVATE
      "${LIBPATHIME_COMPAT_WIN32_DIR}")
    target_link_libraries(${target} PRIVATE libpathime::win32compat)
  endif()
endfunction()
