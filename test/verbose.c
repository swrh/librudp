/*
  Librudp, a reliable UDP transport library.

  This file is part of FOILS, the Freebox Open Interface
  Libraries. This file is distributed under a 2-clause BSD license,
  see LICENSE.TXT for details.

  Copyright (c) 2011, Freebox SAS
  See AUTHORS for details
 */

#include <stdio.h>
#include <stdlib.h>

#include <rudp/rudp.h>

static __inline
void _rudp_log_printf(
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

static
void *_rudp_default_alloc(struct rudp_base *rudp, size_t len)
{
    void *b = malloc(len);
    _rudp_log_printf(rudp, RUDP_LOG_DEBUG, "%s: %d bytes allocated at %p\n",
           __FUNCTION__, (int)len, b);
    return b;
}

static
void _rudp_default_free(struct rudp_base *rudp, void *buffer)
{
    _rudp_log_printf(rudp, RUDP_LOG_DEBUG, "%s: freed buffer at %p\n",
           __FUNCTION__, buffer);
    return free(buffer);
}

extern const char *__progname;

static
void _rudp_log(struct rudp_base *rudp,
               enum rudp_log_level level,
               const char *fmt,
               va_list arg)
{
    const char *level_str = "?";
    time_t ltime; /* calendar time */
    struct tm localtime_buf;
    char asctime_buf[26], *time_str, *p;
    char line_buf[1024];

    ltime = time(NULL); /* get current cal time */
    time_str = asctime_r(localtime_r(&ltime, &localtime_buf), asctime_buf);
    if (time_str != NULL) {
        p = strchr(time_str, '\n');
        if (p != NULL)
            *p = 0;
    }

    switch (level) {
    case RUDP_LOG_IO: level_str = "IO"; break;
    case RUDP_LOG_DEBUG: level_str = "DEBUG"; break;
    case RUDP_LOG_INFO: level_str = "INFO"; break;
    case RUDP_LOG_WARN: level_str = "WARN"; break;
    case RUDP_LOG_ERROR: level_str = "ERROR"; break;
    }

    line_buf[0] = 0;

    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC_RAW, &tp);

    snprintf(line_buf + strlen(line_buf), sizeof(line_buf) - strlen(line_buf), "%16llu.%09ld ", (unsigned long long int)tp.tv_sec, tp.tv_nsec);
    snprintf(line_buf + strlen(line_buf), sizeof(line_buf) - strlen(line_buf), "%16llu %26s %s %6s ", (unsigned long long int)ltime, __progname, time_str, level_str);
    vsnprintf(line_buf + strlen(line_buf), sizeof(line_buf) - strlen(line_buf), fmt, arg);

    fwrite(line_buf, strlen(line_buf), 1, stdout);
    fflush(stdout);
}

const struct rudp_handler verbose_handler =
{
    .log = _rudp_log,
    .mem_alloc = _rudp_default_alloc,
    .mem_free = _rudp_default_free,
};
