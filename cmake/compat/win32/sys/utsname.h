/*
 * Minimal <sys/utsname.h> for Windows (no such header on MSVC).
 *
 * pyzy's src/Util.h calls uname() solely to read the node (host) name. We
 * provide struct utsname and uname(); the implementation (utsname_win.c) fills
 * nodename via GetComputerNameA and leaves the rest as harmless constants.
 */
#ifndef LIBPATHIME_COMPAT_SYS_UTSNAME_H
#define LIBPATHIME_COMPAT_SYS_UTSNAME_H

#ifdef __cplusplus
extern "C" {
#endif

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

int uname(struct utsname *buf);

#ifdef __cplusplus
}
#endif

#endif /* LIBPATHIME_COMPAT_SYS_UTSNAME_H */
