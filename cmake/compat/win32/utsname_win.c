/*
 * uname() for Windows, filling only the node name (all pyzy needs). Part of
 * libpathime's compat layer; only compiled on WIN32.
 */
#ifdef _WIN32

#include <sys/utsname.h>

#include <windows.h>
#include <string.h>

int
uname(struct utsname *buf)
{
    DWORD n = sizeof(buf->nodename);

    memset(buf, 0, sizeof(*buf));
    strcpy(buf->sysname, "Windows");
    if (!GetComputerNameA(buf->nodename, &n))
        buf->nodename[0] = '\0';
    return 0;
}

#endif /* _WIN32 */
