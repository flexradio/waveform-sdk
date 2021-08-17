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
   if (packet->header.stream_id & 0x0001U)
   {
      return true;
   }

   return false;
}

/// @brief Byte swaps the payload of a VITA-49 packet
/// @param packet The packet whose payload to swap
inline static void vita_swap_payload(struct waveform_vita_packet* packet)
{
   size_t payload_len = packet->header.length - (VITA_PACKET_HEADER_SIZE(packet) / sizeof(uint32_t));
   for (size_t i = 0; i < payload_len; ++i)
   {
      packet->word_payload[i] = ntohl(packet->word_payload[i]);
   }
}

/// @brief Libevent callback for when a VITA packet is read from the UDP socket.
/// @details When a packet is recieved from the network, libevent calls this callback to let us know.  In here we do all of
///          our initial packet processing and sanity checks and endian flipping before calling the appropriate user callback
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

   //  Swap appropriate header fields.  We swap the static values for comparison for the class IDs,
   //  so we don't need to worry about swapping that.
   packet.header.length = ntohs(packet.header.length);
   packet.header.stream_id = ntohl(packet.header.stream_id);

   if (packet.header.integer_timestamp_type != INTEGER_TIMESTAMP_NOT_PRESENT)
   {
      packet.header.timestamp_int = htonl(packet.header.timestamp_int);
      packet.header.timestamp_frac = be64toh(packet.header.timestamp_frac);
   }

   if (packet.header.oui != __constant_cpu_to_be32(FLEX_OUI))
   {
      waveform_log(WF_LOG_INFO, "Invalid OUI: 0x%08x\n", ntohl(packet.header.oui));
      return;
   }

   unsigned long payload_length = (packet.header.length * sizeof(uint32_t)) - VITA_PACKET_HEADER_SIZE(&packet);

   if (payload_length != bytes_received - VITA_PACKET_HEADER_SIZE(&packet))
   {
      waveform_log(WF_LOG_INFO, "VITA header size doesn't match bytes read from network (%lu != %ld - %lu) -- %lu\n",
                   payload_length, bytes_received, VITA_PACKET_HEADER_SIZE(&packet), sizeof(struct waveform_vita_packet));
      return;
   }

   if (packet.header.information_class != __constant_be16_to_cpu(SMOOTHLAKE_INFORMATION_CLASS))
   {
      waveform_log(WF_LOG_INFO, "Invalid packet information class: 0x%04x\n", ntohs(packet.header.information_class));
      return;
   }

   struct waveform_t* cur_wf = container_of(vita, struct waveform_t, vita);
   struct waveform_cb_list* cb_list;

   if (packet.header.packet_type == VITA_PACKET_TYPE_IF_DATA_WITH_STREAM_ID &&
       packet.header.packet_class.is_audio &&
       packet.header.packet_class.bits_per_sample == BPS_32 &&
       packet.header.packet_class.sample_rate == SR_24K &&
       packet.header.packet_class.frames_per_sample == FPS_2 &&
       packet.header.packet_class.is_float)
   {
      //  This is an audio packet from the RX or Mic
      vita_swap_payload(&packet);
      if (is_transmit_packet(&packet))
      {
         cb_list = cur_wf->tx_data_cbs;
         vita->tx_stream_id = packet.header.stream_id;
      }
      else
      {
         cb_list = cur_wf->rx_data_cbs;
         vita->rx_stream_id = packet.header.stream_id;
      }
   }
   else if (packet.header.packet_type == VITA_PACKET_TYPE_IF_DATA_WITH_STREAM_ID &&
            packet.header.packet_class.is_audio &&
            packet.header.packet_class.bits_per_sample == BPS_8 &&
            packet.header.packet_class.sample_rate == SR_3K &&
            packet.header.packet_class.frames_per_sample == FPS_1 &&
            packet.header.packet_class.is_float)
   {
      // This is a byte data packet.
      // We don't swap the data around here so that we are transparent
      // to the user who is sending it.
      packet.byte_payload.length = ntohl(packet.byte_payload.length);
      if (is_transmit_packet(&packet))
      {
         cb_list = cur_wf->tx_byte_data_cbs;
         vita->rx_bytes_stream_id = packet.header.stream_id;
      }
      else
      {
         cb_list = cur_wf->rx_byte_data_cbs;
         vita->tx_stream_id = packet.header.stream_id;
      }
   }
   else
   {
      // This is an unknown format packet
      vita_swap_payload(&packet);
      cb_list = cur_wf->unknown_data_cbs;
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

   if ((packet->header.timestamp_type & 0x50u) != 0)
   {
      packet->header.timestamp_int = time(NULL);
      packet->header.timestamp_frac = 0;
   }

   packet_len = VITA_PACKET_HEADER_SIZE(packet) + (packet->header.length * sizeof(float));
   packet->header.length += (VITA_PACKET_HEADER_SIZE(packet) / 4);
   assert(packet_len % 4 == 0);

   // Hopefully the compiler will vector optimize this, because there should be NEON instructions for 4-wide
   // byte swap.  If it doesn't, we should do it ourselves.
   for (uint32_t* word = (uint32_t*) packet; word < (uint32_t*) (packet + 1); ++word)
   {
      *word = htonl(*word);
   }

   swap_frac_timestamp((uint32_t*) &(packet->header.timestamp_frac));

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
   packet->header.length += VITA_PACKET_HEADER_SIZE(packet) / sizeof(uint32_t);
   size_t len = packet->header.length * sizeof(uint32_t);
   packet->header.length = htons(packet->header.length);

   waveform_log(WF_LOG_DEBUG, "Transmitting Packet of length %ld bytes:\n", len);

   ssize_t bytes_sent;
   if ((bytes_sent = sendto(vita->sock, packet, len, 0, (const struct sockaddr*) &radio_addr, sizeof(struct sockaddr_in))) == -1)
   {
      char error_string[1024];
      strerror_r(errno, error_string, sizeof(error_string));
      waveform_log(WF_LOG_ERROR, "Error sending vita packet to %s: %s\n", inet_ntoa(radio_addr.sin_addr), error_string);
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

   struct timespec current_time = {0};
   if (clock_gettime(CLOCK_REALTIME, &current_time) == -1)
   {
      waveform_log(WF_LOG_INFO, "Couldn't get current time: %m\n");
      current_time.tv_sec = 0;
      current_time.tv_nsec = 0;
   }

   struct waveform_vita_packet packet = {
         .header = {
               .packet_type = VITA_PACKET_TYPE_IF_DATA_WITH_STREAM_ID,
               .class_present = true,
               .trailer_present = false,
               .integer_timestamp_type = INTEGER_TIMESTAMP_UTC,
               .fractional_timestamp_type = FRACTIONAL_TIMESTAMP_REAL_TIME,
               .sequence = vita->data_sequence++,
               .length = num_samples,
               .timestamp_int = htonl(current_time.tv_sec),
               .timestamp_frac = htobe64(current_time.tv_nsec * 1000),
               .stream_id = htonl(type == TRANSMITTER_DATA ? vita->tx_stream_id : vita->rx_stream_id),
               .oui = __constant_cpu_to_be32(FLEX_OUI),
               .information_class = __constant_cpu_to_be16(SMOOTHLAKE_INFORMATION_CLASS),
               .packet_class = {
                     .is_audio = true,
                     .is_float = true,
                     .sample_rate = SR_24K,
                     .bits_per_sample = BPS_32,
                     .frames_per_sample = FPS_2}},
         .raw_payload = {0}};

   for (size_t i = 0; i < num_samples; ++i)
   {
      packet.word_payload[i] = htonl(((uint32_t*) samples)[i]);
   }

   return vita_send_packet(vita, &packet);
}

ssize_t vita_send_raw_data_packet(struct vita* vita, void* data, size_t data_size, enum waveform_packet_type type)
{
   if (data_size > MEMBER_SIZE(struct waveform_vita_packet, byte_payload.data))
   {
      waveform_log(WF_LOG_ERROR, "%lu bytes exceeds maximum sending limit of %lu byt4es\n", data_size,
                   MEMBER_SIZE(struct waveform_vita_packet, byte_payload.data));
      return -EFBIG;
   }

   struct timespec current_time = {0};
   if (clock_gettime(CLOCK_REALTIME, &current_time) == -1)
   {
      waveform_log(WF_LOG_INFO, "Couldn't get current time: %m\n");
      current_time.tv_sec = 0;
      current_time.tv_nsec = 0;
   }

   struct waveform_vita_packet packet = {
         .header = {
               .packet_type = VITA_PACKET_TYPE_IF_DATA_WITH_STREAM_ID,
               .class_present = true,
               .trailer_present = false,
               .integer_timestamp_type = INTEGER_TIMESTAMP_UTC,
               .fractional_timestamp_type = FRACTIONAL_TIMESTAMP_REAL_TIME,
               .sequence = vita->data_sequence++,
               .length = DIV_ROUND_UP(data_size, sizeof(uint32_t)),
               .timestamp_int = htonl(current_time.tv_sec),
               .timestamp_frac = htobe64(current_time.tv_nsec * 1000),
               .stream_id = type == htonl(TRANSMITTER_DATA ? vita->tx_bytes_stream_id : vita->rx_bytes_stream_id),
               .oui = __constant_cpu_to_be32(FLEX_OUI),
               .information_class = __constant_cpu_to_be16(SMOOTHLAKE_INFORMATION_CLASS),
               .packet_class = {
                     .is_audio = true,
                     .is_float = false,
                     .sample_rate = SR_3K,
                     .bits_per_sample = BPS_8,
                     .frames_per_sample = FPS_1}},
         .byte_payload = {.length = htonl(data_size), .data = {0}}};

   memcpy(packet.byte_payload.data, data, data_size);
   return vita_send_packet(vita, &packet);
}

// ****************************************
// Public API Functions
// ****************************************
inline uint16_t get_packet_len(struct waveform_vita_packet* packet)
{
   return packet->header.length - (VITA_PACKET_HEADER_SIZE(packet) / sizeof(uint32_t));
}

inline float* get_packet_data(struct waveform_vita_packet* packet)
{
   return packet->if_samples;
}

inline uint8_t* get_packet_byte_data(struct waveform_vita_packet* packet)
{
   return packet->byte_payload.data;
}

inline uint32_t get_packet_byte_data_length(struct waveform_vita_packet* packet)
{
   return packet->byte_payload.length;
}

inline uint32_t get_packet_ts_int(struct waveform_vita_packet* packet)
{
   return packet->header.timestamp_int;
}

inline uint64_t get_packet_ts_frac(struct waveform_vita_packet* packet)
{
   return packet->header.timestamp_frac;
}

inline void get_packet_ts(struct waveform_vita_packet* packet, struct timespec* ts)
{
   ts->tv_sec = packet->header.timestamp_int;
   ts->tv_nsec = packet->header.timestamp_frac / 1000;
}


inline uint32_t get_stream_id(struct waveform_vita_packet* packet)
{
   return packet->header.stream_id;
}

inline uint64_t get_class_id(struct waveform_vita_packet* packet)
{
   return (packet->header.information_class << 16) & packet->header.packet_class_byte;
}

inline uint8_t get_packet_count(struct waveform_vita_packet* packet)
{
   return packet->header.sequence;
}