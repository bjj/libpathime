/*
 * Forced-include prelude for the Windows build (see LibpathimeCompat.cmake,
 * which injects it with /FI on every target that opts into the compat layer).
 *
 * The shim *headers* next to this file work by shadowing POSIX headers that do
 * not exist on Windows at all. That trick is unavailable for two cases:
 *
 *   1. Declarations that belong in a header MSVC *does* ship — shadowing
 *      <fcntl.h> or <malloc.h> would hide the real content behind it.
 *   2. Functions the vendored sources call with no #include at all, relying on
 *      glibc's <stdlib.h> pulling them in (alloca).
 *
 * Both are handled by injecting the declarations ahead of every translation
 * unit instead. Keep this file small: anything reachable through a shimmable
 * header belongs in that header, not here.
 */
#ifndef LIBPATHIME_COMPAT_WIN32_PRELUDE_H
#define LIBPATHIME_COMPAT_WIN32_PRELUDE_H

#ifdef _WIN32

/* --- alloca -------------------------------------------------------------- */
/* anthy's xstr.c, mkdic.c and mkdepgraph.c call alloca() without including
 * anything for it (glibc declares it from <stdlib.h>). Left implicit, MSVC
 * compiles it as an int-returning unknown function and the link fails. */
#include <malloc.h>
#ifndef alloca
#define alloca _alloca
#endif

/* --- case-insensitive compares (<strings.h>) ----------------------------- */
/* anthy's dic_util.c calls strncasecmp() without including <strings.h>. The
 * CRT spells these with a leading underscore. */
#include <string.h>
#ifndef strcasecmp
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp
#endif

/* --- POSIX record locking (<fcntl.h>) ------------------------------------ */
/* MSVC's <fcntl.h> has the O_* flags but no fcntl(), struct flock, or the
 * locking commands. anthy's src-worddic/priv_dic.c takes a write lock on a
 * one-byte range of its lock file; posix_win.c maps that onto LockFileEx. */
#include <sys/types.h>  /* off_t */

#ifndef F_SETLKW
#define F_GETLK  5
#define F_SETLK  6
#define F_SETLKW 7

#define F_RDLCK  0
#define F_WRLCK  1
#define F_UNLCK  2

struct flock {
    short l_type;    /* F_RDLCK | F_WRLCK | F_UNLCK */
    short l_whence;  /* SEEK_SET | SEEK_CUR | SEEK_END */
    off_t l_start;
    off_t l_len;     /* 0 means "to end of file" */
    int   l_pid;
};

#ifdef __cplusplus
extern "C" {
#endif
int fcntl(int fd, int cmd, ...);
#ifdef __cplusplus
}
#endif

#endif /* F_SETLKW */

#endif /* _WIN32 */

#endif /* LIBPATHIME_COMPAT_WIN32_PRELUDE_H */
