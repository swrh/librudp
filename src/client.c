/*
  Librudp, a reliable UDP transport library.

  This file is part of FOILS, the Freebox Open Interface
  Libraries. This file is distributed under a 2-clause BSD license,
  see LICENSE.TXT for details.

  Copyright (c) 2011, Freebox SAS
  See AUTHORS for details
 */

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <rudp/client.h>
#include <rudp/packet.h>
#include <rudp/peer.h>
#include <rudp/rudp.h>

#include "rudp_list.h"
#include "rudp_packet.h"

static const struct rudp_endpoint_handler client_endpoint_handler;
static const struct rudp_peer_handler client_peer_handler;

void
rudp_client_init(struct rudp_client *client, struct rudp_base *rudp,
        const struct rudp_client_handler *handler, void *arg)
{
    rudp_endpoint_init(&client->endpoint, rudp, &client_endpoint_handler);
    rudp_address_init(&client->address, rudp);
    client->handler = *handler;
    client->arg = arg;
    client->rudp = rudp;
    client->connected = 0;
    client->peer_valid = 0;
}

struct rudp_client *
rudp_client_new(struct rudp_base *rudp, const struct rudp_client_handler *handler,
        void *arg)
{
    struct rudp_client *client;

    client = rudp_mem_alloc(rudp, sizeof(struct rudp_client));
    if (client != NULL)
        rudp_client_init(client, rudp, handler, arg);
    return client;
}

rudp_error_t rudp_client_connect(struct rudp_client *client)
{
    const struct sockaddr_storage *addr;
    struct sockaddr bind_addr;
    socklen_t size;

    rudp_error_t err = rudp_address_get(&client->address, &addr, &size);
    if ( err )
        return err;

    rudp_peer_from_sockaddr(&client->peer, client->rudp,
                            addr,
                            &client_peer_handler, &client->endpoint);
    client->peer_valid = 1;

    rudp_peer_send_connect(&client->peer);

    memset(&bind_addr, 0, sizeof (bind_addr));
    bind_addr.sa_family = addr->ss_family;
    rudp_endpoint_set_addr(&client->endpoint, &bind_addr, sizeof (bind_addr));

    return rudp_endpoint_bind(&client->endpoint);
}

void
rudp_client_close(struct rudp_client *client)
{
    if (client == NULL || !client->peer_valid)
        return;
    rudp_peer_send_close_noqueue(&client->peer);
    rudp_peer_deinit(&client->peer);
    client->peer_valid = 0;
    rudp_endpoint_close(&client->endpoint);
}

void
rudp_client_deinit(struct rudp_client *client)
{
    rudp_client_close(client);
    rudp_address_deinit(&client->address);
    rudp_endpoint_deinit(&client->endpoint);
}

void
rudp_client_free(struct rudp_client *client)
{
    if (client == NULL)
        return;
    rudp_client_deinit(client);
    rudp_mem_free(client->rudp, client);
}

static
void client_handle_data_packet(
    struct rudp_peer *peer,
    struct rudp_packet_chain *pc)
{
    struct rudp_client *client = __container_of(peer, struct rudp_client *, peer);
    struct rudp_packet_data *header = &pc->packet->data;

    client->handler.handle_packet(
        client, header->header.command - RUDP_CMD_APP,
        header->data, pc->len - sizeof(header->header), client->arg);
}

static
void client_link_info(struct rudp_peer *peer, struct rudp_link_info *info)
{
    struct rudp_client *client = __container_of(peer, struct rudp_client *, peer);

    client->handler.link_info(client, info, client->arg);
}

static
void client_peer_dropped(struct rudp_peer *peer)
{
    struct rudp_client *client = __container_of(peer, struct rudp_client *, peer);

    client->connected = 0;

    rudp_peer_deinit(&client->peer);
    client->peer_valid = 0;
    rudp_endpoint_close(&client->endpoint);

    rudp_log_printf(peer->rudp, RUDP_LOG_INFO, "Peer dropped (server lost)\n");

    client->handler.server_lost(client, client->arg);
}

static const struct rudp_peer_handler client_peer_handler = {
    .handle_packet = client_handle_data_packet,
    .link_info = client_link_info,
    .dropped = client_peer_dropped,
};

/*
  - socket watcher
     - endpoint packet reader
        - client packet handler <===
           - peer packet handler
 */
static
void client_handle_endpoint_packet(struct rudp_endpoint *endpoint,
                                   const struct sockaddr_storage *addr,
                                   struct rudp_packet_chain *pc)
{
    struct rudp_client *client = __container_of(endpoint, struct rudp_client *, endpoint);

    rudp_log_printf(client->rudp, RUDP_LOG_INFO,
                    "Endpoint handling packet\n");

    rudp_error_t err = rudp_peer_incoming_packet(&client->peer, pc);
    if ( err == 0 && client->connected == 0 )
    {
        client->connected = 1;
        client->handler.connected(client, client->arg);
    }
}

static const struct rudp_endpoint_handler client_endpoint_handler = {
    .handle_packet = client_handle_endpoint_packet,
};

rudp_error_t rudp_client_send(
    struct rudp_client *client,
    int reliable, int command,
    const void *data,
    const size_t size)
{
    if (client == NULL || !client->connected)
        return EINVAL;

    return rudp_peer_send(client->rudp, &client->peer, reliable, command, data, size);
}

rudp_error_t rudp_client_set_hostname(
    struct rudp_client *client,
    const char *hostname,
    const uint16_t port,
    uint32_t ip_flags)
{
    if (client == NULL || hostname == NULL || port <= 0)
        return EINVAL;

    return rudp_address_set_hostname(&client->address,
                                      hostname, port, ip_flags);
}

rudp_error_t rudp_client_set_ipv4(
    struct rudp_client *client,
    const struct in_addr *address,
    const uint16_t port)
{
    if (client == NULL || address == NULL || port <= 0)
        return EINVAL;

    rudp_address_set_ipv4(&client->address, address, port);

    return 0;
}

rudp_error_t rudp_client_set_ipv6(
    struct rudp_client *client,
    const struct in6_addr *address,
    const uint16_t port)
{
    struct sockaddr_in6 addr6;

    if (client == NULL || address == NULL || port <= 0)
        return EINVAL;

    memset(&addr6, 0, sizeof (addr6));
    addr6.sin6_family = AF_INET6;
    addr6.sin6_addr = *address;
    addr6.sin6_port = htons(port);

    return rudp_address_set(&client->address, (struct sockaddr *)&addr6, sizeof(addr6));
}

rudp_error_t rudp_client_set_addr(
    struct rudp_client *client,
    const struct sockaddr *addr,
    socklen_t addrlen)
{
    return rudp_address_set(&client->address, addr, addrlen);
}
