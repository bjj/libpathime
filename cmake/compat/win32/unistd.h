/*
 * Minimal <unistd.h> for Windows (MSVC has no such header).
 *
 * anthy-unicode's filemap.c includes <unistd.h> for open()/close(). MSVC
 * exposes those (and read/write/lseek) from <io.h> under both the POSIX names
 * and the underscore-prefixed names. We just forward to it and fill the couple
 * of POSIX typedefs that <io.h> does not provide.
 */
#ifndef LIBPATHIME_COMPAT_UNISTD_H
#define LIBPATHIME_COMPAT_UNISTD_H

#include <io.h>
#include <process.h>
#include <stddef.h>

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#endif

#endif /* LIBPATHIME_COMPAT_UNISTD_H */
