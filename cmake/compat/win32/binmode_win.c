/*
 * Force the CRT's default file-translation mode to binary.
 *
 * anthy's build-time codegen tools write and read their intermediates with
 * plain fopen(fn, "w") / fopen(fn, "r") — mkdepgraph's anthy.dep, mkfiledic's
 * anthy.dic and the section files it concatenates — even though the contents
 * are raw little-endian-swapped binary. Under the CRT's default text mode that
 * silently rewrites every 0x0A on the way out and stops reading at the first
 * 0x1A on the way in, so the dictionary comes out corrupt.
 *
 * This is deliberately *not* part of libpathime_win32compat: _fmode is
 * process-global state, and a library has no business changing it under its
 * host. It is compiled straight into the codegen executables instead, which
 * own their process. (Consequence: the anthy *runtime* library still opens its
 * private-dictionary files in text mode on Windows — see
 * docs/windows-port.md.)
 *
 * Only compiled on WIN32.
 */
#ifdef _WIN32

#include <fcntl.h>
#include <stdlib.h>

static int __cdecl
libpathime_set_binary_mode(void)
{
    _set_fmode(_O_BINARY);
    return 0;
}

/* .CRT$XIU is the slot the CRT reserves for user C initialisers: it runs after
 * the CRT is up and before main(), so nothing has opened a file yet. */
#pragma section(".CRT$XIU", long, read)
__declspec(allocate(".CRT$XIU"))
int (__cdecl *libpathime_binmode_init)(void) = libpathime_set_binary_mode;

#endif /* _WIN32 */
