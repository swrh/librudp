/*
  Librudp, a reliable UDP transport library.

  This file is part of FOILS, the Freebox Open Interface
  Libraries. This file is distributed under a 2-clause BSD license,
  see LICENSE.TXT for details.

  Copyright (c) 2011, Freebox SAS
  See AUTHORS for details
 */

#include <string.h>
#include <errno.h>
#ifndef _MSC_VER
# include <unistd.h>
#endif

#include <event2/event.h>

#include <rudp/error.h>
#include <rudp/endpoint.h>
#include <rudp/packet.h>

#include "rudp_packet.h"

#ifdef _MSC_VER
#define RUDP_INVALID_SOCKET INVALID_SOCKET
#else
#define RUDP_INVALID_SOCKET -1
#endif

static void _endpoint_handle_incoming(evutil_socket_t fd, short flags,
        void *data);

void rudp_endpoint_init(
    struct rudp_endpoint *endpoint,
    struct rudp_base *rudp,
    const struct rudp_endpoint_handler *handler)
{
    rudp_address_init(&endpoint->addr, rudp);
    endpoint->socket_fd = RUDP_INVALID_SOCKET;
    endpoint->rudp = rudp;
    endpoint->handler = *handler;

    endpoint->ev = NULL;
}


void
rudp_endpoint_deinit(struct rudp_endpoint *endpoint)
{
    rudp_address_deinit(&endpoint->addr);

    if (endpoint->ev != NULL) {
        event_free(endpoint->ev);
        endpoint->ev = NULL;
    }
}

/*
  - socket watcher
     - endpoint packet reader <===
        - server/client packet handler
 */
static void
_endpoint_handle_incoming(evutil_socket_t fd, short flags, void *data)
{
    struct rudp_endpoint *endpoint = data;
    struct rudp_packet_chain *pc = rudp_packet_chain_alloc(
        endpoint->rudp, RUDP_RECV_BUFFER_SIZE);
    struct sockaddr_storage addr;

    rudp_error_t ret = rudp_endpoint_recv(
        endpoint, pc->packet, &pc->len, &addr);

    if (ret == 0)
        endpoint->handler.handle_packet(endpoint, &addr, pc);

    rudp_packet_chain_free(endpoint->rudp, pc);
}

rudp_error_t rudp_endpoint_bind(struct rudp_endpoint *endpoint)
{
    const struct sockaddr_storage *addr;
    socklen_t size;
    rudp_error_t err = rudp_address_get(&endpoint->addr, &addr, &size);
    evutil_socket_t skt;

    switch (err) {
    case 0:
        break;
    case EDESTADDRREQ:
        addr = NULL;
        break;
    default:
        return err;
    }

    skt = socket(addr ? addr->ss_family : AF_INET6, SOCK_DGRAM, 0);
    if (skt == RUDP_INVALID_SOCKET)
        return EVUTIL_SOCKET_ERROR();

    endpoint->socket_fd = skt;

    int ret = 0;

    if ( addr )
        ret = bind(endpoint->socket_fd,
                   (const struct sockaddr *)addr,
                   size);

    if ( ret == -1 ) {
        rudp_error_t e = errno;

        rudp_endpoint_close(endpoint);

        return e;
    }

    endpoint->ev = event_new(endpoint->rudp->eb, endpoint->socket_fd,
            EV_PERSIST|EV_READ, _endpoint_handle_incoming, endpoint);
    if (endpoint->ev == NULL) {
        rudp_endpoint_close(endpoint);
        return ENOMEM;
    }

    if (event_add(endpoint->ev, NULL) == -1) {
        rudp_endpoint_close(endpoint);
        return ECANCELED;
    }

    return 0;
}


void
rudp_endpoint_close(struct rudp_endpoint *endpoint)
{
    if (endpoint == NULL)
        return;

    if (endpoint->ev != NULL) {
        event_free(endpoint->ev);
        endpoint->ev = NULL;
    }

    if (endpoint->socket_fd != RUDP_INVALID_SOCKET) {
        evutil_closesocket(endpoint->socket_fd);
        endpoint->socket_fd = RUDP_INVALID_SOCKET;
    }
}

rudp_error_t rudp_endpoint_recv(struct rudp_endpoint *endpoint,
                                void *data, size_t *len,
                                struct sockaddr_storage *addr)
{
    int ret;

#ifdef _MSC_VER
    int slen = sizeof(*addr);
#else
    socklen_t slen = sizeof(*addr);
#endif

    ret = recvfrom(endpoint->socket_fd, data, (int)*len, 0,
                   (struct sockaddr *)addr, &slen);

    if ( ret == -1 )
        return errno;

    *len = ret;

    return 0;
}

rudp_error_t
rudp_endpoint_send(struct rudp_endpoint *endpoint,
        const struct rudp_address *addr,
        const void *data, size_t len)
{
    const struct sockaddr_storage *address;
    socklen_t size;

    if (endpoint == NULL)
        return EINVAL;

    rudp_error_t err = rudp_address_get(addr, &address, &size);
    if (err)
        return err;

    int ret = sendto(endpoint->socket_fd, data, (int)len, 0,
                     (const struct sockaddr *)address,
                     (int)size);

    if ( ret == -1 )
        return errno;

    return 0;
}

void rudp_endpoint_set_ipv4(
    struct rudp_endpoint *endpoint,
    const struct in_addr *address,
    const uint16_t port)
{
    rudp_address_set_ipv4(&endpoint->addr, address, port);
}

void rudp_endpoint_set_ipv6(
    struct rudp_endpoint *endpoint,
    const struct in6_addr *address,
    const uint16_t port)
{
    struct sockaddr_in6 addr6;

    memset(&addr6, 0, sizeof (addr6));
    addr6.sin6_family = AF_INET6;
    addr6.sin6_addr = *address;
    addr6.sin6_port = htons(port);

    rudp_address_set(&endpoint->addr, (struct sockaddr *)&addr6,
                     sizeof (addr6));
}

rudp_error_t rudp_endpoint_set_addr(
    struct rudp_endpoint *endpoint,
    const struct sockaddr *addr,
    socklen_t addrlen)
{
    return rudp_address_set(&endpoint->addr, addr, addrlen);
}

rudp_error_t rudp_endpoint_set_hostname(
    struct rudp_endpoint *endpoint,
    const char *hostname,
    const uint16_t port,
    uint32_t ip_flags)
{
    return rudp_address_set_hostname(&endpoint->addr,
                                     hostname, port, ip_flags);
}

int rudp_endpoint_address_compare(const struct rudp_endpoint *endpoint,
                                  const struct sockaddr_storage *addr)
{
    return rudp_address_compare(&endpoint->addr, addr);
}
