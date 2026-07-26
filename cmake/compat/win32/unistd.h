/*
 * Minimal <unistd.h> for Windows (MSVC has no such header).
 *
 * The vendored submodules include <unistd.h> for the usual POSIX grab-bag:
 * open/close/access/getpid/getuid, the access() mode bits, and the S_I*
 * permission bits that on POSIX arrive via <sys/stat.h>. MSVC scatters those
 * across <io.h>/<direct.h>/<process.h> under underscore-prefixed names, and
 * simply lacks the rest — so this header is the single POSIX fill-in point for
 * the compat layer. (We deliberately do *not* shadow <sys/stat.h>, which does
 * exist on MSVC; we pull it in here and add the missing macros on top.)
 */
#ifndef LIBPATHIME_COMPAT_UNISTD_H
#define LIBPATHIME_COMPAT_UNISTD_H

#include <io.h>       /* access, open, close, read, write, lseek */
#include <direct.h>   /* _mkdir, _rmdir, _getcwd */
#include <process.h>  /* _getpid */
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#endif

/* The UCRT has no mode_t/pid_t/uid_t at all. */
#ifndef LIBPATHIME_COMPAT_MODE_T
#define LIBPATHIME_COMPAT_MODE_T
typedef unsigned short mode_t;
typedef int            pid_t;
typedef int            uid_t;
typedef int            gid_t;
#endif

/* access() mode bits. <io.h> declares access() but not the POSIX names for its
 * modes. Windows has no execute permission bit, and _access() actively rejects
 * a mode of 1 (it trips the invalid-parameter handler), so X_OK has to degrade
 * to an existence test rather than map to some CRT equivalent. */
#ifndef F_OK
#define F_OK 0
#define X_OK 0
#define W_OK 2
#define R_OK 4
#endif

/* POSIX permission bits. The UCRT only defines the owner-only _S_IREAD family
 * (see <sys/stat.h>); group/other bits do not exist on Windows and are zero,
 * which keeps `mode & S_IXGRP`-style tests correctly false. */
#ifndef S_IRUSR
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#define S_IXUSR _S_IEXEC
#define S_IRGRP 0
#define S_IWGRP 0
#define S_IXGRP 0
#define S_IROTH 0
#define S_IWOTH 0
#define S_IXOTH 0
#endif
#ifndef S_IRWXU
#define S_IRWXU (S_IRUSR | S_IWUSR | S_IXUSR)
#define S_IRWXG 0
#define S_IRWXO 0
#endif

#ifndef S_ISREG
#define S_ISREG(m)  (((m) & _S_IFMT) == _S_IFREG)
#define S_ISDIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
#define S_ISCHR(m)  (((m) & _S_IFMT) == _S_IFCHR)
#define S_ISFIFO(m) (((m) & _S_IFMT) == _S_IFIFO)
#define S_ISLNK(m)  (0)               /* no symlink bit in the CRT's st_mode */
#endif

/* POSIX spellings the CRT provides only underscored. */
#define getpid()      _getpid()
#define mkdir(p, m)   _mkdir(p)       /* Windows directories carry no mode */
#define rmdir(p)      _rmdir(p)
#define lstat(p, s)   stat((p), (s))  /* no symlink-aware stat in the CRT */

/* Single-user OS: everything runs as "the user", never as root. Callers use
 * this only to decide whether they are privileged (anthy's file_dic.c). */
#define getuid()      ((uid_t) 1000)
#define geteuid()     ((uid_t) 1000)
#define getgid()      ((gid_t) 1000)
#define getegid()     ((gid_t) 1000)

/* Implemented in posix_win.c. */
int fchmod(int fd, mode_t mode);
int dprintf(int fd, const char *format, ...);
int truncate(const char *path, long length);
int mkstemp(char *tmpl);   /* tmpl must end in "XXXXXX" */
int libpathime_gethostname(char *name, size_t len);

/* Winsock also declares gethostname (as __stdcall, and needing WSAStartup).
 * Prefer ours — GetComputerNameA needs no socket subsystem — but stand down if
 * a TU has already pulled in winsock. */
#if !defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_)
#define gethostname(n, l) libpathime_gethostname((n), (l))
#endif

#ifdef __cplusplus
}
#endif

#endif /* LIBPATHIME_COMPAT_UNISTD_H */
