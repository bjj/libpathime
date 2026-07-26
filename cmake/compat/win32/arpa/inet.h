/*
 * Minimal <arpa/inet.h> for Windows. anthy-unicode includes either this or
 * <netinet/in.h> depending on the file, but only ever wants the byte-order
 * helpers, so this is a thin alias.
 */
#ifndef LIBPATHIME_COMPAT_ARPA_INET_H
#define LIBPATHIME_COMPAT_ARPA_INET_H

#include <netinet/in.h>

#endif /* LIBPATHIME_COMPAT_ARPA_INET_H */
