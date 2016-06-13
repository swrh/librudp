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
#include <rudp/client.h>

#define display_err(statement,r) \
    do { \
        rudp_error_t err = statement; \
        printf("%s:%d %s: %s\n", __FILE__, __LINE__, #statement, strerror(err)); \
        if (err) return r; \
    } while(0)

static void
handle_packet(struct rudp_client *client, int command, const void *data,
        size_t len, void *arg)
{
    printf("%s:%d %s\n", __FILE__, __LINE__, __FUNCTION__);
    printf(">>> command %d, message '''", command);
    fwrite(data, 1, len, stdout);
    printf("'''\n");
    if ( !strncmp((const char *)data, "quit", 4) )
        event_base_loopbreak(client->rudp->eb);
}

static void
link_info(struct rudp_client *client, struct rudp_link_info *info, void *arg)
{
    printf("%s:%d %s\n", __FILE__, __LINE__, __FUNCTION__);
}

static void
server_lost(struct rudp_client *client, void *arg)
{
    printf("%s:%d %s\n", __FILE__, __LINE__, __FUNCTION__);

    display_err(  rudp_client_connect(client),  );
}

static void
connected(struct rudp_client *client, void *arg)
{
    printf("%s:%d %s\n", __FILE__, __LINE__, __FUNCTION__);
}

static const struct rudp_client_handler handler = {
    .handle_packet = handle_packet,
    .link_info = link_info,
    .server_lost = server_lost,
    .connected = connected,
};

static void
handle_stdin(evutil_socket_t fd, short events, void *param)
{
    struct rudp_client *client = param;
    char buffer[512], *tmp;

    tmp = fgets(buffer, 512, stdin);
    ssize_t size = strlen(tmp);
    rudp_client_send(client, 1, 0, tmp, size);
}

extern const struct rudp_handler verbose_handler;

int main(int argc, char **argv)
{
    struct rudp_client client;
    struct event_base *eb = event_base_new();
    struct event *ev;
    struct rudp_base rudp;
    const struct rudp_handler *my_handler = RUDP_HANDLER_DEFAULT;
    const char *peer = "127.0.0.1";
    int peer_no = 1;

    if ( argc > 1 && !strcmp(argv[1], "-v") ) {
        my_handler = &verbose_handler;
        peer_no = 2;
    }

    if ( argc > peer_no )
        peer = argv[peer_no];

    rudp_init(&rudp, eb, my_handler);

    ev = event_new(eb, 0, EV_PERSIST|EV_READ, handle_stdin, &client);

    rudp_client_init(&client, &rudp, &handler, NULL);
    rudp_client_set_hostname(&client, peer, 4242, 0);
    display_err(  rudp_client_connect(&client) , 1);

    if (event_add(ev, NULL) == -1)
        err(EXIT_FAILURE, "event_add");

    if (event_base_loop(eb, 0) == -1)
        err(EXIT_FAILURE, "event_base_loop");

    event_free(ev);

    rudp_client_close(&client);
    rudp_client_deinit(&client);

    rudp_deinit(&rudp);
    event_base_free(eb);

    return 0;
}
