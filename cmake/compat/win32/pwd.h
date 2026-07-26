/*
 * Minimal <pwd.h> for Windows.
 *
 * anthy-unicode's src-diclib/conf.c does `getpwuid(getuid())->pw_dir` to find
 * the user's home directory. Windows has no passwd database; the implementation
 * (posix_win.c) synthesises a single entry from the environment.
 */
#ifndef LIBPATHIME_COMPAT_PWD_H
#define LIBPATHIME_COMPAT_PWD_H

#include <unistd.h>  /* uid_t, gid_t */

#ifdef __cplusplus
extern "C" {
#endif

struct passwd {
    char *pw_name;   /* login name           */
    char *pw_passwd; /* always ""            */
    uid_t pw_uid;
    gid_t pw_gid;
    char *pw_gecos;  /* always ""            */
    char *pw_dir;    /* home directory       */
    char *pw_shell;  /* always ""            */
};

struct passwd *getpwuid(uid_t uid);
struct passwd *getpwnam(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* LIBPATHIME_COMPAT_PWD_H */
