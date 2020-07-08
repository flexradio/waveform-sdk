//
// Created by Annaliese McDermond on 6/25/20.
//
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <string.h>

#include <waveform_api.h>

int rx_phase = 0;
double phase_advance = 2.0 * M_PI * 1000.0 / 24000.0;
pthread_mutex_t rx_phase_lock, tx_phase_lock;

static float sin_table[] = {
    0.0F,                 0.25881904510252074F, 0.49999999999999994F,     0.7071067811865475F,   0.8660254037844386F,
    0.9659258262890682F,  1.0F,                 0.9659258262890683F,      0.8660254037844388F,   0.7071067811865476F,
    0.5000000000000003F,  0.258819045102521F,   1.2246467991473532e-16F, -0.25881904510252035F, -0.4999999999999998F,
   -0.7071067811865471F, -0.8660254037844384F, -0.9659258262890681F,     -1.0F,                 -0.9659258262890684F,
   -0.866025403784439F,  -0.7071067811865477F, -0.5000000000000004F,     -0.2588190451025215F
};

static void echo_command(struct waveform_t *waveform, unsigned int argc, char *argv[], void *arg)
{
//    fprintf(stderr, "Got a status for %s\n", argv[0]);
//    fprintf(stderr, "Number of args is %u\n", argc);
//    for(int i = 0; i < argc; ++i) {
//        fprintf(stderr, "ARG #%u: %s\n", i, argv[i]);
//    }
}

static void packet_rx(struct waveform_t *waveform, struct waveform_vita_packet *packet, size_t packet_size, void *arg)
{
    float *null_samples = (float *) calloc(get_packet_len(packet), sizeof(float));

    // float *null_samples = (float *) calloc(get_packet_len(packet), sizeof(float));
    float null_samples[get_packet_len(packet)];
    memset(null_samples, 0, sizeof(null_samples));

    pthread_mutex_lock(&rx_phase_lock);
    for (int i = 0; i < get_packet_len(packet); i += 2) {
        null_samples[i] = null_samples[i + 1] = sin_table[rx_phase] * 0.5F;
        rx_phase = (rx_phase + 1) % 24;
    }
    pthread_mutex_unlock(&rx_phase_lock);

    waveform_send_data_packet(waveform, null_samples, get_packet_len(packet), SPEAKER_DATA);
    // free(null_samples);
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

    pthread_mutex_init(&rx_phase_lock, NULL);
    pthread_mutex_init(&tx_phase_lock, NULL);

    struct radio_t *radio = waveform_radio_create(&addr);
    struct waveform_t *test_waveform = waveform_create(radio, "JunkMode", "JUNK", "DIGU", "1.0.0");
    waveform_register_status_cb(test_waveform, "slice", echo_command, NULL);
    waveform_register_state_cb(test_waveform, state_test, NULL);
    waveform_register_rx_data_cb(test_waveform, packet_rx, NULL);
    waveform_radio_start(radio);

    waveform_radio_wait(radio);
}