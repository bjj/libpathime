/*
 * Windows mmap()/munmap() over file-mapping objects, for libpathime's compat
 * layer. Backs the anthy-unicode dictionary loader (src-diclib/filemap.c),
 * which maps a whole file at offset 0 with MAP_SHARED.
 *
 * Only compiled on WIN32 (see cmake/LibpathimeCompat.cmake).
 */
#ifdef _WIN32

#include <sys/mman.h>

#include <io.h>       /* _get_osfhandle */
#include <windows.h>

void *
mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    HANDLE fh, mapping;
    DWORD  page_prot, view_access;
    void  *view;

    (void) addr;
    (void) flags; /* filemap.c only ever passes MAP_SHARED */

    fh = (HANDLE) _get_osfhandle(fd);
    if (fh == INVALID_HANDLE_VALUE)
        return MAP_FAILED;

    if (prot & PROT_WRITE) {
        page_prot   = PAGE_READWRITE;
        view_access = FILE_MAP_WRITE; /* implies read */
    } else {
        page_prot   = PAGE_READONLY;
        view_access = FILE_MAP_READ;
    }

    mapping = CreateFileMapping(fh, NULL, page_prot, 0, 0, NULL);
    if (mapping == NULL)
        return MAP_FAILED;

    view = MapViewOfFile(mapping, view_access,
                         (DWORD)((offset >> 32) & 0xFFFFFFFF),
                         (DWORD)(offset & 0xFFFFFFFF),
                         length);

    /* The view keeps the mapping alive after the handle is closed, so we can
     * drop it now and let munmap() need nothing but the base address. */
    CloseHandle(mapping);

    return view ? view : MAP_FAILED;
}

int
munmap(void *addr, size_t length)
{
    (void) length;
    return UnmapViewOfFile(addr) ? 0 : -1;
}

#endif /* _WIN32 */
