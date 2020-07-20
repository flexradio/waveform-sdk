//
// Created by Annaliese McDermond on 6/29/20.
//

#ifndef WAVEFORM_WAVEFORM_H
#define WAVEFORM_WAVEFORM_H

#include <sds.h>

#include <waveform_api.h>

#include "vita.h"

struct waveform_cb_list {
    sds name;
    union {
        waveform_cmd_cb_t cmd_cb;
        waveform_state_cb_t state_cb;
        waveform_data_cb_t data_cb;
    };
    void *arg;
    struct waveform_cb_list *next;
};

struct waveform_t {
    char *name;
    char *short_name;
    char *underlying_mode;
    char *version;
    char active_slice;

    int rx_depth;
    int tx_depth;

    struct radio_t *radio;

    struct vita vita;

    struct waveform_cb_list *status_cbs;
    struct waveform_cb_list *state_cbs;
    struct waveform_cb_list *rx_data_cbs;
    struct waveform_cb_list *tx_data_cbs;
    struct waveform_cb_list *cmd_cbs;

    void *ctx;

    struct waveform_t *next;
};

extern struct waveform_t *wf_list;

#define radio_waveforms_for_each(radio, pos) \
    for (struct waveform_t *(pos) = wf_list; (pos) != NULL; (pos) = (pos)->next) \
        if ((pos)->radio == (radio))

#define waveform_cb_for_each(wf, cb_list, pos) \
            for (struct waveform_cb_list *(pos) = (wf)->cb_list; (pos) != NULL; (pos) = (pos)->next)

#endif //WAVEFORM_WAVEFORM_H
