/*
 * Minimal <sys/mman.h> for Windows (MSVC/clang-cl).
 *
 * Part of libpathime's Windows compat layer. It is placed *ahead* of the
 * system include path on WIN32 so that the vendored anthy-unicode source
 * (src-diclib/filemap.c) resolves `#include <sys/mman.h>` to this shim.
 *
 * Only the surface filemap.c actually uses is provided: mmap()/munmap() with
 * PROT_READ|PROT_WRITE and MAP_SHARED. The implementation lives in mman.c.
 */
#ifndef LIBPATHIME_COMPAT_SYS_MMAN_H
#define LIBPATHIME_COMPAT_SYS_MMAN_H

#include <sys/types.h> /* off_t */
#include <stddef.h>    /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FAILED    ((void *) -1)

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int   munmap(void *addr, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* LIBPATHIME_COMPAT_SYS_MMAN_H */
