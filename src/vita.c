// SPDX-License-Identifier: LGPL-3.0-or-later
/// @file vita.c
/// @brief Implementation of VITA packet processing
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
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

// ****************************************
// Third Party Library Includes
// ****************************************
#include <event2/event.h>
#include <utlist.h>

// ****************************************
// Project Includes
// ****************************************
#include "radio.h"
#include "utils.h"
#include "vita.h"
#include "waveform.h"

// ****************************************
// Project Includes
// ****************************************
static const uint16_t vita_port = 4991;

// ****************************************
// Structs, Enums, typedefs
// ****************************************
struct data_cb_wq_desc {
   struct waveform_cb_list* cb;
   struct waveform_vita_packet packet;
   size_t packet_size;
   struct waveform_t* wf;
   struct data_cb_wq_desc* next;
};

// ****************************************
// Static Variables
// ****************************************
static sem_t wq_sem;
static pthread_mutex_t wq_lock;
static struct data_cb_wq_desc* wq = NULL;
static _Atomic bool wq_running = false;
static pthread_t wq_thread;
static struct sockaddr_in radio_addr;

// ****************************************
// Static Functions
// ****************************************

/// @brief Test whether a packet is a transmit or receive packet
/// @details A transmit packet will have a 1 in the least significant bit of the class
///          ID of the packet, where a receive packet will have a 0.  Return true or
///          false depending on the type of packet.
/// @param packet The packet to test
/// @returns true for a transmit packet, else false.
inline static bool is_transmit_packet(struct waveform_vita_packet* packet)
{
   if (packet->stream_id & 0x0001U)
   {
      return true;
   }

   return false;
}

/// @brief Libevent callback for when a VITA packet is read from the UDP socket.
/// @details When a packet is recieved from the network, libevent calls this callback to let us know.  In here we do all of
///          our initial packet processing and sanity checks and bit flipping before calling the appropriate user callback
///          function.
/// @param socket The socket upon which the VITA packet was received
/// @param what The event type that occurred
/// @param ctx A reference to the VITA structure for the processing loop.
static void vita_read_cb(evutil_socket_t socket, short what, void* ctx)
{
   struct vita* vita = (struct vita*) ctx;
   ssize_t bytes_received;
   struct waveform_vita_packet packet;

   if (!(what & EV_READ))
   {
      waveform_log(WF_LOG_INFO, "Callback is not for a read?!\n");
      return;
   }

   if ((bytes_received = recv(socket, &packet, sizeof(packet), 0)) == -1)
   {
      waveform_log(WF_LOG_ERROR, "VITA read failed: %s\n", strerror(errno));
      return;
   }

   // Byte swap the whole packet because we're going to pass it to the user, and we'll assume they want
   // host byte order, otherwise it's potentially confusing.
   // Hopefully the compiler will vector optimize this, because there should be NEON instructions for 4-wide
   // byte swap.  If it doesn't, we should do it ourselves.
   for (uint32_t* word = (uint32_t*) &packet; word < (uint32_t*) (&packet + 1); ++word)
   {
      *word = ntohl(*word);
   }

   swap_frac_timestamp((uint32_t*) &(packet.timestamp_frac));

   unsigned long payload_length = (packet.length * sizeof(uint32_t)) - VITA_PACKET_HEADER_SIZE(&packet);

   if (payload_length != bytes_received - VITA_PACKET_HEADER_SIZE(&packet))
   {
      waveform_log(WF_LOG_INFO, "VITA header size doesn't match bytes read from network (%lu != %ld - %lu) -- %lu\n",
                   payload_length, bytes_received, VITA_PACKET_HEADER_SIZE(&packet), sizeof(struct waveform_vita_packet));
      return;
   }

   struct waveform_t* cur_wf = container_of(vita, struct waveform_t, vita);

   struct waveform_cb_list* cb_list;
   if (packet.class_id == AUDIO_CLASS_ID)
   {
      if (is_transmit_packet(&packet))
      {
         cb_list = cur_wf->tx_data_cbs;
         vita->tx_stream_id = packet.stream_id;
      }
      else
      {
         cb_list = cur_wf->rx_data_cbs;
         vita->rx_stream_id = packet.stream_id;
      }
   }
   else
   {
      cb_list = cur_wf->unk_data_cbs;
   }

   struct waveform_cb_list* cur_cb;
   LL_FOREACH(cb_list, cur_cb)
   {
      struct data_cb_wq_desc* desc = calloc(1, sizeof(*desc));// Freed when taken out of linked list

      desc->wf = cur_wf;
      memcpy(&desc->packet, &packet, bytes_received);
      desc->packet_size = bytes_received;
      desc->cb = cur_cb;

      pthread_mutex_lock(&wq_lock);
      LL_APPEND(wq, desc);
      pthread_mutex_unlock(&wq_lock);

      sem_post(&wq_sem);
   }
}

/// @brief VITA processing event loop
/// @details An event loop for VITA processing that opens a socket to communicate with the radio, sets up
///          appropriate callbacks so that we can handle events and then executes event_base_dispatch
///          which will run in an infinite loop until there is no more connection there.  See the libevent
///          documentation for more details.
/// @param arg The VITA struct for which to run the event loop
static void* vita_evt_loop(void* arg)
{
   struct waveform_t* wf = (struct waveform_t*) arg;
   struct vita* vita = &(wf->vita);
   int ret;

   struct sched_param thread_fifo_priority = {
         .sched_priority = sched_get_priority_max(SCHED_FIFO)};
   ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &thread_fifo_priority);
   if (ret)
   {
      waveform_log(WF_LOG_DEBUG, "Setting thread to realtime: %s\n", strerror(ret));
   }

   struct sockaddr_in bind_addr = {
         .sin_family = AF_INET,
         .sin_addr.s_addr = htonl(INADDR_ANY),
         .sin_port = 0,
   };
   socklen_t bind_addr_len = sizeof(bind_addr);

   // TODO: This needs to come back in when the radio does sane stuff with ports again
   //   struct sockaddr_in radio_addr = {
   //         .sin_family = AF_INET,
   //         .sin_addr.s_addr = wf->radio->addr.sin_addr.s_addr,
   //         .sin_port = htons(vita_port)};
   radio_addr.sin_family = AF_INET;
   radio_addr.sin_addr.s_addr = wf->radio->addr.sin_addr.s_addr;
   radio_addr.sin_port = htons(vita_port);

   waveform_log(WF_LOG_DEBUG, "Initializing VITA-49 engine...\n");

   vita->sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
   if (vita->sock == -1)
   {
      waveform_log(WF_LOG_ERROR, " Failed to initialize VITA socket: %s\n", strerror(errno));
      goto fail;
   }

   if (bind(vita->sock, (struct sockaddr*) &bind_addr, sizeof(bind_addr)))
   {
      waveform_log(WF_LOG_ERROR, "error binding socket: %s\n", strerror(errno));
      goto fail_socket;
   }

   // TODO: This needs to come back in when the radio does sane stuff with ports again
   //   if (connect(vita->sock, (struct sockaddr*) &radio_addr, sizeof(struct sockaddr_in)) == -1)
   //   {
   //      waveform_log(WF_LOG_ERROR, "Couldn't connect socket: %s\n", strerror(errno));
   //      goto fail_socket;
   //   }

   if (getsockname(vita->sock, (struct sockaddr*) &bind_addr, &bind_addr_len) == -1)
   {
      waveform_log(WF_LOG_ERROR, "Couldn't get port number of VITA socket\n");
      goto fail_socket;
   }

   vita->base = event_base_new();
   if (!vita->base)
   {
      waveform_log(WF_LOG_ERROR, "Couldn't create VITA event base\n");
      goto fail_socket;
   }

   vita->read_evt = event_new(vita->base, vita->sock, EV_READ | EV_PERSIST, vita_read_cb, vita);
   if (!vita->read_evt)
   {
      waveform_log(WF_LOG_ERROR, "Couldn't create VITA read event\n");
      goto fail_base;
   }
   if (event_add(vita->read_evt, NULL) == -1)
   {
      waveform_log(WF_LOG_ERROR, "Couldn't add VITA read event to base\n");
      goto fail_evt;
   }

   vita->port = ntohs(bind_addr.sin_port);

   vita->data_sequence = 0;
   vita->meter_sequence = 0;

   waveform_send_api_command_cb(wf, NULL, NULL, "waveform set %s udpport=%hu", wf->name, vita->port);
   waveform_send_api_command_cb(wf, NULL, NULL, "client udpport %hu", vita->port);

   event_base_dispatch(vita->base);

   waveform_log(WF_LOG_DEBUG, "VITA thread ending...\n");

fail_evt:
   event_free(vita->read_evt);
fail_base:
   event_base_free(vita->base);
fail_socket:
   close(vita->sock);
   vita->sock = 0;
fail:
   return NULL;
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
/// @brief Data callback event loop
/// @details An event loop for running user-defined data callbacks.  This runs and
///          takes tasks from the wq linked list and executes them in order.  This
///          thread terminates when the main VITA loop terminates.
static void* vita_cb_loop(void* arg __attribute__((unused)))
{
   struct timespec timeout;
   int ret;

   //  Run this as a lower priority than the thread servicing the socket
   struct sched_param thread_fifo_priority = {
         .sched_priority = sched_get_priority_max(SCHED_FIFO) - 8};
   ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &thread_fifo_priority);
   if (ret)
   {
      waveform_log(WF_LOG_DEBUG, "Setting thread to realtime: %s\n", strerror(ret));
   }

   wq_running = true;
   while (wq_running)
   {

      if (clock_gettime(CLOCK_REALTIME, &timeout) == -1)
      {
         waveform_log(WF_LOG_ERROR, "Couldn't gert time.\n");
         continue;
      }

      timeout.tv_sec += 1;

      while ((ret = sem_timedwait(&wq_sem, &timeout)) == -1 && errno == EINTR)
         ;

      if (ret == -1)
      {
         if (errno == ETIMEDOUT)
         {
            continue;
         }
         else
         {
            waveform_log(WF_LOG_ERROR, "Error acquiring semaphore: %s\n", strerror(errno));
            continue;
         }
      }

      if (wq == NULL)
      {
         waveform_log(WF_LOG_WARNING, "Thread awakened but nothing is in the queue?\n");
         continue;
      }

      struct data_cb_wq_desc* current_task = wq;
      pthread_mutex_lock(&wq_lock);
      LL_DELETE(wq, current_task);
      pthread_mutex_unlock(&wq_lock);

      (current_task->cb->data_cb)(current_task->wf, &current_task->packet, current_task->packet_size, current_task->cb->arg);

      free(current_task);
   }

   return NULL;
}
#pragma clang diagnostic pop

/// @brief Prepare a vita packet for processing
/// @details Fills in various calculated fields in the packet to ready it for
///          transmission.  This includes calculating th epacket size, setting
///          the timestamp and resolving the byte order.
/// @param packet The packet to prepare
/// @returns The length of the packet to be sent.
static size_t vita_prep_packet(struct waveform_vita_packet* packet)
{
   size_t packet_len;

   if ((packet->timestamp_type & 0x50u) != 0)
   {
      packet->timestamp_int = time(NULL);
      packet->timestamp_frac = 0;
   }

   packet_len = VITA_PACKET_HEADER_SIZE(packet) + (packet->length * sizeof(float));
   packet->length += (VITA_PACKET_HEADER_SIZE(packet) / 4);
   assert(packet_len % 4 == 0);

   // Hopefully the compiler will vector optimize this, because there should be NEON instructions for 4-wide
   // byte swap.  If it doesn't, we should do it ourselves.
   for (uint32_t* word = (uint32_t*) packet; word < (uint32_t*) (packet + 1); ++word)
   {
      *word = htonl(*word);
   }

   swap_frac_timestamp((uint32_t*) &(packet->timestamp_frac));

   return packet_len;
}

// ****************************************
// Global Functions
// ****************************************
int vita_init(struct waveform_t* wf)
{
   int ret;

   sem_init(&wq_sem, 0, 0);

   ret = pthread_create(&wq_thread, NULL, vita_cb_loop, NULL);
   if (ret)
   {
      waveform_log(WF_LOG_FATAL, "Cannot create work queue thread: %s\n", strerror(ret));
      return -1;
   }

   ret = pthread_create(&wf->vita.thread, NULL, vita_evt_loop, wf);
   if (ret)
   {
      waveform_log(WF_LOG_ERROR, "Creating thread: %s\n", strerror(ret));
      return -1;
   }

   return 0;
}

void vita_destroy(struct waveform_t* wf)
{
   if (wf->vita.sock == 0)
   {
      waveform_log(WF_LOG_INFO, "Waveform is not running, not trying to destory again\n");
      return;
   }

   // Stop both the threads
   wq_running = false;
   pthread_join(wq_thread, NULL);
   sem_destroy(&wq_sem);

   event_base_loopexit(wf->vita.base, NULL);

   //  Clean up the callback work queue
   struct data_cb_wq_desc* task;
   struct data_cb_wq_desc* tmp;
   LL_FOREACH_SAFE(wq, task, tmp)
   {
      LL_DELETE(wq, task);
      free(task);
   }
}

ssize_t vita_send_packet(struct vita* vita, struct waveform_vita_packet* packet)
{
   size_t len = vita_prep_packet(packet);

   ssize_t bytes_sent;
   //   if ((bytes_sent = send(vita->sock, packet, len, 0)) == -1)
   if ((bytes_sent = sendto(vita->sock, packet, len, 0, &radio_addr, sizeof(radio_addr))) == -1)
   {
      waveform_log(WF_LOG_ERROR, "Error sending vita packet: %d\n", errno);
      return -errno;
   }

   if (bytes_sent != len)
   {
      waveform_log(WF_LOG_ERROR, "Short write on vita send\n");
      return -E2BIG;
   }

   return 0;
}

ssize_t vita_send_data_packet(struct vita* vita, float* samples, size_t num_samples, enum waveform_packet_type type)
{
   if (num_samples * sizeof(float) > MEMBER_SIZE(struct waveform_vita_packet, raw_payload))
   {
      waveform_log(WF_LOG_ERROR, "%lu samples exceeds maximum sending limit of %lu samples\n", num_samples,
                   MEMBER_SIZE(struct waveform_vita_packet, raw_payload) / sizeof(float));
      return -EFBIG;
   }

   //  We go ahead and allocate a queue entry here because we don't want to copy it unnecessarily if we have to queue it in
   //  vita_send_packet.  The overhead here is really only the size of a pointer, which isn't very big.
   struct waveform_vita_packet* packet = calloc(1, sizeof(*packet));

   packet->packet_type = VITA_PACKET_TYPE_IF_DATA_WITH_STREAM_ID;
   packet->class_id = AUDIO_CLASS_ID;
   packet->length = num_samples;// Length is in 32-bit words

   packet->timestamp_type = 0x50U | (vita->data_sequence++ & 0x0fu);

   switch (type)
   {
      case TRANSMITTER_DATA:
         packet->stream_id = vita->tx_stream_id;
         break;
      case SPEAKER_DATA:
         packet->stream_id = vita->rx_stream_id;
         break;
      default:
         waveform_log(WF_LOG_INFO, "Invalid packet type!\n");
         break;
   }

   memcpy(packet->raw_payload, samples, num_samples * sizeof(float));
   return vita_send_packet(vita, packet);
}

// ****************************************
// Public API Functions
// ****************************************
inline uint16_t get_packet_len(struct waveform_vita_packet* packet)
{
   return packet->length - (VITA_PACKET_HEADER_SIZE(packet) / sizeof(uint32_t));
}

inline float* get_packet_data(struct waveform_vita_packet* packet)
{
   return packet->if_samples;
}

inline uint32_t get_packet_ts_int(struct waveform_vita_packet* packet)
{
   return packet->timestamp_int;
}

inline uint64_t get_packet_ts_frac(struct waveform_vita_packet* packet)
{
   return packet->timestamp_frac;
}

inline void get_packet_ts(struct waveform_vita_packet* packet, struct timespec* ts)
{
   ts->tv_sec = packet->timestamp_int;
   ts->tv_nsec = packet->timestamp_frac / 1000;
}


inline uint32_t get_stream_id(struct waveform_vita_packet* packet)
{
   return packet->stream_id;
}

inline uint64_t get_class_id(struct waveform_vita_packet* packet)
{
   return packet->class_id;
}

inline uint8_t get_packet_count(struct waveform_vita_packet* packet)
{
   return packet->timestamp_type & 0x0fu;
}