/*
 * Minimal <sys/time.h> for Windows.
 *
 * anthy-unicode's src-util/input.c includes it as the POSIX home of time();
 * gettimeofday()/struct timeval are provided too so the header is honest about
 * its name. struct timeval is guarded the same way <winsock2.h> guards it, so
 * including both in one TU is safe in either order.
 */
#ifndef LIBPATHIME_COMPAT_SYS_TIME_H
#define LIBPATHIME_COMPAT_SYS_TIME_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* <winsock.h> (pulled in by <windows.h>) defines struct timeval outright, with
 * no guard macro of its own — so key off the winsock include guards as well. */
#if !defined(_TIMEVAL_DEFINED) && !defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_)
#define _TIMEVAL_DEFINED
struct timeval {
    long tv_sec;
    long tv_usec;
};
#endif

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

int gettimeofday(struct timeval *tv, struct timezone *tz);

#ifdef __cplusplus
}
#endif

#endif /* LIBPATHIME_COMPAT_SYS_TIME_H */
