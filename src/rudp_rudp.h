/*
  Librudp, a reliable UDP transport library.

  This file is part of FOILS, the Freebox Open Interface
  Libraries. This file is distributed under a 2-clause BSD license,
  see LICENSE.TXT for details.

  Copyright (c) 2011, Freebox SAS
  See AUTHORS for details
 */

#ifndef RUDP_LOG_IMPL_H
#define RUDP_LOG_IMPL_H

#include <rudp/rudp.h>

#define RUDP_MAX(x, y) ((x) > (y) ? (x) : (y))
#define RUDP_MIN(x, y) ((x) < (y) ? (x) : (y))

static __inline
void rudp_log_printf(
    struct rudp_base *rudp,
    const enum rudp_log_level level,
    const char *fmt, ...)
{
    if (rudp == NULL || rudp->handler.log == NULL)
        return;

    va_list arg;
    va_start(arg, fmt);

    rudp->handler.log(rudp, level, fmt, arg);

    va_end(arg);
}

static __inline
void *rudp_mem_alloc(struct rudp_base *rudp, size_t len)
{
    return rudp->handler.mem_alloc(rudp, len);
}

static __inline
void rudp_mem_free(struct rudp_base *rudp, void *buffer)
{
    rudp->handler.mem_free(rudp, buffer);
}

#endif
