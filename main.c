//
// Created by Annaliese McDermond on 6/25/20.
//
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>

#include <waveform_api.h>

static void echo_command(struct waveform_t *waveform, unsigned int argc, char *argv[], void *arg) {
    fprintf(stderr, "Got a status for %s\n", argv[0]);
    fprintf(stderr, "Number of args is %u\n", argc);
    for(int i = 0; i < argc; ++i) {
        fprintf(stderr, "ARG #%u: %s\n", i, argv[i]);
    }
}

static void packet_rx(struct waveform_t *waveform, struct waveform_vita_packet *packet, size_t packet_size, void *arg)
{
    fputc('.', stderr);
}

static void state_test(struct waveform_t *waveform, enum waveform_state state, void *arg) {
    switch (state) {
        case ACTIVE:
            fprintf(stderr, "wf is active\n");
            break;
        case INACTIVE:
            fprintf(stderr, "wf is inactive\n");
            break;
        case PTT_REQUESTED:
            fprintf(stderr, "ptt requested\n");
            break;
        case UNKEY_REQUESTED:
            fprintf(stderr, "unkey requested\n");
            break;
        default:
            fprintf(stderr, "unknown state received");
            break;
    }
}

int main(int argc, char **argv)
{
    struct sockaddr_in addr = {
            .sin_addr = {0},
            .sin_port = htons(4992),
            .sin_family = AF_INET
    };

    inet_aton("10.0.3.34", &addr.sin_addr);

    struct radio_t *radio = waveform_radio_create(&addr);
    struct waveform_t *test_waveform = waveform_create(radio, "JunkMode", "JUNK", "DIGU", "1.0.0");
    waveform_register_status_cb(test_waveform, "slice", echo_command, NULL);
    waveform_register_state_cb(test_waveform, state_test, NULL);
    waveform_register_rx_data_cb(test_waveform, packet_rx, NULL);
    waveform_radio_start(radio);

    waveform_radio_wait(radio);
}