/*
 * Minimal <uuid/uuid.h> (libuuid API) for Windows.
 *
 * pyzy's src/Util.h uses the e2fsprogs libuuid path (HAVE_LIBUUID):
 *     uuid_t u; uuid_generate(u); uuid_unparse_lower(u, buf);
 * We provide just those two calls, implemented over the Win32 RPC UuidCreate
 * (see uuid_win.c, linked from Rpcrt4). Placed ahead of the system include
 * path on WIN32 so pyzy resolves `#include <uuid/uuid.h>` to this shim.
 */
#ifndef LIBPATHIME_COMPAT_UUID_UUID_H
#define LIBPATHIME_COMPAT_UUID_UUID_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char uuid_t[16];

void uuid_generate(uuid_t out);
void uuid_unparse_lower(const uuid_t uu, char *out); /* out: >= 37 bytes */
void uuid_unparse(const uuid_t uu, char *out);

#ifdef __cplusplus
}
#endif

#endif /* LIBPATHIME_COMPAT_UUID_UUID_H */
