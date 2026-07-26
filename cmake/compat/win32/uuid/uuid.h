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

/* The Windows SDK's <rpcdce.h> (pulled in by <windows.h>) does
 *     #ifndef UUID_DEFINED
 *     #define UUID_DEFINED
 *     typedef GUID UUID;
 *     #ifndef uuid_t
 *     #define uuid_t UUID
 *     #endif
 *     #endif
 * A plain `typedef unsigned char uuid_t[16]` here does not satisfy that
 * `#ifndef`, so rpcdce.h would macro-rewrite every later `uuid_t` to `UUID`.
 * Declare the real type under its own name and expose `uuid_t` as a macro, so
 * rpcdce.h stands down and the libuuid meaning wins whichever header comes
 * first. */
typedef unsigned char libpathime_uuid_t[16];

#ifdef uuid_t
#undef uuid_t
#endif
#define uuid_t libpathime_uuid_t

void uuid_generate(libpathime_uuid_t out);
void uuid_unparse_lower(const libpathime_uuid_t uu, char *out); /* out: >= 37 bytes */
void uuid_unparse(const libpathime_uuid_t uu, char *out);

#ifdef __cplusplus
}
#endif

#endif /* LIBPATHIME_COMPAT_UUID_UUID_H */
