/*
  Librudp, a reliable UDP transport library.

  This file is part of FOILS, the Freebox Open Interface
  Libraries. This file is distributed under a 2-clause BSD license,
  see LICENSE.TXT for details.

  Copyright (c) 2011, Freebox SAS
  See AUTHORS for details
 */

#define _BSD_SOURCE

#ifndef _MSC_VER
# include <sys/time.h>
#endif
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <event2/event.h>

#include <rudp/address.h>
#include <rudp/endpoint.h>
#include <rudp/peer.h>
#include <rudp/rudp.h>

#include "rudp_list.h"
#include "rudp_packet.h"

#define CLOCK_GRANULARITY 1000

/* Declarations */

static void peer_post_ack(struct rudp_peer *peer);
static rudp_error_t peer_send_raw(
    struct rudp_peer *peer,
    const void *data, size_t len);
static int peer_handle_ack(struct rudp_peer *peer, uint16_t ack);

static void peer_service(struct rudp_peer *peer);
static void _peer_service(evutil_socket_t fd, short flags, void *arg);
static int peer_service_schedule(struct rudp_peer *peer);
static void rudp_peer_handle_segment(
    struct rudp_peer *peer,
    const struct rudp_packet_header *header,
    struct rudp_packet_chain *pc);

enum peer_state
{
    PEER_NEW,
    PEER_RUN,
    PEER_CONNECTING,
    PEER_DEAD,
};

/* Object management */

void
rudp_peer_reset(struct rudp_peer *peer)
{
    struct rudp_packet_chain *pc, *tmp;

    if (peer == NULL)
        return;

    if (peer->sendq.next != NULL) {
        rudp_list_for_each_safe(struct rudp_packet_chain *, pc, tmp, &peer->sendq, chain_item) {
            rudp_list_remove(&pc->chain_item);
            rudp_packet_chain_free(peer->rudp, pc);
        }
    }

    if (peer->ev != NULL)
        evtimer_del(peer->ev);

    peer->abs_timeout_deadline = rudp_timestamp() + peer->timeout.drop;
    peer->in_seq_reliable = (uint16_t)-1;
    peer->in_seq_unreliable = 0;
    peer->out_seq_reliable = rudp_random();
    peer->out_seq_unreliable = 0;
    peer->out_seq_acked = peer->out_seq_reliable - 1;
    peer->state = PEER_NEW;
    peer->last_out_time = rudp_timestamp();
    peer->srtt = -1;
    peer->rttvar = -1;
    peer->rto = peer->timeout.min_rto;
    peer->must_ack = 0;
    peer->sendto_err = 0;
}

void rudp_peer_init(
    struct rudp_peer *peer,
    struct rudp_base *rudp,
    const struct rudp_peer_handler *handler,
    struct rudp_endpoint *endpoint)
{
    rudp_list_init(&peer->sendq);
    peer->segments = NULL;
    rudp_address_init(&peer->address, rudp);
    peer->endpoint = endpoint;
    peer->rudp = rudp;
    peer->handler = *handler;
    peer->ev = evtimer_new(rudp->eb, _peer_service, peer);

    peer->timeout.min_rto = rudp->default_timeout.min_rto;
    peer->timeout.max_rto = rudp->default_timeout.max_rto;
    peer->timeout.drop = rudp->default_timeout.drop;
    peer->timeout.action = rudp->default_timeout.action;

    rudp_peer_reset(peer);

    peer_service_schedule(peer);
}

struct rudp_peer *
rudp_peer_new(struct rudp_base *rudp, const struct rudp_peer_handler *handler,
        struct rudp_endpoint *endpoint)
{
    struct rudp_peer *peer;

    peer = rudp_mem_alloc(rudp, sizeof(struct rudp_peer));
    if (peer != NULL)
        rudp_peer_init(peer, rudp, handler, endpoint);
    return peer;
}

void
rudp_peer_deinit(struct rudp_peer *peer)
{
    if (peer == NULL)
        return;

    rudp_peer_reset(peer);
    rudp_address_deinit(&peer->address);

    if (peer->segments != NULL)
        rudp_packet_chain_free(peer->rudp, peer->segments);
    peer->segments = NULL;

    if (peer->ev != NULL) {
        event_free(peer->ev);
        peer->ev = NULL;
    }

    peer->rudp = NULL;
}

void
rudp_peer_free(struct rudp_peer *peer)
{
    if (peer == NULL)
        return;
    rudp_peer_deinit(peer);
    rudp_mem_free(peer->rudp, peer);
}

static void
peer_update_rtt(struct rudp_peer *peer, rudp_time_t last_rtt)
{
    /* Invalid RTT. */
    if (last_rtt <= 0)
        return;

    if (peer->srtt == -1) {
        /* RFC 6298 2.2 */
        peer->srtt = last_rtt;
        peer->rttvar = last_rtt / 2;
        peer->rto = peer->srtt + RUDP_MAX(CLOCK_GRANULARITY, 4 * peer->rttvar);
    } else {
        /* RFC 6298 2.3 - Alpha is 1/8 and beta 1/4, both hardcoded for now. */
        peer->rttvar = (3 * peer->rttvar + labs((long)(peer->srtt - last_rtt))) / 4;
        peer->srtt = (7 * peer->srtt + last_rtt) / 8;
        peer->rto = peer->srtt + RUDP_MAX(CLOCK_GRANULARITY, 4 * peer->rttvar);
    }

    /* RFC 6298 2.4 */
    peer->rto = RUDP_MAX(peer->rto, peer->timeout.min_rto);

    /* RFC 6298 2.5 */
    peer->rto = RUDP_MIN(peer->rto, peer->timeout.max_rto);

    rudp_log_printf(peer->rudp, RUDP_LOG_INFO,
                    "Timeout state: rttvar %d srtt %d rto %d\n",
                    (int)peer->rttvar, (int)peer->srtt, (int)peer->rto);
}

static void
peer_rto_backoff(struct rudp_peer *peer)
{
    /* RFC 6298 5.5 */
    peer->rto = RUDP_MAX(peer->rto * 2, peer->timeout.max_rto);

    rudp_log_printf(peer->rudp, RUDP_LOG_INFO,
                    "Timeout state: rttvar %d srtt %d rto %d\n",
                    (int)peer->rttvar, (int)peer->srtt, (int)peer->rto);
}

void rudp_peer_from_sockaddr(
    struct rudp_peer *peer,
    struct rudp_base *rudp,
    const struct sockaddr_storage *addr,
    const struct rudp_peer_handler *handler,
    struct rudp_endpoint *endpoint)
{
    rudp_peer_init(peer, rudp, handler, endpoint);
    rudp_address_set(&peer->address, (struct sockaddr *) addr, sizeof (*addr));
}

/* Sync handling */

enum packet_state
{
    SEQUENCED,
    UNSEQUENCED,
    RETRANSMITTED,
};

static
enum packet_state peer_analyse_reliable(
    struct rudp_peer *peer,
    uint16_t reliable_seq)
{
    if ( peer->in_seq_reliable == reliable_seq )
        return RETRANSMITTED;

    if ( (uint16_t)(peer->in_seq_reliable + 1) != reliable_seq ) {
        if (peer->state != PEER_NEW || peer->in_seq_reliable != (uint16_t)-1) {
            rudp_log_printf(peer->rudp, RUDP_LOG_WARN,
                            "%s unsequenced last seq %04x packet %04x\n",
                            __FUNCTION__, peer->in_seq_reliable, reliable_seq);
        }
        return UNSEQUENCED;
    }

    peer->in_seq_reliable = reliable_seq;
    peer->in_seq_unreliable = 0;

    return SEQUENCED;
}

static
enum packet_state peer_analyse_unreliable(
    struct rudp_peer *peer,
    uint16_t reliable_seq,
    uint16_t unreliable_seq)
{
    rudp_log_printf(peer->rudp, RUDP_LOG_IO,
                    "%s rel %04x == %04x, unrel %04x >= %04x\n",
                    __FUNCTION__,
                    peer->in_seq_reliable, reliable_seq,
                    unreliable_seq, peer->in_seq_unreliable);

    if ( peer->in_seq_reliable != reliable_seq )
        return UNSEQUENCED;

    int16_t unreliable_delta = unreliable_seq - peer->in_seq_unreliable;

    if ( unreliable_delta <= 0 )
        return UNSEQUENCED;

    peer->in_seq_unreliable = unreliable_seq;

    return SEQUENCED;
}

/* Send function */

static void peer_ping(struct rudp_peer *peer)
{
    struct rudp_packet_chain *pc = rudp_packet_chain_alloc(
        peer->rudp,
        sizeof(struct rudp_packet_header) + sizeof(rudp_time_t)
        );
    struct rudp_packet_data *data = &pc->packet->data;

    rudp_log_printf(peer->rudp, RUDP_LOG_DEBUG,
                    "%s pushing PING\n", __FUNCTION__);

    data->header.command = RUDP_CMD_PING;

    rudp_time_t timestamp = rudp_timestamp();
    memcpy(data->data, &timestamp, sizeof(timestamp));

    rudp_peer_send_reliable(peer, pc);
}

/* Receiver functions */

static
void peer_handle_ping(
    struct rudp_peer *peer,
    const struct rudp_packet_chain *in)
{
    /*
      We cant take RTT stats from retransmitted packets.
      Generic calling code still generates an ACK.
    */
    if ( in->packet->header.opt & RUDP_OPT_RETRANSMITTED )
        return;

    struct rudp_packet_chain *out =
        rudp_packet_chain_alloc(peer->rudp, in->len);
    struct rudp_packet_header *header = &out->packet->header;

    header->command = RUDP_CMD_PONG;
    header->opt = 0;
    header->version = RUDP_VERSION;

    rudp_log_printf(peer->rudp, RUDP_LOG_DEBUG,
                    "%s answering to ping\n", __FUNCTION__);

    memcpy(&out->packet->data.data[0],
           &in->packet->data.data[0],
           in->len - sizeof(struct rudp_packet_header));

    rudp_peer_send_unreliable(peer, out);
}

static
void peer_handle_pong(
    struct rudp_peer *peer,
    const struct rudp_packet_chain *pc)
{
    rudp_time_t orig, delta;
    memcpy(&orig, pc->packet->data.data, sizeof(orig));

    delta = rudp_timestamp() - orig;

    peer_update_rtt(peer, delta);
}

static int
peer_service_schedule(struct rudp_peer *peer)
{
    if (peer == NULL || peer->ev == NULL)
        return EINVAL;

    rudp_time_t timestamp = rudp_timestamp();

    // If nothing in sendq: reschedule service for later
    rudp_time_t delta = peer->timeout.action;

    // just abuse for_each to get head, if it exists
    struct rudp_packet_chain *head;
    rudp_list_for_each(struct rudp_packet_chain *, head, &peer->sendq, chain_item)
    {
        struct rudp_packet_header *header = &head->packet->header;

        if ( header->opt & RUDP_OPT_RETRANSMITTED )
            // already transmitted head, wait for rto
            delta = timestamp - peer->last_out_time + peer->rto;
        else
            // transmit asap
            delta = 0;

        // We dont really want to iterate after head
        break;
    }

    delta = RUDP_MAX(RUDP_MIN(delta, peer->abs_timeout_deadline - timestamp), 0);

    rudp_log_printf(peer->rudp, RUDP_LOG_DEBUG,
                    "%s:%d Idle, service scheduled for %d\n",
                    __FUNCTION__, __LINE__,
                    (int)delta);

    struct timeval tv;
    rudp_timestamp_to_timeval(&tv, delta);

    /* Avoid double evtimer_add(). */
    evtimer_del(peer->ev);
    evtimer_add(peer->ev, &tv);

    return 0;
}

static
void peer_handle_connreq(
    struct rudp_peer *peer,
    const struct rudp_packet_header *header)
{
    struct rudp_packet_chain *pc =
        rudp_packet_chain_alloc(peer->rudp,
                                sizeof(struct rudp_packet_conn_rsp));
    struct rudp_packet_conn_rsp *response = &pc->packet->conn_rsp;

    memset(response, 0, sizeof(struct rudp_packet_conn_rsp));
    response->header.version = RUDP_VERSION;
    response->header.command = RUDP_CMD_CONN_RSP;
    response->header.segments_size = htons(1);
    response->accepted = htonl(1);

    rudp_log_printf(peer->rudp, RUDP_LOG_INFO,
                    "%s answering to connreq\n", __FUNCTION__);

    rudp_peer_send_unreliable(peer, pc);
}

/*
 * Accumulate segments until one splitted message fully arrives,
 * then dispatch the callbacks
 */

static void rudp_peer_handle_segment(
    struct rudp_peer *peer,
    const struct rudp_packet_header *header,
    struct rudp_packet_chain *pc)
{
    uint16_t segments_size, segment_index;

    segment_index = ntohs(header->segment_index);
    segments_size = ntohs(header->segments_size);

    if (segments_size == 1) {
        peer->handler.handle_packet(peer, pc);
        return;
    }

    if (segment_index == 0) {
        if (peer->segments != NULL)
            rudp_mem_free(peer->rudp, peer->segments);

        peer->segments = rudp_packet_chain_alloc(peer->rudp, segments_size * RUDP_RECV_BUFFER_SIZE);
        peer->segments->len = 0;
        peer->segments->packet->header.opt = header->opt;
        peer->segments->packet->header.command = header->command;
    }

    memcpy(&peer->segments->packet->data.data[0] + peer->segments->len,
            &pc->packet->data.data[0],
            pc->len - sizeof(*header));

    peer->segments->len += pc->len - sizeof(*header);

    if ((segment_index + 1) == segments_size) {
        peer->segments->len += sizeof(*header);
        peer->handler.handle_packet(peer, peer->segments);
        rudp_packet_chain_free(peer->rudp, peer->segments);
        peer->segments = NULL;
    }
}

/*
  - socket watcher
     - endpoint packet reader
        - server packet handler
           - peer packet handler <===
 */
rudp_error_t rudp_peer_incoming_packet(
    struct rudp_peer *peer, struct rudp_packet_chain *pc)
{
    const struct rudp_packet_header *header = &pc->packet->header;
    struct rudp_base *rudp;

    rudp_log_printf(peer->rudp, RUDP_LOG_IO,
                    "<<< incoming [%d] %s %s (%d) %04x:%04x\n",
                    peer->state,
                    (header->opt & RUDP_OPT_RELIABLE)
                        ? "reliable" : "unreliable",
                    rudp_command_name(header->command), header->command,
                    ntohs(header->reliable), ntohs(header->unreliable));

    if ( header->opt & RUDP_OPT_ACK ) {
        rudp_log_printf(peer->rudp, RUDP_LOG_IO,
                        "    has ACK flag, %04x\n",
                        (int)ntohs(header->reliable_ack));
        int broken = peer_handle_ack(peer, ntohs(header->reliable_ack));
        if ( broken ) {
            rudp_log_printf(peer->rudp, RUDP_LOG_WARN,
                            "    broken ACK flag, ignoring packet\n");
            return EINVAL;
        }
    }

    enum packet_state state;

    if ( header->opt & RUDP_OPT_RELIABLE )
        state = peer_analyse_reliable(peer, ntohs(header->reliable));
    else
        state = peer_analyse_unreliable(peer, ntohs(header->reliable),
                                        ntohs(header->unreliable));

    switch ( state ) {
    case UNSEQUENCED:
        if (peer->state == PEER_NEW
            && header->command == RUDP_CMD_CONN_REQ) {
            // Server side, handling new client
            peer_handle_connreq(peer, header);
            peer->in_seq_reliable = ntohs(header->reliable);
            peer->state = PEER_RUN;
        } else if (peer->state == PEER_CONNECTING
                   && header->command == RUDP_CMD_CONN_RSP) {
            // Client side, handling new server
            peer->in_seq_reliable = ntohs(header->reliable);
            peer_handle_ack(peer, ntohs(header->reliable_ack));
            peer->state = PEER_RUN;
        } else {
            rudp_log_printf(peer->rudp, RUDP_LOG_WARN,
                            "    unsequenced packet in state %d, ignored\n",
                            peer->state);
        }
        break;

    case RETRANSMITTED:
        peer->abs_timeout_deadline = rudp_timestamp() + peer->timeout.drop;
        break;

    case SEQUENCED:
        peer->abs_timeout_deadline = rudp_timestamp() + peer->timeout.drop;

        switch ( header->command )
        {
        case RUDP_CMD_CLOSE:
            /* Save "rudp" here because "peer" might be freed at the dropped()
             * handler (server). */
            rudp = peer->rudp;
            peer->state = PEER_DEAD;
            peer->handler.dropped(peer);
            rudp_log_printf(rudp, RUDP_LOG_INFO,
                            "      peer dropped\n");
            return 0;

            break;

        case RUDP_CMD_PING:
            if ( peer->state == PEER_RUN ) {
                rudp_log_printf(peer->rudp, RUDP_LOG_DEBUG,
                                "       ping\n");
                peer_handle_ping(peer, pc);
            } else {
                rudp_log_printf(peer->rudp, RUDP_LOG_WARN,
                                "       ping while not running\n");
            }
            break;

        case RUDP_CMD_PONG:
            if ( peer->state == PEER_RUN ) {
                rudp_log_printf(peer->rudp, RUDP_LOG_DEBUG,
                                "       pong\n");
                peer_handle_pong(peer, pc);
            } else {
                rudp_log_printf(peer->rudp, RUDP_LOG_WARN,
                                "       pong while not running\n");
            }
            break;

        case RUDP_CMD_NOOP:
        case RUDP_CMD_CONN_REQ:
        case RUDP_CMD_CONN_RSP:
             break;

        default:
            if ( peer->state != PEER_RUN ) {
                rudp_log_printf(peer->rudp, RUDP_LOG_WARN,
                                "       user payload while not running\n");
                break;
            }

            if ( header->command >= RUDP_CMD_APP ){
                rudp_peer_handle_segment(peer,header,pc);
            }
        }
    }

    if ( header->opt & RUDP_OPT_RELIABLE ) {
        rudp_log_printf(peer->rudp, RUDP_LOG_DEBUG,
                        "       reliable packet, posting ack\n");
        peer_post_ack(peer);
    }

    return peer_service_schedule(peer);
}


/* Ack handling function */


static
int peer_handle_ack(struct rudp_peer *peer, uint16_t ack)
{
    struct rudp_link_info link_info;
    int16_t ack_delta = (ack - peer->out_seq_acked);
    int16_t adv_delta = (ack - peer->out_seq_reliable);

    if ( ack_delta < 0 )
        // ack in past
        return 0;

    if ( adv_delta > 0 )
        // packet acking an unsent seq no -- broken packet
        return 1;

    rudp_log_printf(peer->rudp, RUDP_LOG_DEBUG,
                    "%s acked seqno is now %04x\n", __FUNCTION__, ack);

    peer->out_seq_acked = ack;

    struct rudp_packet_chain *pc, *tmp;
    rudp_list_for_each_safe(struct rudp_packet_chain *, pc, tmp, &peer->sendq, chain_item)
    {
        struct rudp_packet_header *header = &pc->packet->header;
        uint16_t seqno = ntohs(header->reliable);
        int16_t delta = (seqno - ack);

        // not transmitted yet:
        // - unreliable packets, if they are still here
        // - reliable packet not marked retransmitted
        if ( ! (header->opt & RUDP_OPT_RELIABLE)
             || ! (header->opt & RUDP_OPT_RETRANSMITTED) )
            break;

        rudp_log_printf(peer->rudp, RUDP_LOG_DEBUG,
                        "%s (ack=%04x) considering unqueueing"
                        " packet id %04x, delta %d: %s\n",
                        __FUNCTION__,
                        ack, seqno, delta, delta > 0 ? "no": "yes");

        if ( delta > 0 )
            break;

        link_info.acked = seqno;
        peer->handler.link_info(peer, &link_info);

        rudp_list_remove(&pc->chain_item);
        rudp_packet_chain_free(peer->rudp, pc);
    }

    rudp_log_printf(peer->rudp, RUDP_LOG_DEBUG,
                    "%s left in queue:\n",
                    __FUNCTION__);
    rudp_list_for_each(struct rudp_packet_chain *, pc, &peer->sendq, chain_item) {
        struct rudp_packet_header *header = &pc->packet->header;
        rudp_log_printf(peer->rudp, RUDP_LOG_DEBUG,
                        "%s   - %04x:%04x\n",
                        __FUNCTION__,
                        ntohs(header->reliable),
                        ntohs(header->unreliable));
    }
    rudp_log_printf(peer->rudp, RUDP_LOG_DEBUG,
                    "%s ---\n",
                    __FUNCTION__);

    return 0;
}


/*
  Ack field is present in all headers.  Therefore any packet can be an
  ack.  If the send queue is empty, we cant afford to wait for a new
  one, so we send a new NOOP.
 */
static
void peer_post_ack(struct rudp_peer *peer)
{
    peer->must_ack = 1;

    if ( ! rudp_list_empty(&peer->sendq) ) {
        return;
    }

    struct rudp_packet_chain *pc = rudp_packet_chain_alloc(
        peer->rudp, sizeof(struct rudp_packet_conn_rsp));
    struct rudp_packet_conn_rsp *packet = &pc->packet->conn_rsp;

    rudp_log_printf(peer->rudp, RUDP_LOG_DEBUG,
                    "%s pushing NOOP ACK\n", __FUNCTION__);

    packet->header.command = RUDP_CMD_NOOP;
    packet->accepted = 0;

    rudp_peer_send_unreliable(peer, pc);
}


/* Sender functions */

static void
peer_sendq_append_unreliable(struct rudp_peer *peer,
        struct rudp_packet_chain *pc, size_t index, size_t length)
{
    pc->packet->header.version = RUDP_VERSION;
    pc->packet->header.opt = 0;
    pc->packet->header.dummy = 0;
    pc->packet->header.segment_index = htons((unsigned int)index);
    pc->packet->header.segments_size = htons((unsigned int)length);
    pc->packet->header.reliable = htons(peer->out_seq_reliable);
    pc->packet->header.unreliable = htons(++(peer->out_seq_unreliable));

    rudp_log_printf(peer->rudp, RUDP_LOG_IO,
                    ">>> outgoing unreliable %s (%d) %04x:%04x\n",
                    rudp_command_name(pc->packet->header.command),
                    pc->packet->header.command,
                    ntohs(pc->packet->header.reliable),
                    ntohs(pc->packet->header.unreliable));

    rudp_list_append(&peer->sendq, &pc->chain_item);
}

static void
peer_sendq_append_reliable(struct rudp_peer *peer,
        struct rudp_packet_chain *pc, size_t index, size_t length)
{
    peer->out_seq_unreliable = 0;

    pc->packet->header.version = RUDP_VERSION;
    pc->packet->header.opt = RUDP_OPT_RELIABLE;
    pc->packet->header.dummy = 0;
    pc->packet->header.segment_index = htons((unsigned int)index);
    pc->packet->header.segments_size = htons((unsigned int)length);
    pc->packet->header.reliable = htons(++(peer->out_seq_reliable));
    pc->packet->header.unreliable = 0; /* htons(peer->out_seq_unreliable); */

    rudp_log_printf(peer->rudp, RUDP_LOG_IO,
                    ">>> outgoing reliable %s (%d) %04x:%04x\n",
                    rudp_command_name(pc->packet->header.command),
                    pc->packet->header.command,
                    ntohs(pc->packet->header.reliable),
                    ntohs(pc->packet->header.unreliable));

    rudp_list_append(&peer->sendq, &pc->chain_item);
}

rudp_error_t
rudp_peer_send(struct rudp_base *rudp, struct rudp_peer *peer, int reliable,
        int command, const void *data, const size_t size)
{
    int ret;
    struct rudp_packet_chain *pc;
    size_t written, to_write;
    size_t header_size = sizeof(struct rudp_packet_header);
    size_t max_write = RUDP_RECV_BUFFER_SIZE - header_size;
    size_t segments = (size / max_write) + ((size % max_write) != 0);
    size_t segment;

    if (peer == NULL || data == NULL || size <= 0)
        return EINVAL;

    if ((command + RUDP_CMD_APP) > 255)
        return EINVAL;

    written = 0;
    for (segment = 0; segment < segments; segment++) {
        to_write = RUDP_MIN(size - written, max_write);
        pc = rudp_packet_chain_alloc(rudp, header_size + to_write);
        memcpy(&pc->packet->data.data[0], (const char *)data + written, to_write);
        written += to_write;
        pc->packet->header.command = RUDP_CMD_APP + command;
        if (reliable)
            peer_sendq_append_reliable(peer, pc, segment, segments);
        else
            peer_sendq_append_unreliable(peer, pc, segment, segments);
    }

    ret = peer_service_schedule(peer);
    if (ret != 0)
        return ret;
    return peer->sendto_err;
}

rudp_error_t
rudp_peer_send_unreliable(struct rudp_peer *peer,
        struct rudp_packet_chain *pc)
{
    int ret;
    peer_sendq_append_unreliable(peer, pc, 0, 1);

    ret = peer_service_schedule(peer);
    if (ret != 0)
        return ret;
    return peer->sendto_err;
}

rudp_error_t
rudp_peer_send_unreliable_segments(struct rudp_peer *peer,
        struct rudp_packet_chain **pc, size_t length)
{
    int ret;
    size_t index;

    for (index = 0; index < length; index++)
        peer_sendq_append_unreliable(peer, *(pc++), index, length);

    ret = peer_service_schedule(peer);
    if (ret != 0)
        return ret;
    return peer->sendto_err;
}

rudp_error_t
rudp_peer_send_reliable(struct rudp_peer *peer,
        struct rudp_packet_chain *pc)
{
    int ret;
    peer_sendq_append_reliable(peer, pc, 0, 1);

    ret = peer_service_schedule(peer);
    if (ret != 0)
        return ret;
    return peer->sendto_err;
}

rudp_error_t
rudp_peer_send_reliable_segments(struct rudp_peer *peer,
        struct rudp_packet_chain **pc, size_t length)
{
    int ret;
    size_t index;

    for (index = 0; index < length; index++)
        peer_sendq_append_reliable(peer, *(pc++), index, length);

    ret = peer_service_schedule(peer);
    if (ret != 0)
        return ret;
    return peer->sendto_err;
}

static
rudp_error_t peer_send_raw(
    struct rudp_peer *peer,
    const void *data, size_t len)
{
    if (peer == NULL)
        return EINVAL;

    peer->sendto_err = rudp_endpoint_send(peer->endpoint, &peer->address, data, len);
    if (peer->sendto_err != EINVAL)
        peer->last_out_time = rudp_timestamp();

    return peer->sendto_err;
}

rudp_error_t rudp_peer_send_connect(struct rudp_peer *peer)
{
    struct rudp_packet_chain *pc = rudp_packet_chain_alloc(
        peer->rudp, sizeof(struct rudp_packet_conn_req));
    struct rudp_packet_conn_req *conn_req = &pc->packet->conn_req;

    memset(conn_req, 0, sizeof(struct rudp_packet_conn_req));

    conn_req->header.command = RUDP_CMD_CONN_REQ;

    peer->state = PEER_CONNECTING;

    return rudp_peer_send_reliable(peer, pc);
}

rudp_error_t
rudp_peer_send_close_noqueue(struct rudp_peer *peer)
{
    struct rudp_packet_header header;

    if (peer == NULL || peer->rudp == NULL)
        return EINVAL;

    memset(&header, 0, sizeof(header));

    header.version = RUDP_VERSION;
    header.command = RUDP_CMD_CLOSE;
    header.reliable = htons(peer->out_seq_reliable);
    header.unreliable = htons(++(peer->out_seq_unreliable));
    header.segments_size = htons(1);

    rudp_log_printf(peer->rudp, RUDP_LOG_IO,
                    ">>> outgoing noqueue %s (%d) %04x:%04x\n",
                    rudp_command_name(header.command),
                    ntohs(header.reliable),
                    header.reliable,
                    ntohs(header.unreliable));

    return peer_send_raw(peer, &header, sizeof(header));
}

/* Worker functions */

static void peer_send_queue(struct rudp_peer *peer)
{
    struct rudp_packet_chain *pc, *tmp;
    rudp_list_for_each_safe(struct rudp_packet_chain *, pc, tmp, &peer->sendq, chain_item)
    {
        struct rudp_packet_header *header = &pc->packet->header;

        if ( peer->must_ack ) {
            header->opt |= RUDP_OPT_ACK;
            header->reliable_ack = htons(peer->in_seq_reliable);
        } else {
            header->reliable_ack = 0;
        }

        rudp_log_printf(peer->rudp, RUDP_LOG_IO,
                        ">>>>>> %ssend %sreliable %s %04x:%04x %s %04x\n",
                        header->opt & RUDP_OPT_RETRANSMITTED ? "RE" : "",
                        header->opt & RUDP_OPT_RELIABLE ? "" : "un",
                        rudp_command_name(pc->packet->header.command),
                        ntohs(pc->packet->header.reliable),
                        ntohs(pc->packet->header.unreliable),
                        header->opt & RUDP_OPT_ACK ? "ack" : "noack",
                        ntohs(pc->packet->header.reliable_ack));

        peer_send_raw(peer, header, pc->len);

        if ( (header->opt & RUDP_OPT_RELIABLE)
             && (header->opt & RUDP_OPT_RETRANSMITTED) ) {
            peer_rto_backoff(peer);
            break;
        }

        if ( header->opt & RUDP_OPT_RELIABLE ) {
            header->opt |= RUDP_OPT_RETRANSMITTED;
        } else {
            rudp_list_remove(&pc->chain_item);
            rudp_packet_chain_free(peer->rudp, pc);
        }
    }
}



/*
  Two reasons may bring us here:

  - There are some (un)reliable items in the send queue, either
    - we are retransmitting
    - we just enqueued something and we need to send

  - There is nothing in the send queue and we want to ensure the peer
    is still up
 */
static void peer_service(struct rudp_peer *peer)
{
    rudp_time_t timestamp = rudp_timestamp();

    if (peer->abs_timeout_deadline < timestamp) {
        peer->handler.dropped(peer);
        return;
    }

    if ( rudp_list_empty(&peer->sendq) ) {
        /*
          Nothing was in the send queue, so we may be in a timeout
          situation. Handle retries and final timeout.
        */
        rudp_time_t out_delta = timestamp - peer->last_out_time;
        if (out_delta > peer->timeout.action)
            peer_ping(peer);
    }

    peer_send_queue(peer);

    peer_service_schedule(peer);
}

static void _peer_service(evutil_socket_t fd, short flags, void *arg)
{
    peer_service((struct rudp_peer *)arg);
}

int rudp_peer_address_compare(const struct rudp_peer *peer,
                              const struct sockaddr_storage *addr)
{
    return rudp_address_compare(&peer->address, addr);
}

void
rudp_peer_set_timeout_max_rto(struct rudp_peer *peer, rudp_time_t max_rto)
{
    peer->timeout.max_rto = max_rto;
}

void
rudp_peer_set_timeout_drop(struct rudp_peer *peer, rudp_time_t drop)
{
    peer->timeout.drop = drop;
}

void
rudp_peer_set_timeout_action(struct rudp_peer *peer, rudp_time_t action)
{
    peer->timeout.action = action;
}
