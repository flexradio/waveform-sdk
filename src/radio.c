//
// Created by Annaliese McDermond on 6/25/20.
//

#define _GNU_SOURCE

#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <limits.h>
#include <assert.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/thread.h>

#include <pthread_workqueue.h>

#include <sds.h>
#include <utlist.h>

#include "utils.h"
#include "waveform.h"
#include "radio.h"

static void add_sequence_to_response_queue(struct waveform_t *waveform, waveform_response_cb_t cb, void *ctx)
{
    struct response_queue_entry *new_entry;

    new_entry = (struct response_queue_entry *) malloc(sizeof(*new_entry));
    new_entry->cb = cb;
    new_entry->sequence = waveform->radio->sequence;
    new_entry->ctx = ctx;

    LL_APPEND(waveform->radio->rq_head, new_entry);
}

static void complete_response_entry(struct radio_t *radio, unsigned int sequence, unsigned int code, sds message)
{
    struct response_queue_entry *current_entry;

    LL_SEARCH_SCALAR(radio->rq_head, current_entry, sequence, sequence);
    if (!current_entry)
        return;

    current_entry->cb(current_entry->wf, code, message, current_entry->ctx);
    LL_DELETE(radio->rq_head, current_entry);
    free(current_entry);
}

static void destroy_response_queue(struct waveform_t *waveform)
{
    struct response_queue_entry *current_entry, *tmp_entry;

    LL_FOREACH_SAFE(waveform->radio->rq_head, current_entry, tmp_entry) {
        LL_DELETE(waveform->radio->rq_head, current_entry);
    }
}

static void interlock_state_change(struct radio_t *radio, sds state)
{
    enum waveform_state cb_state;

    if (strcmp(state, "PTT_REQUESTED") == 0) {
        cb_state = PTT_REQUESTED;
    } else if (strcmp(state, "UNKEY_REQUESTED") == 0) {
        cb_state = UNKEY_REQUESTED;
    } else {
        return;
    }

    radio_waveforms_for_each(radio, cur_wf) {
        for (struct waveform_cb_list *cur_cb = cur_wf->state_cbs; cur_cb != NULL; cur_cb = cur_cb->next) {
            // XXX Spawn to worker thread.
            (cur_cb->state_cb)(cur_wf, cb_state, cur_cb->arg);
        }
    }
}

static void mode_change(struct radio_t *radio, sds mode, char slice)
{
    fprintf(stderr, "Got a request for mode %s on slice %d\n", mode, slice);

    //  XXX Run these in the work queue.
    radio_waveforms_for_each(radio, cur_wf) {
        //  User has deselected this waveform's mode.
        if (cur_wf->active_slice == slice && strcmp(cur_wf->short_name, mode) != 0) {
            for (struct waveform_cb_list *cur_cb = cur_wf->state_cbs; cur_cb != NULL; cur_cb = cur_cb->next) {
                (cur_cb->state_cb)(cur_wf, INACTIVE, cur_cb->arg);
            }
            cur_wf->active_slice = -1;
            vita_destroy(cur_wf);
        }

        // User has selected this waveform's mode and we're not busy.
        if (cur_wf->active_slice == -1 && strcmp(cur_wf->short_name, mode) == 0) {
            for (struct waveform_cb_list *cur_cb = cur_wf->state_cbs; cur_cb != NULL; cur_cb = cur_cb->next) {
                (cur_cb->state_cb)(cur_wf, ACTIVE, cur_cb->arg);
            }
            cur_wf->active_slice = slice;
            vita_init(cur_wf);
        }
    }
}

struct status_cb_wq_desc {
    struct waveform_t *wf;
    sds message;
    struct waveform_cb_list *cb;
};

static void radio_call_status_cb(void *arg)
{
    int argc;
    struct status_cb_wq_desc *desc = (struct status_cb_wq_desc *) arg;

    sds *argv = sdssplitargs(desc->message, &argc);
    if (argc < 1) {
        sdsfree(desc->message);
        free(desc);
        return;
    }

    (desc->cb->cmd_cb)(desc->wf, argc, argv, desc->cb->arg);

    sdsfreesplitres(argv, argc);
    sdsfree(desc->message);
    free(desc);
}

static void process_status_message(struct radio_t *radio, sds message)
{
    int argc;

    sds *argv = sdssplitargs(message, &argc);
    if (argc < 1) {
        return;
    }

    if (strcmp(argv[0], "slice") == 0) {
        sds mode = find_kwarg(argc, argv, "mode");
        if (mode) {
            errno = 0;
            char *endptr;

            long slice = strtoul(argv[1], &endptr, 10);
            if ((errno == ERANGE && slice == ULONG_MAX) ||
                (errno != 0 && slice == 0)) {
                fprintf(stderr, "Error finding slice: %s\n", strerror(errno));
            } else {
                mode_change(radio, mode, (char) slice);
            }
            sdsfree(mode);
        }
    } else if (strcmp(argv[0], "interlock") == 0) {
        sds state = find_kwarg(argc, argv, "state");
        if (state) {
            interlock_state_change(radio, state);
            sdsfree(state);
        }
    }

    radio_waveforms_for_each(radio, cur_wf) {
        waveform_cb_for_each(cur_wf, status_cbs, cur_cb) {
            struct status_cb_wq_desc *desc = calloc(1, sizeof(*desc));
            pthread_workitem_handle_t handle;
            unsigned int gencountp;

            desc->wf = cur_wf;
            desc->message = sdsdup(message);
            desc->cb = cur_cb;

            pthread_workqueue_additem_np(radio->cb_wq, radio_call_status_cb, desc, &handle, &gencountp);
        }
    }

    sdsfreesplitres(argv, argc);
}

struct cmd_cb_wq_desc {
    int sequence;
    sds message;
    struct waveform_t *wf;

    struct waveform_cb_list *cb;
};

static void radio_call_command_cb(void *arg)
{
    int argc;
    struct cmd_cb_wq_desc *desc = (struct cmd_cb_wq_desc *) arg;

    sds *argv = sdssplitargs(desc->message, &argc);
    if (argc < 1) {
        sdsfree(desc->message);
        free(desc);
        return;
    }

    int ret = (desc->cb->cmd_cb)(desc->wf, argc - 2, argv + 2, desc->cb->arg);
    if (ret) {
        waveform_send_api_command_cb(desc->wf, NULL, NULL, "waveform response %d|%08x", desc->sequence, ret + 0x50000000);
    } else {
        waveform_send_api_command_cb(desc->wf, NULL, NULL, "waveform response %d|0", desc->sequence);
    }

    sdsfreesplitres(argv, argc);
    sdsfree(desc->message);
    free(desc);
}

static void process_waveform_command(struct radio_t *radio, int sequence, sds message) {
    int argc;

    sds *argv = sdssplitargs(message, &argc);
    if (argc < 3 || strcmp(argv[0], "slice") != 0) {
        sdsfreesplitres(argv, argc);
        return;
    }

    errno = 0;
    char *endptr;
    long slice = strtoul(argv[1], &endptr, 10);
    if ((errno == ERANGE && slice == ULONG_MAX) ||
        (errno != 0 && slice == 0)) {
        fprintf(stderr, "Error finding slice: %s\n", strerror(errno));

        sdsfreesplitres(argv, argc);
        return;
    }

    radio_waveforms_for_each(radio, cur_wf) {
        if (slice != cur_wf->active_slice) {
            continue;
        }

        waveform_cb_for_each(cur_wf, cmd_cbs, cur_cb) {
            if (strcmp(cur_cb->name, argv[2]) != 0) {
                continue;
            }

            struct cmd_cb_wq_desc *desc = calloc(1, sizeof(*desc));
            pthread_workitem_handle_t handle;
            unsigned int gencountp;

            desc->wf = cur_wf;
            desc->message = sdsdup(message);
            desc->cb = cur_cb;
            desc->sequence = sequence;

            pthread_workqueue_additem_np(radio->cb_wq, radio_call_command_cb, desc, &handle, &gencountp);
        }
    }

    sdsfreesplitres(argv, argc);
}

static void radio_process_line(struct radio_t *radio, sds line)
{
    char *message, *endptr, *response_message;
    int ret;
    unsigned long code, handle, sequence;
    unsigned int api_version[4];

    assert(radio != NULL);
    assert(line != NULL);

    fprintf(stderr, "Rx: %s\n", line);
    char command = *line;
    sdsrange(line, 1, -1);
    switch(command) {
        case 'V':
            // TODO: Fix me so that I read into the api struct.
            errno = 0;
#pragma clang diagnostic push
#pragma ide diagnostic ignored "cert-err34-c"
            ret = sscanf(line, "%d.%d.%d.%d", &api_version[0], &api_version[1], &api_version[2], &api_version[3]);
#pragma clang diagnostic pop
            if (ret != 4)
                fprintf(stderr, "Error converting version string: %s\n", line);

            fprintf(stdout, "Radio API Version: %d.%d(%d.%d)\n", api_version[0], api_version[1], api_version[2], api_version[3]);
            break;
        case 'H':
            errno = 0;
            radio->handle = strtoul(line, &endptr, 16);
            if ((errno == ERANGE && radio->handle == ULONG_MAX) ||
                (errno != 0 && radio->handle == 0)) {
                fprintf(stderr, "Error finding session handle: %s\n", strerror(errno));
                break;
            }

            if (endptr == line) {
                fprintf(stderr, "Cannot find session handle in: %s\n", line);
                break;
            }

            break;
        case 'S':
            errno = 0;
            handle = strtoul(line, &endptr, 16);
            if ((errno == ERANGE && handle == ULONG_MAX) ||
                (errno != 0 && handle == 0)) {
                fprintf(stderr, "Error finding status handle: %s\n", strerror(errno));
                break;
            }

            if (endptr == line) {
                break;
            }

            sdsrange(line, endptr - line + 1, -1);
            process_status_message(radio, line);
            break;
        case 'M':
            break;
        case 'R':
            errno = 0;
            sequence = strtoul(line, &endptr, 10);
            if ((errno == ERANGE && sequence == ULONG_MAX) ||
                (errno != 0 && sequence == 0)) {
                fprintf(stderr, "Error finding response sequence: %s\n", strerror(errno));
                break;
            }

            if (endptr == line) {
                fprintf(stderr, "Cannot find response sequence in: %s\n", line);
                break;
            }

            errno = 0;
            code = strtoul(endptr + 1, &response_message, 16);
            if ((errno == ERANGE && code == ULONG_MAX) ||
                (errno != 0 && code == 0)) {
                fprintf(stderr, "Error finding response code: %s\n", strerror(errno));
                break;
            }

            if (response_message == endptr + 1) {
                fprintf(stderr, "Cannot find response code in: %s\n", line);
                break;
            }

            complete_response_entry(radio, sequence, code, response_message + 1);
            break;
        case 'C':
            errno = 0;
            sequence = strtoul(line, &endptr, 10);
            if ((errno == ERANGE && sequence == ULONG_MAX) ||
                (errno != 0 && sequence == 0)) {
                output("Error finding command sequence: %s\n", strerror(errno));
                break;
            }

            if (line == endptr) {
                output("Cannot find command sequence in: %s\n", line);
                break;
            }

            sdsrange(line, endptr - line + 1, -1);
            process_waveform_command(radio, sequence, line);
            break;
        default:
            fprintf(stderr, "Unknown command: %s\n", line);
            break;
    }
}

long waveform_radio_send_api_command_cb_va(struct waveform_t *wf, waveform_response_cb_t cb, void *arg, char *command, va_list ap)
{
    int cmdlen;
    char *message_format;
    va_list aq;

    struct evbuffer *output = bufferevent_get_output(wf->radio->bev);

    cmdlen = asprintf(&message_format, "C%ld|%s\n", wf->radio->sequence, command);
    if (cmdlen < 0) {
        return -1;
    }

    fprintf(stderr, "Tx: ");
    va_copy(aq, ap);
    vfprintf(stderr, message_format, aq);
    va_end(aq);

    evbuffer_add_vprintf(output, message_format, ap);

    free(message_format);

    if (cb) {
        add_sequence_to_response_queue(wf, cb, arg);
    }

    return ++wf->radio->sequence;
}

static inline long waveform_radio_send_api_command_cb(struct waveform_t *wf, waveform_response_cb_t cb, void *arg, char *command, ...)
{
    va_list ap;
    long ret;

    va_start(ap, command);
    ret = waveform_radio_send_api_command_cb_va(wf, cb, arg, command, ap);
    va_end(ap);

    return ret;
}

static void radio_init(struct radio_t *radio)
{
    int sub = 0;

    radio_waveforms_for_each(radio, cur_wf) {
        if (sub == 0) {
            waveform_radio_send_api_command_cb(cur_wf, NULL, NULL, "sub slice all");
            sub = 1;
        }

        waveform_radio_send_api_command_cb(cur_wf, NULL, NULL,
                "waveform create name=%s mode=%s underlying_mode=%s version=%s",
                cur_wf->name, cur_wf->short_name, cur_wf->underlying_mode, cur_wf->version);
        waveform_radio_send_api_command_cb(cur_wf, NULL, NULL,
                "waveform set %s tx=1", cur_wf->name);
        waveform_radio_send_api_command_cb(cur_wf, NULL, NULL,
                "waveform set %s rx_filter depth=%d", cur_wf->name, cur_wf->rx_depth);
        waveform_radio_send_api_command_cb(cur_wf, NULL, NULL,
                "waveform set %s tx_filter depth=%d", cur_wf->name, cur_wf->tx_depth);
    }
}

static void radio_event_cb(struct bufferevent *bev __attribute__ ((unused)), short what, void *ctx)
{
    struct radio_t* radio = (struct radio_t *) ctx;
    switch (what) {
        case BEV_EVENT_CONNECTED:
            fprintf(stdout, "Connected to radio at %s\n", inet_ntoa(radio->addr.sin_addr));
            radio_init(radio);
            break;
        case BEV_EVENT_ERROR:
            fprintf(stderr, "Error connecting to radio\n");
            break;
        case BEV_EVENT_TIMEOUT:
            fprintf(stderr,"Timeout connecting to radio\n");
            break;
        case BEV_EVENT_EOF:
            fprintf(stderr, "Remote side disconnected\n");
            break;
        default:
            fprintf(stderr, "Unknown error\n");
            break;
    }
}

static void radio_read_cb(struct bufferevent *bev, void *ctx)
{
    struct radio_t* radio = (struct radio_t *) ctx;
    char *line;
    size_t chars_read;


    struct evbuffer* input_buffer = bufferevent_get_input(bev);

    while ((line = evbuffer_readln(input_buffer, &chars_read, EVBUFFER_EOL_ANY))) {
        sds newline = sdsnewlen(line, chars_read);
        free(line);
        radio_process_line(radio, newline);
        sdsfree(newline);
    }
}


static void* radio_evt_loop(void *arg)
{
    struct radio_t* radio = (struct radio_t *) arg;

    evthread_use_pthreads();

    radio->base = event_base_new();

    radio->bev = bufferevent_socket_new(radio->base, -1, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);
    if (!radio->bev) {
        fprintf(stderr,"Could not create buffer event socket\n");
        goto eb_abort;
    }

    bufferevent_setcb(radio->bev, radio_read_cb, NULL, radio_event_cb, radio);
#pragma clang diagnostic push
#pragma ide diagnostic ignored "hicpp-signed-bitwise"
    if (bufferevent_enable(radio->bev, EV_READ|EV_WRITE)) { //  XXX Do we really need write here?
#pragma clang diagnostic pop
        fprintf(stderr, "Could not enable buffer event\n");
        goto bev_abort;
    }

    if (bufferevent_socket_connect(radio->bev, (struct sockaddr *) &radio->addr, sizeof(struct sockaddr_in))) {
        fprintf(stderr, "Could not connect to radio\n");
        goto bev_abort;
    }

    event_base_dispatch(radio->base);

    bev_abort:
    bufferevent_free(radio->bev);

    eb_abort:
    event_base_free(radio->base);

    return NULL;
}

struct radio_t *waveform_radio_create(struct sockaddr_in *addr) {
    struct radio_t *radio = calloc(1, sizeof(*radio));
    if (!radio) {
        return NULL;
    }

    memcpy(&radio->addr, addr, sizeof(struct sockaddr_in));

    pthread_workqueue_init_np();

    return radio;
}

void waveform_radio_destroy(struct radio_t *radio) {
    free(radio);
}

int waveform_radio_start(struct radio_t *radio) {
    int ret;
    pthread_workqueue_attr_t wq_attr;

    ret = pthread_workqueue_attr_init_np(&wq_attr);
    if (ret) {
        fprintf(stderr, "Creating WQ attributes: %s\n", strerror(ret));
        return -1;
    }

//    ret = pthread_workqueue_attr_setovercommit_np(&wq_attr, 1);
//    if (ret) {
//        fprintf(stderr, "Couldn't set WQ to overcommit: %s\n", strerror(ret));
//        //  Purposely not returning here because this is a non-fatal error.  Things will still work,
//        //  but potentially suck.
//    }

    ret = pthread_workqueue_attr_setqueuepriority_np(&wq_attr, WORKQ_DEFAULT_PRIOQUEUE);
    if (ret) {
        fprintf(stderr, "Couldn't set WQ priority: %s\n", strerror(ret));
        //  Purposely not returning here because this is a non-fatal error.  Things will still work,
        //  but potentially suck.
    }

    ret = pthread_workqueue_create_np(&radio->cb_wq, &wq_attr);
    if (ret) {
        fprintf(stderr, "Couldn't create callback WQ: %s\n", strerror(ret));
        return -1;
    }

    ret = pthread_create(&radio->thread, NULL, radio_evt_loop, radio);
    if(ret) {
        free(radio);
        fprintf(stderr, "Creating thread: %s\n", strerror(ret));
        return -1;
    }

    return 0;
}

int waveform_radio_wait(struct radio_t* radio)
{
    return pthread_join(radio->thread, NULL);
}

