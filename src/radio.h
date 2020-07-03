//
// Created by Annaliese McDermond on 6/29/20.
//

#ifndef WAVEFORM_SDK_RADIO_H
#define WAVEFORM_SDK_RADIO_H

#include <pthread_workqueue.h>

struct radio_t {
    struct sockaddr_in addr;
    pthread_t thread;
    struct event_base  *base;
    struct bufferevent *bev;
    unsigned long handle;
    long sequence;
    pthread_workqueue_t cb_wq;
};

long waveform_radio_send_api_command_cb_va(struct radio_t *radio, waveform_response_cb_t cb, void *arg, char *command, va_list ap);

#endif //WAVEFORM_SDK_RADIO_H
