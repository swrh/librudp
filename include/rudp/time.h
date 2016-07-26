/*
  Librudp, a reliable UDP transport library.

  This file is part of FOILS, the Freebox Open Interface
  Libraries. This file is distributed under a 2-clause BSD license,
  see LICENSE.TXT for details.

  Copyright (c) 2011, Freebox SAS
  See AUTHORS for details
 */

#ifndef RUDP_TIME_H_
/** @hidden */
#define RUDP_TIME_H_

#include <event2/util.h>

/**
   @file
   @module{Time}
   @short Uniform time representation

   Time handling is done through the @ref rudp_time_t type. This type
   is a scalar and can be manipulated with usual scalar operations.

   The only functions declared for usage with @ref rudp_time_t are
   @ref rudp_timestamp and @ref rudp_timestamp_to_timeval.
*/

#include <time.h>
#include <stdint.h>
#ifndef _MSC_VER
# include <sys/time.h>
#endif
#include <stdlib.h>

/**
   @this is an abstract time type definition.  It contains miliseconds
   since a common time reference.
 */
typedef int64_t rudp_time_t;

#define RUDP_TIME_MAX INT64_MAX

/**
   @this retrieves the current library timestamp

   @returns a timestamp
 */
static __inline
rudp_time_t rudp_timestamp(void)
{
#if defined(_WIN32)
    return (rudp_time_t)GetTickCount64();
#elif defined(CLOCK_MONOTONIC_RAW)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == -1)
        return -1;
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#else
    struct timeval tv;
    evutil_gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

/**
   @this converts a milisecond value to a @tt {struct timeval}.

   @param tv (out) Timeval structure
   @param ts Timestamp
 */
static __inline
void rudp_timestamp_to_timeval(struct timeval *tv, rudp_time_t ts)
{
#ifdef _MSC_VER
    tv->tv_sec = (long)(ts / 1000);
#else
    tv->tv_sec = ts / 1000;
#endif
    tv->tv_usec = (ts % 1000) * 1000;
}

#endif
