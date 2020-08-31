// SPDX-License-Identifier: LGPL-3.0-or-later
/// @file main.c
/// @brief Functional test yoke for Waveform SDK Functionality
/// @authors Annaliese McDermond <anna@flex-radio.com>
///
/// @copyright Copyright (c) 2020 FlexRadio Systems
///
/// This program is free software: you can redistribute it and/or modify
/// it under the terms of the GNU Lesser General Public License as published by
/// the Free Software Foundation, version 3.
///
/// This program is distributed in the hope that it will be useful, but
/// WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
/// Lesser General Public License for more details.
///
/// You should have received a copy of the GNU Lesser General Public License
/// along with this program. If not, see <http://www.gnu.org/licenses/>.
///

// ****************************************
// System Includes
// ****************************************
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

// ****************************************
// Project Includes
// ****************************************
#include <waveform/waveform_api.h>

// ****************************************
// Structs, Enums, typedefs
// ****************************************
struct junk_context {
   int rx_phase;
   int tx_phase;
   pthread_mutex_t rx_phase_lock;
   pthread_mutex_t tx_phase_lock;
   int tx;
   short snr;
};

// ****************************************
// Macros
// ****************************************
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

// ****************************************
// Static Variables
// ****************************************
static const float sin_table[] = {0.0F,
                                  0.25881904510252074F,
                                  0.49999999999999994F,
                                  0.7071067811865475F,
                                  0.8660254037844386F,
                                  0.9659258262890682F,
                                  1.0F,
                                  0.9659258262890683F,
                                  0.8660254037844388F,
                                  0.7071067811865476F,
                                  0.5000000000000003F,
                                  0.258819045102521F,
                                  1.2246467991473532e-16F,
                                  -0.25881904510252035F,
                                  -0.4999999999999998F,
                                  -0.7071067811865471F,
                                  -0.8660254037844384F,
                                  -0.9659258262890681F,
                                  -1.0F,
                                  -0.9659258262890684F,
                                  -0.866025403784439F,
                                  -0.7071067811865477F,
                                  -0.5000000000000004F,
                                  -0.2588190451025215F};

static const struct waveform_meter_entry meters[] = {
      {.name = "junk-snr", .min = -100.0f, .max = 100.0f, .unit = DB},
      {.name = "junk-foff", .min = 0.0f, .max = 100000.0f, .unit = DB},
      {.name = "junk-clock-offset", .min = 0.0f, .max = 100000.0f, .unit = DB}};


// ****************************************
// Static Functions
// ****************************************
// Static functions are not documented here with doxygen because this file is merely a test yoke that will go away and become
// an example eventually.  At that point the functions will be documented.
static int echo_command(struct waveform_t* waveform, unsigned int argc,
                        char* argv[], void* arg __attribute__((unused)))
{
   //    fprintf(stderr, "Got a status for %s\n", argv[0]);
   //    fprintf(stderr, "Number of args is %u\n", argc);
   //    for(int i = 0; i < argc; ++i) {
   //        fprintf(stderr, "ARG #%u: %s\n", i, argv[i]);
   //    }
   return 0;
}

static int test_command(struct waveform_t* waveform, unsigned int argc,
                        char* argv[], void* arg __attribute__((unused)))
{
   for (int i = 0; i < argc; ++i)
      fprintf(stderr, "ARG #%u: %s\n", i, argv[i]);

   return 0;
}

static void packet_rx(struct waveform_t* waveform,
                      struct waveform_vita_packet* packet, size_t packet_size,
                      void* arg __attribute__((unused)))
{
   struct junk_context* ctx = waveform_get_context(waveform);

   if (ctx->tx == 1)
   {
      return;
   }

   float null_samples[get_packet_len(packet)];
   memset(null_samples, 0, sizeof(null_samples));

   pthread_mutex_lock(&ctx->rx_phase_lock);
   for (int i = 0; i < get_packet_len(packet); i += 2)
   {
      null_samples[i] = null_samples[i + 1] =
            sin_table[ctx->rx_phase] * 0.5F;
      ctx->rx_phase = (ctx->rx_phase + 1) % 24;
   }
   pthread_mutex_unlock(&ctx->rx_phase_lock);

   waveform_send_data_packet(waveform, null_samples,
                             get_packet_len(packet), SPEAKER_DATA);

   waveform_meter_set_float_value(waveform, "junk-snr", ctx->snr);
   waveform_meters_send(waveform);
   ctx->snr = (ctx->snr + 1) % 1024;
}

static void packet_tx(struct waveform_t* waveform,
                      struct waveform_vita_packet* packet, size_t packet_size,
                      void* arg __attribute__((unused)))
{
   struct junk_context* ctx = waveform_get_context(waveform);

   if (ctx->tx != 1)
   {
      return;
   }

   float xmit_samples[get_packet_len(packet)];
   memset(xmit_samples, 0, sizeof(xmit_samples));

   pthread_mutex_lock(&ctx->tx_phase_lock);
   for (int i = 0; i < get_packet_len(packet); i += 2)
   {
      xmit_samples[i] = xmit_samples[i + 1] =
            sin_table[ctx->tx_phase] * 0.5F;
      ctx->tx_phase = (ctx->tx_phase + 1) % 24;
   }
   pthread_mutex_unlock(&ctx->tx_phase_lock);

   waveform_send_data_packet(waveform, xmit_samples,
                             get_packet_len(packet), TRANSMITTER_DATA);
}

static void set_filter_callback(struct waveform_t* waveform, unsigned int code,
                                char* message, void* arg)
{
   fprintf(stderr, "Invoked callback for code %d, message %s\n", code,
           message);
}

static void state_test(struct waveform_t* waveform, enum waveform_state state,
                       void* arg)
{
   struct junk_context* ctx = waveform_get_context(waveform);

   switch (state)
   {
      case ACTIVE:
         fprintf(stderr, "wf is active\n");
         waveform_send_api_command_cb(waveform, set_filter_callback,
                                      NULL, "filt 0 100 3000");
         break;
      case INACTIVE:
         fprintf(stderr, "wf is inactive\n");
         break;
      case PTT_REQUESTED:
         fprintf(stderr, "ptt requested\n");
         ctx->tx = 1;
         break;
      case UNKEY_REQUESTED:
         fprintf(stderr, "unkey requested\n");
         ctx->tx = 0;
         break;
      default:
         fprintf(stderr, "unknown state received");
         break;
   }
}

// ****************************************
// Global Functions
// ****************************************
int main(int argc, char** argv)
{
   struct sockaddr_in* addr;

   struct junk_context ctx = {0};

   struct timeval timeout = {
         .tv_sec = 10,
         .tv_usec = 0};
   addr = waveform_discover_radio(&timeout);

   if (addr == NULL)
   {
      fprintf(stderr, "No radio found");
      return 0;
   }

   fprintf(stderr, "Connecting to radio at %s:%u\n", inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));

   pthread_mutex_init(&ctx.rx_phase_lock, NULL);
   pthread_mutex_init(&ctx.tx_phase_lock, NULL);

   struct radio_t* radio = waveform_radio_create(addr);
   struct waveform_t* test_waveform =
         waveform_create(radio, "JunkMode", "JUNK", "DIGU", "1.0.0");
   waveform_register_status_cb(test_waveform, "slice", echo_command, NULL);
   waveform_register_state_cb(test_waveform, state_test, NULL);
   waveform_register_rx_data_cb(test_waveform, packet_rx, NULL);
   waveform_register_tx_data_cb(test_waveform, packet_tx, NULL);
   waveform_register_command_cb(test_waveform, "set", test_command, NULL);
   waveform_register_meter_list(test_waveform, meters, ARRAY_SIZE(meters));
   waveform_set_context(test_waveform, &ctx);
   waveform_radio_start(radio);

   waveform_radio_wait(radio);
}