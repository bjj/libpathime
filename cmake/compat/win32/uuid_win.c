/*
 * libuuid-compatible uuid_generate()/uuid_unparse*() for Windows, backed by
 * the RPC runtime's UuidCreate (Rpcrt4). Part of libpathime's compat layer;
 * only compiled on WIN32 (see cmake/LibpathimeCompat.cmake).
 */
#ifdef _WIN32

#include <uuid/uuid.h>

#include <windows.h>
#include <rpc.h>
#include <stdio.h>
#include <string.h>

void
uuid_generate(libpathime_uuid_t out)
{
    UUID u;
    /* UuidCreate lays the 128 bits out with mixed endianness across its
     * fields; libuuid callers only need a well-distributed 16-byte value, so
     * copy the raw struct bytes. */
    UuidCreate(&u);
    memcpy(out, &u, sizeof(u));
}

static void
uuid_format(const libpathime_uuid_t uu, char *out, const char *fmt)
{
    sprintf(out,
            fmt,
            uu[0], uu[1], uu[2], uu[3], uu[4], uu[5], uu[6], uu[7],
            uu[8], uu[9], uu[10], uu[11], uu[12], uu[13], uu[14], uu[15]);
}

void
uuid_unparse_lower(const libpathime_uuid_t uu, char *out)
{
    uuid_format(uu, out,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x");
}

void
uuid_unparse(const libpathime_uuid_t uu, char *out)
{
    uuid_unparse_lower(uu, out);
}

#endif /* _WIN32 */
