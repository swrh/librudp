/*
  Librudp, a reliable UDP transport library.

  This file is part of FOILS, the Freebox Open Interface
  Libraries. This file is distributed under a 2-clause BSD license,
  see LICENSE.TXT for details.

  Copyright (c) 2011, Freebox SAS
  See AUTHORS for details
 */

#ifndef RUDP_RUDP_H_
/** @hidden */
#define RUDP_RUDP_H_


#define RUDP_VERSION 0x01

/**
   @file
   @module {Master state}
   @short Master library state

   Master library state holds context used for administrative purposes
   of the library. It handles:
   @list
     @item Event loop abstraction handling (though libela)
     @item Memory management
     @item Logging
   @end list

   Master library state must be passed to all library objects
   initialization.

   A libela context must be initialized before the rudp context. See
   libela documentation for more information.

   When a handler is defined for logging, it gets all the messages
   from the library.  If user wants to filter messages, it can use the
   @tt level parameter. @see rudp_log_level.

   Memory allocation is handler through alloc/free-like functions.

   @see rudp_handler for functions to implement.

   Librudp provides sane defaults for the needed handlers, not
   reporting any log messages, and using @tt malloc and @tt free from
   the system's libc. Such handler is defined as @ref
   #RUDP_HANDLER_DEFAULT.

   Sample usage:
   @code
    struct rudp_base rudp;
    struct event_base *eb = ...;

    rudp_init(&rudp, eb, RUDP_HANDLER_DEFAULT);

    // allocate and initialize some objects

    // run your event loop

    // deinitialize and free the objects

    rudp_deinit(&rudp);
   @end code
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <rudp/compiler.h>
#include <rudp/error.h>
#include <rudp/list.h>
#include <rudp/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
   @this defines log levels of log messages.
 */
enum rudp_log_level
{
    RUDP_LOG_IO,
    RUDP_LOG_DEBUG,
    RUDP_LOG_INFO,
    RUDP_LOG_WARN,
    RUDP_LOG_ERROR,
};

struct rudp_base;

/**
   Master state handler code callbacks
 */
struct rudp_handler
{
    /**
       @this can be called anytime in the state life, to gather
       information about the library actions.

       Setting NULL in this callback is permitted to get no messages
       at all.

       @param rudp The rudp context
       @param level Log level of the message
       @param fmt Message printf-like format
       @param arg Argument list
     */
    void (*log)(
        struct rudp_base *rudp,
        enum rudp_log_level level, const char *fmt, va_list arg);

    /**
       @this is called each time the library needs memory.

       @param rudp The rudp context
       @param size Size of buffer needed
       @returns A newly allocated buffer, or NULL
     */
    void *(*mem_alloc)(struct rudp_base *rudp, size_t size);

    /**
       @this is called each time the library frees a previously
       allocated buffer.

       @param rudp The rudp context
       @param buffer Previously allocated buffer to release
     */
    void (*mem_free)(struct rudp_base *rudp, void *buffer);
};

extern RUDP_EXPORT const struct rudp_handler rudp_handler_default;

/**
   Can be used where default allocators and no logging output is
   needed for @ref rudp_init.
 */
#define RUDP_HANDLER_DEFAULT &rudp_handler_default

/**
   @this is a rudp context

   Rudp have the same @xref {olc} {life cycle} as most other complex
   objects of the library.  Before use, rudp contexts must be
   initialized with @ref rudp_init, and after use, they must be
   cleaned with @ref rudp_deinit.

   @hidecontent
 */
struct rudp_base
{
    struct rudp_handler handler;
    struct event_base *eb;
    struct rudp_list free_packet_list;
    uint16_t allocated_packets;
    uint16_t free_packets;
    struct {
        /** Minimum retransmission timeout. */
        rudp_time_t min_rto;
        /** Maximum retransmission timeout. */
        rudp_time_t max_rto;
        rudp_time_t action;
        rudp_time_t drop;
    } default_timeout;
};

/**
   @this initializes a master library state structure

   @param rudp Rudp context to initialize
   @param eb A valid event loop abstraction context
   @param handler A rudp handler descriptor. Use @ref
   #RUDP_HANDLER_DEFAULT if no specific behavior is intended.
 */
RUDP_EXPORT
void rudp_init(
    struct rudp_base *rudp,
    struct event_base *eb,
    const struct rudp_handler *handler);

/**
   @this initializes a master library state structure

   @param rudp Rudp context to deinitialize
 */
RUDP_EXPORT
void rudp_deinit(struct rudp_base *rudp);

RUDP_EXPORT
struct rudp_base *rudp_new(
    struct event_base *eb,
    const struct rudp_handler *handler);

RUDP_EXPORT
void rudp_free(struct rudp_base *rudp);

/**
   @this generates a 16 bit random value

   @param rudp Rudp context to deinitialize
 */
RUDP_EXPORT
uint16_t rudp_random(void);

#ifdef __cplusplus
}
#endif

#endif
