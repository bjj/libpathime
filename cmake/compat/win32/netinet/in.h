/*
 * Minimal <netinet/in.h> for Windows.
 *
 * anthy-unicode uses this header for one thing only: the byte-order helpers
 * (its on-disk dictionary format is big-endian, so every field goes through
 * ntohl/htonl). We implement them directly rather than including <winsock2.h>,
 * which would drag in the whole socket API and require linking ws2_32 — and
 * whose gethostname() clashes with the compat <unistd.h>.
 *
 * Every Windows target architecture (x86, x64, ARM64) is little-endian, so the
 * conversions are unconditional byte swaps.
 */
#ifndef LIBPATHIME_COMPAT_NETINET_IN_H
#define LIBPATHIME_COMPAT_NETINET_IN_H

#include <stdlib.h>  /* _byteswap_ushort, _byteswap_ulong */

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_)

static __inline unsigned short htons(unsigned short x) { return _byteswap_ushort(x); }
static __inline unsigned short ntohs(unsigned short x) { return _byteswap_ushort(x); }
static __inline unsigned int   htonl(unsigned int x)   { return _byteswap_ulong(x); }
static __inline unsigned int   ntohl(unsigned int x)   { return _byteswap_ulong(x); }

#endif

#ifdef __cplusplus
}
#endif

#endif /* LIBPATHIME_COMPAT_NETINET_IN_H */
