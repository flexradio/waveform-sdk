//
// Created by Annaliese McDermond on 7/2/20.
//

#include <stdlib.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>

#include <event2/event.h>

#include "waveform.h"
#include "radio.h"
#include "vita.h"
#include "utils.h"

static pthread_workqueue_t vita_wq = NULL;

struct data_cb_wq_desc {
    struct waveform_cb_list *cb;
    struct waveform_vita_packet packet;
    size_t packet_size;
    struct waveform_t *wf;
};

static void vita_data_cb(void *arg)
{
    struct data_cb_wq_desc *desc = (struct data_cb_wq_desc *) arg;

    (desc->cb->data_cb)(desc->wf, &desc->packet, desc->packet_size, desc->cb->arg);

    free(desc);
}

static void vita_read_cb(evutil_socket_t socket, short what, void *ctx)
{
    struct vita *vita = (struct vita *) ctx;
    ssize_t bytes_received;
    struct waveform_vita_packet packet;

    if (!(what & EV_READ)) {
        fprintf(stderr, "Callback is not for a read?!\n");
        return;
    }

    if ((bytes_received = recv(socket, &packet, sizeof(packet), 0)) == -1) {
        fprintf(stderr, "VITA read failed: %s\n", strerror(errno));
        return;
    }

    // Byte swap the whole packet because we're going to pass it to the user, and we'll assume they want
    // host byte order, otherwise it's potentially confusing.
    // Hopefully the compiler will vector optimize this, because there should be NEON instructions for 4-wide
    // byte swap.  If it doesn't, we should do it ourselves.
    for (uint32_t *word = (uint32_t *) &packet; word < (uint32_t *) (&packet + 1); ++word) {
        *word = ntohl(*word);
    }

    unsigned long payload_length = (packet.length * sizeof(uint32_t)) - VITA_PACKET_HEADER_SIZE;

    if(payload_length != bytes_received - VITA_PACKET_HEADER_SIZE) {
        fprintf(stderr, "VITA header size doesn't match bytes read from network (%lu != %ld - %lu) -- %lu\n",
                payload_length, bytes_received, VITA_PACKET_HEADER_SIZE, sizeof(struct waveform_vita_packet));
        return;
    }

    struct waveform_t *cur_wf = container_of(vita, struct waveform_t, vita);

    //  XXX This is ugly.  We should probably be using some sort of pool of packet structures so that
    //  XXX we don't have to reallocate the memory all the time.  Maybe this really doesn't make a
    //  XXX difference, but memory copies suck.
    if (!(packet.stream_id & 0x0001U)) {
        vita->rx_stream_id = packet.stream_id;
        waveform_cb_for_each(cur_wf, rx_data_cbs, cur_cb) {
            struct data_cb_wq_desc *desc = (struct data_cb_wq_desc *) calloc(1, sizeof(struct data_cb_wq_desc));
            pthread_workitem_handle_t handle;
            unsigned int gencountp;

            desc->wf = cur_wf;
            memcpy(&desc->packet, &packet, bytes_received);
            desc->packet_size = bytes_received;
            desc->cb = cur_cb;

            pthread_workqueue_additem_np(vita_wq, vita_data_cb, desc, &handle, &gencountp);
        }
    } else {
        //  Transmit packet processing
        vita->tx_stream_id = packet.stream_id;
        waveform_cb_for_each(cur_wf, tx_data_cbs, cur_cb) {
            struct data_cb_wq_desc *desc = (struct data_cb_wq_desc *) calloc(1, sizeof(struct data_cb_wq_desc));
            pthread_workitem_handle_t handle;
            unsigned int gencountp;

            desc->wf = cur_wf;
            memcpy(&desc->packet, &packet, bytes_received);
            desc->packet_size = bytes_received;
            desc->cb = cur_cb;

            pthread_workqueue_additem_np(vita_wq, vita_data_cb, desc, &handle, &gencountp);
        }
    }
}

static void* vita_evt_loop(void *arg)
{
    struct waveform_t *wf = (struct waveform_t *) arg;
    struct vita *vita = &(wf->vita);
    int ret;

    struct sched_param thread_fifo_priority = {
        .sched_priority = sched_get_priority_max(SCHED_FIFO)
    };
    ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &thread_fifo_priority);
    if(ret) {
        fprintf(stderr, "Setting thread to realtime: %s\n", strerror(ret));
    }

    struct sockaddr_in bind_addr =  {
            .sin_family = AF_INET,
            .sin_addr.s_addr = htonl(INADDR_ANY),
            .sin_port = 0,
    };
    socklen_t bind_addr_len = sizeof(bind_addr);

    struct sockaddr_in radio_addr = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = wf->radio->addr.sin_addr.s_addr,
            .sin_port = htons(4993)    // XXX Magic here
    };

    fprintf(stderr, "Initializing VITA-49 engine...\n");

    vita->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (vita->sock == -1) {
        fprintf(stderr, " Failed to initialize VITA socket: %s\n", strerror(errno));
        goto fail;
    }

    if (bind(vita->sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr))) {
        fprintf(stderr, "error binding socket: %s\n",strerror(errno));
        goto fail_socket;
    }

    if (connect(vita->sock, (struct sockaddr *) &radio_addr, sizeof(struct sockaddr_in)) == -1) {
        fprintf(stderr, "Couldn't connect socket: %s\n", strerror(errno));
        goto fail_socket;
    }

    if (getsockname(vita->sock, (struct sockaddr *) &bind_addr, &bind_addr_len) == -1) {
        fprintf(stderr, "Couldn't get port number of VITA socket\n");
        goto fail_socket;
    }

    vita->base = event_base_new();
    if (!vita->base) {
        fprintf(stderr, "Couldn't create VITA event base\n");
        goto fail_socket;
    }

    vita->evt = event_new(vita->base, vita->sock, EV_READ|EV_PERSIST, vita_read_cb, vita);
    if (!vita->evt) {
        fprintf(stderr, "Couldn't create VITA event\n");
        goto fail_base;
    }
    if (event_add(vita->evt, NULL) == -1) {
        fprintf(stderr, "Couldn't add VITA event to base\n");
        goto fail_evt;
    }

    vita->port = ntohs(bind_addr.sin_port);

    vita->data_sequence = 0;
    vita->meter_sequence = 0;

    // XXX Check locking here.  Need to make sure threads are enabled in libevent
    waveform_send_api_command_cb(wf, NULL, NULL, "waveform set %s udpport=%hu", wf->name, vita->port);

    event_base_dispatch(vita->base);

    fprintf(stderr, "VITA thread ending...\n");

fail_evt:
    event_free(vita->evt);
fail_base:
    event_base_free(vita->base);
fail_socket:
    close(vita->sock);
fail:
    return NULL;
}

int vita_init(struct waveform_t *wf)
{
    int ret;

    if (!vita_wq) {
        pthread_workqueue_attr_t wq_attr;

        ret = pthread_workqueue_attr_init_np(&wq_attr);
        if (ret) {
            fprintf(stderr, "Creating WQ attributes: %s\n", strerror(ret));
            return -1;
        }

        ret = pthread_workqueue_attr_setqueuepriority_np(&wq_attr, WORKQ_HIGH_PRIOQUEUE);
        if (ret) {
            fprintf(stderr, "Couldn't set WQ priority: %s\n", strerror(ret));
            //  Purposely not returning here because this is a non-fatal error.  Things will still work,
            //  but potentially really suck.
        }

        ret = pthread_workqueue_create_np(&vita_wq, &wq_attr);
        if (ret) {
            fprintf(stderr, "Couldn't create callback WQ: %s\n", strerror(ret));
            return -1;
        }
    }

    ret = pthread_create(&wf->vita.thread, NULL, vita_evt_loop, wf);
    if (ret) {
        fprintf(stderr, "Creating thread: %s\n", strerror(ret));
        return -1;
    }
}

void vita_destroy(struct waveform_t *wf)
{
    event_base_loopexit(wf->vita.base, NULL);
}

static void vita_send_packet_cb(evutil_socket_t socket, short what, void *arg)
{
    struct waveform_vita_packet *packet = (struct waveform_vita_packet *) arg;
    if (!(what & EV_WRITE)) {
        fprintf(stderr, "Callback is not for a read?!\n");
        return;
    }

    ssize_t bytes_sent;
    size_t packet_len = VITA_PACKET_HEADER_SIZE + (packet->length * sizeof(float));
    packet->length += (VITA_PACKET_HEADER_SIZE / 4);

    //  XXX Lots of magic numbers here!
    packet->timestamp_type = 0x50U | (packet->timestamp_type & 0x0FU);
    assert(packet_len % 4 == 0);
    packet->timestamp_int = time(NULL);
    packet->timestamp_frac = 0;

//    if (packet->class_id == AUDIO_CLASS_ID) {
//        packet->timestamp_type = vita->data_sequence++;
//    } else {
//        packet->timestamp_type = vita->meter_sequence++;
//    }


    // Hopefully the compiler will vector optimize this, because there should be NEON instructions for 4-wide
    // byte swap.  If it doesn't, we should do it ourselves.
    for (uint32_t *word = (uint32_t *) packet; word < (uint32_t *) (packet + 1); ++word) {
        *word = htonl(*word);
    }

    if ((bytes_sent = send(socket, packet, packet_len, 0)) == -1) {
        output("Error sending vita packet: %s\n", strerror(errno));
        return;
    }

    if (bytes_sent != packet_len) {
        output("Short write on vita send\n");
        return;
    }

    free(packet);
}

void vita_send_packet(struct vita *vita, struct waveform_vita_packet *packet)
{
    vita->evt = event_new(vita->base, vita->sock, EV_WRITE, vita_send_packet_cb, packet);
    if (!vita->evt) {
        fprintf(stderr, "Couldn't create VITA event\n");
        return;
    }
    if (event_add(vita->evt, NULL) == -1) {
        fprintf(stderr, "Couldn't add VITA event to base\n");
        event_free(vita->evt);
        return;
    }
}

//void vita_send_meter_packet(struct vita *vita, void *meters, size_t len)
//{
//    struct waveform_vita_packet packet = {0};
//
//    packet.packet_type = VITA_PACKET_TYPE_EXT_DATA_WITH_STREAM_ID;
//    packet.stream_id = METER_STREAM_ID;
//    packet.class_id = METER_CLASS_ID;
//    packet.timestamp_type = vita->meter_sequence++;
//
//    assert(len < sizeof(packet.raw_payload));
//    memcpy(packet.raw_payload, meters, len);
//
//    vita_send_packet(vita, &packet, len);
//}
//
void vita_send_data_packet(struct vita *vita, float *samples, size_t num_samples, enum waveform_packet_type type)
{
    struct waveform_vita_packet *packet = calloc(1, sizeof(struct waveform_vita_packet));

    packet->packet_type = VITA_PACKET_TYPE_IF_DATA_WITH_STREAM_ID;
    packet->class_id = AUDIO_CLASS_ID;
    packet->length = num_samples; // Length is in 32-bit words
    //  XXX This is an issue because the pointer dereference won't be atomic.  If two threads go at this at once
    //  XXX the value could get corrupted.  It's not greatly important, but it will screw up the sequence.  It should
    //  XXX probably be done on the IO thread, but we lose the reference to the struct vita when we call the
    //  XXX event callback.
    packet->timestamp_type = vita->data_sequence++;

    switch (type) {
        case TRANSMITTER_DATA:
            packet->stream_id = vita->tx_stream_id;
            break;
        case SPEAKER_DATA:
            packet->stream_id = vita->rx_stream_id;
            break;
        default:
            fprintf(stderr, "Invalid packet type!\n");
            break;
    }

    memcpy(packet->raw_payload, samples, num_samples * sizeof(float));
    vita_send_packet(vita, packet);
}

inline uint16_t get_packet_len(struct waveform_vita_packet *packet)
{
    return packet->length - (VITA_PACKET_HEADER_SIZE / sizeof(uint32_t));
}

inline float *get_packet_data(struct waveform_vita_packet *packet)
{
    return packet->if_samples;
}

inline uint32_t get_packet_ts_int(struct waveform_vita_packet *packet)
{
    return packet->timestamp_int;
}

inline uint64_t get_packet_ts_frac(struct waveform_vita_packet *packet)
{
    return packet->timestamp_frac;
}