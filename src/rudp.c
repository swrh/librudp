/*
  Librudp, a reliable UDP transport library.

  This file is part of FOILS, the Freebox Open Interface
  Libraries. This file is distributed under a 2-clause BSD license,
  see LICENSE.TXT for details.

  Copyright (c) 2011, Freebox SAS
  See AUTHORS for details
 */

#include <stdlib.h>

#include <event2/util.h>

#include <rudp/rudp.h>
#include <rudp/packet.h>
#include <rudp/time.h>

#include "rudp_list.h"
#include "rudp_rudp.h"

void rudp_init(
    struct rudp *rudp,
    struct event_base *eb,
    const struct rudp_handler *handler)
{
    rudp->handler = handler;
    rudp->eb = eb;

    rudp_list_init(&rudp->free_packet_list);
    rudp->free_packets = 0;
    rudp->allocated_packets = 0;
}

static
void *_rudp_default_alloc(struct rudp *rudp, size_t len)
{
    return calloc(1, len);
}

static
void _rudp_default_free(struct rudp *rudp, void *buffer)
{
    free(buffer);
}

RUDP_EXPORT
const struct rudp_handler rudp_handler_default =
{
    .log = NULL,
    .mem_alloc = _rudp_default_alloc,
    .mem_free = _rudp_default_free,
};

void rudp_deinit(struct rudp *rudp)
{
    struct rudp_packet_chain *pc, *tmp;
    rudp_list_for_each_safe(struct rudp_packet_chain *, pc, tmp, &rudp->free_packet_list, chain_item) {
        rudp_list_remove(&pc->chain_item);
        rudp_mem_free(rudp, pc);
    }
}

struct rudp *
rudp_new(struct event_base *eb, const struct rudp_handler *handler)
{
    struct rudp *rudp;

    if (handler == NULL)
        handler = &rudp_handler_default;

    rudp = handler->mem_alloc(NULL, sizeof(struct rudp));
    if (rudp == NULL)
        return NULL;

    rudp_init(rudp, eb, handler);

    return rudp;
}

void
rudp_free(struct rudp *rudp)
{
    if (rudp != NULL)
        rudp->handler->mem_free(rudp, rudp);
}

uint16_t rudp_random(void)
{
    uint16_t r;

    evutil_secure_rng_get_bytes(&r, sizeof(r));

    return r;
}
