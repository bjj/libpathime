/*
 * Minimal <dirent.h> for Windows, over FindFirstFileA/FindNextFileA.
 *
 * anthy-unicode's src-worddic/priv_dic.c walks the imported-dictionary
 * directory with opendir/readdir/closedir and reads only `d_name`. The
 * implementation lives in posix_win.c.
 */
#ifndef LIBPATHIME_COMPAT_DIRENT_H
#define LIBPATHIME_COMPAT_DIRENT_H

#ifdef __cplusplus
extern "C" {
#endif

#define NAME_MAX 260  /* MAX_PATH; matches WIN32_FIND_DATAA::cFileName */

struct dirent {
    long d_ino;                 /* always 0 — Windows has no cheap inode */
    unsigned short d_reclen;
    char d_name[NAME_MAX + 1];
};

typedef struct LIBPATHIME_DIR DIR;

DIR           *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int            closedir(DIR *dirp);
void           rewinddir(DIR *dirp);

#ifdef __cplusplus
}
#endif

#endif /* LIBPATHIME_COMPAT_DIRENT_H */
