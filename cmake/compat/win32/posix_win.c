/*
 * POSIX functions the UCRT does not provide, for libpathime's Windows compat
 * layer: the directory walk (<dirent.h>), the passwd lookup (<pwd.h>), and the
 * odds and ends declared in the compat <unistd.h> and <sys/time.h>.
 *
 * Only compiled on WIN32 (see cmake/LibpathimeCompat.cmake).
 */
#ifdef _WIN32

/* <windows.h> comes first on purpose: it drags in <winsock.h>, whose
 * gethostname() declaration must not meet the macro the compat <unistd.h>
 * would otherwise define (that header stands down once winsock is present). */
#include <windows.h>

#include <dirent.h>
#include <pwd.h>
#include <sys/time.h>
#include <unistd.h>

#include <errno.h>
#include <fcntl.h>    /* _O_RDWR, _O_BINARY */
#include <io.h>       /* _get_osfhandle, _write, _sopen_s, _chsize_s */
#include <share.h>    /* _SH_DENYNO */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- <dirent.h> ---------------------------------------------------------- */

struct LIBPATHIME_DIR {
    HANDLE           handle;   /* INVALID_HANDLE_VALUE once exhausted */
    WIN32_FIND_DATAA find;
    int              pending;  /* `find` holds an entry not yet returned */
    struct dirent    entry;
    char             pattern[MAX_PATH + 4];
};

DIR *
opendir(const char *name)
{
    DIR *d;
    size_t len;

    if (name == NULL || name[0] == '\0') {
        errno = ENOENT;
        return NULL;
    }

    len = strlen(name);
    d = (DIR *) calloc(1, sizeof(*d));
    if (d == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    if (_snprintf_s(d->pattern, sizeof(d->pattern), _TRUNCATE,
                    "%s%s*", name,
                    (name[len - 1] == '/' || name[len - 1] == '\\') ? "" : "\\") < 0) {
        free(d);
        errno = ENAMETOOLONG;
        return NULL;
    }

    d->handle = FindFirstFileA(d->pattern, &d->find);
    if (d->handle == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        free(d);
        errno = (err == ERROR_FILE_NOT_FOUND) ? ENOENT
              : (err == ERROR_PATH_NOT_FOUND) ? ENOENT
              : (err == ERROR_ACCESS_DENIED)  ? EACCES
                                              : EINVAL;
        return NULL;
    }
    d->pending = 1;
    return d;
}

struct dirent *
readdir(DIR *dirp)
{
    if (dirp == NULL || dirp->handle == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return NULL;
    }

    if (!dirp->pending) {
        if (!FindNextFileA(dirp->handle, &dirp->find))
            return NULL;   /* end of directory: no errno change, like POSIX */
    }
    dirp->pending = 0;

    strncpy(dirp->entry.d_name, dirp->find.cFileName,
            sizeof(dirp->entry.d_name) - 1);
    dirp->entry.d_name[sizeof(dirp->entry.d_name) - 1] = '\0';
    dirp->entry.d_ino = 0;
    dirp->entry.d_reclen = (unsigned short) sizeof(struct dirent);
    return &dirp->entry;
}

int
closedir(DIR *dirp)
{
    if (dirp == NULL) {
        errno = EBADF;
        return -1;
    }
    if (dirp->handle != INVALID_HANDLE_VALUE)
        FindClose(dirp->handle);
    free(dirp);
    return 0;
}

void
rewinddir(DIR *dirp)
{
    if (dirp == NULL)
        return;
    if (dirp->handle != INVALID_HANDLE_VALUE)
        FindClose(dirp->handle);
    dirp->handle = FindFirstFileA(dirp->pattern, &dirp->find);
    dirp->pending = (dirp->handle != INVALID_HANDLE_VALUE);
}

/* --- <pwd.h> ------------------------------------------------------------- */

/* One synthetic account, filled from the environment. USERPROFILE is the
 * Windows equivalent of $HOME; HOME wins if set (MSYS/Cygwin shells set it). */
struct passwd *
getpwuid(uid_t uid)
{
    static struct passwd pw;
    static char name[256];
    static char dir[MAX_PATH + 1];
    static char empty[] = "";
    const char *env;

    (void) uid;

    name[0] = '\0';
    if ((env = getenv("USERNAME")) != NULL || (env = getenv("USER")) != NULL) {
        strncpy(name, env, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }

    dir[0] = '\0';
    if ((env = getenv("HOME")) != NULL || (env = getenv("USERPROFILE")) != NULL) {
        strncpy(dir, env, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = '\0';
    }
    if (dir[0] == '\0')
        return NULL;   /* no home to report: callers must cope with NULL */

    pw.pw_name   = name;
    pw.pw_passwd = empty;
    pw.pw_uid    = (uid_t) 1000;
    pw.pw_gid    = (gid_t) 1000;
    pw.pw_gecos  = empty;
    pw.pw_dir    = dir;
    pw.pw_shell  = empty;
    return &pw;
}

struct passwd *
getpwnam(const char *name)
{
    (void) name;
    return getpwuid(0);
}

/* --- <unistd.h> ---------------------------------------------------------- */

int
fchmod(int fd, mode_t mode)
{
    /* No fd-based chmod in the CRT, and the only bit Windows tracks is
     * read-only — which every caller here is setting anyway. */
    (void) fd;
    (void) mode;
    return 0;
}

int
libpathime_gethostname(char *name, size_t len)
{
    DWORD n;

    if (name == NULL || len == 0) {
        errno = EINVAL;
        return -1;
    }
    n = (DWORD) len;
    if (!GetComputerNameA(name, &n)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

int
mkstemp(char *tmpl)
{
    /* _mktemp_s only picks a name; opening it O_EXCL is what makes the pair
     * race-free, so retry if someone won the race in between. */
    size_t len;
    int attempt, fd;

    if (tmpl == NULL) {
        errno = EINVAL;
        return -1;
    }
    len = strlen(tmpl);
    if (len < 6 || strcmp(tmpl + len - 6, "XXXXXX") != 0) {
        errno = EINVAL;
        return -1;
    }

    for (attempt = 0; attempt < 32; attempt++) {
        char candidate[MAX_PATH + 1];

        if (len >= sizeof(candidate)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(candidate, tmpl, len + 1);

        if (_mktemp_s(candidate, len + 1) != 0)
            return -1;   /* _mktemp_s set errno */

        if (_sopen_s(&fd, candidate,
                     _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY,
                     _SH_DENYNO, _S_IREAD | _S_IWRITE) == 0) {
            memcpy(tmpl, candidate, len + 1);
            return fd;
        }
        if (errno != EEXIST)
            return -1;
    }
    errno = EEXIST;
    return -1;
}

int
truncate(const char *path, long length)
{
    /* The CRT only has the fd-based _chsize_s, so open just long enough. */
    int fd, rc;

    if (_sopen_s(&fd, path, _O_RDWR | _O_BINARY, _SH_DENYNO, 0) != 0)
        return -1;   /* _sopen_s already set errno */

    rc = _chsize_s(fd, (__int64) length);
    _close(fd);

    if (rc != 0) {
        errno = rc;
        return -1;
    }
    return 0;
}

int
dprintf(int fd, const char *format, ...)
{
    va_list ap;
    char stackbuf[1024];
    char *buf = stackbuf;
    int need, written;

    va_start(ap, format);
    need = _vscprintf(format, ap);
    va_end(ap);
    if (need < 0)
        return -1;

    if ((size_t) need + 1 > sizeof(stackbuf)) {
        buf = (char *) malloc((size_t) need + 1);
        if (buf == NULL)
            return -1;
    }

    va_start(ap, format);
    written = vsnprintf(buf, (size_t) need + 1, format, ap);
    va_end(ap);

    if (written > 0)
        written = _write(fd, buf, (unsigned int) written);

    if (buf != stackbuf)
        free(buf);
    return written;
}

/* --- POSIX record locking (declared in win32_prelude.h) ------------------ */

int
fcntl(int fd, int cmd, ...)
{
    va_list ap;
    struct flock *lck;
    HANDLE h;
    OVERLAPPED ov;
    DWORD flags;
    DWORD len;

    if (cmd != F_SETLK && cmd != F_SETLKW && cmd != F_GETLK) {
        errno = EINVAL;
        return -1;
    }

    va_start(ap, cmd);
    lck = va_arg(ap, struct flock *);
    va_end(ap);

    if (lck == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* F_GETLK asks "who holds this?"; we have no way to answer, so report the
     * range as free, which is what a single-process caller expects. */
    if (cmd == F_GETLK) {
        lck->l_type = F_UNLCK;
        return 0;
    }

    h = (HANDLE) _get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }

    /* Only SEEK_SET ranges are used here; anything else would need the current
     * file position, which POSIX callers of F_SETLK rarely rely on. */
    if (lck->l_whence != SEEK_SET) {
        errno = EINVAL;
        return -1;
    }

    memset(&ov, 0, sizeof(ov));
    ov.Offset = (DWORD) lck->l_start;
    /* l_len == 0 means "to end of file"; LockFileEx wants an explicit count. */
    len = (lck->l_len > 0) ? (DWORD) lck->l_len : 0xFFFFFFFFu;

    if (lck->l_type == F_UNLCK) {
        if (!UnlockFileEx(h, 0, len, 0, &ov)) {
            errno = EACCES;
            return -1;
        }
        return 0;
    }

    flags = (lck->l_type == F_WRLCK) ? LOCKFILE_EXCLUSIVE_LOCK : 0;
    if (cmd == F_SETLK)
        flags |= LOCKFILE_FAIL_IMMEDIATELY;   /* F_SETLKW blocks instead */

    if (!LockFileEx(h, flags, 0, len, 0, &ov)) {
        errno = (GetLastError() == ERROR_LOCK_VIOLATION) ? EAGAIN : EACCES;
        return -1;
    }
    return 0;
}

/* --- <sys/time.h> -------------------------------------------------------- */

int
gettimeofday(struct timeval *tv, struct timezone *tz)
{
    /* FILETIME counts 100 ns ticks from 1601-01-01; shift to the Unix epoch. */
    static const unsigned long long EPOCH_DELTA_100NS = 116444736000000000ULL;
    FILETIME ft;
    ULARGE_INTEGER t;

    if (tz != NULL) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    if (tv == NULL)
        return 0;

    GetSystemTimeAsFileTime(&ft);
    t.LowPart  = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    t.QuadPart -= EPOCH_DELTA_100NS;

    tv->tv_sec  = (long) (t.QuadPart / 10000000ULL);
    tv->tv_usec = (long) ((t.QuadPart % 10000000ULL) / 10ULL);
    return 0;
}

#endif /* _WIN32 */
