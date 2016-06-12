/*
  Librudp, a reliable UDP transport library.

  This file is part of FOILS, the Freebox Open Interface
  Libraries. This file is distributed under a 2-clause BSD license,
  see LICENSE.TXT for details.

  Copyright (c) 2011, Freebox SAS
  See AUTHORS for details
 */

#include <err.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include <event2/event.h>

#include <rudp/rudp.h>
#include <rudp/server.h>

#define display_err(statement) \
    do { \
        rudp_error_t err = statement; \
        printf("%s:%d %s: %s\n", __FILE__, __LINE__, #statement, strerror(err)); \
    } while(0)

static void
handle_packet(struct rudp_server *server, struct rudp_peer *peer, int command,
        const void *data, size_t len, void *arg)
{
    printf("%s:%d %s\n", __FILE__, __LINE__, __FUNCTION__);
    printf(">>> command %d message '''", command);
    fwrite(data, 1, len, stdout);
    printf("'''\n");
    if ( !strncmp((const char *)data, "quit", 4) )
        event_base_loopbreak(server->rudp->eb);
}

static void
link_info(struct rudp_server *server, struct rudp_peer *peer,
        struct rudp_link_info *info, void *arg)
{
    printf("%s:%d %s\n", __FILE__, __LINE__, __FUNCTION__);
}

static void
peer_dropped(struct rudp_server *server, struct rudp_peer *peer, void *arg)
{
    printf("%s:%d %s\n", __FILE__, __LINE__, __FUNCTION__);
}

static void
peer_new(struct rudp_server *server, struct rudp_peer *peer, void *arg)
{
    printf("%s:%d %s\n", __FILE__, __LINE__, __FUNCTION__);
}

static const struct rudp_server_handler handler = {
    .handle_packet = handle_packet,
    .link_info = link_info,
    .peer_dropped = peer_dropped,
    .peer_new = peer_new,
};

static void
handle_stdin(evutil_socket_t fd, short events, void *param)
{
    struct rudp_server *server = param;
    char buffer[512], *tmp;

    tmp = fgets(buffer, 512, stdin);
    ssize_t size = strlen(tmp);
    rudp_server_send_all(server, 1, 0, tmp, size);
}

extern const struct rudp_handler verbose_handler;

int main(int argc, char **argv)
{
    struct rudp_server server;
    struct in_addr address;
    struct event_base *eb = event_base_new();
    struct event *ev;
    struct rudp_base rudp;
    const struct rudp_handler *my_handler = RUDP_HANDLER_DEFAULT;

    if ( argc > 1 && !strcmp(argv[1], "-v") )
        my_handler = &verbose_handler;

    rudp_init(&rudp, eb, my_handler);

    ev = event_new(eb, 0, EV_PERSIST|EV_READ, handle_stdin, &server);

    address.s_addr = INADDR_ANY;

    rudp_server_init(&server, &rudp, &handler, NULL);
    rudp_server_set_ipv4(&server, &address, 4242);
    display_err(  rudp_server_bind(&server)  );

    if (event_add(ev, NULL) == -1)
        err(EXIT_FAILURE, "event_add");

    if (event_base_loop(eb, 0) == -1)
        err(EXIT_FAILURE, "event_base_loop");

    event_free(ev);

    rudp_server_close(&server);
    rudp_server_deinit(&server);

    rudp_deinit(&rudp);
    event_base_free(eb);

    return 0;
}
