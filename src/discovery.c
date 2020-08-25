// SPDX-License-Identifier: LGPL-3.0-or-later
/// @file discovery.c
/// @brief Functionality for discovering radios
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
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ****************************************
// Third Party Library Includes
// ****************************************
#include <event2/event.h>
#include <sds.h>

// ****************************************
// Project Includes
// ****************************************
#include "utils.h"
#include "vita.h"

// ****************************************
// Macros
// ****************************************
#define DISCOVERY_PORT 4992

// ****************************************
// Static Variables
// ****************************************
static struct event_base* base;

// ****************************************
// Static Functions
// ****************************************

/// @brief Callback to implement discovery when packet is received
/// @details This is a callback for the discovery event look executed by libevent whenever a packet is received
///          on the discovery port.  We are passed a pointer to a pointer of the address structure we are expected
///          to fill in if we are successful.  If not, we set the address to NULL to indicate that we didn't get
///          a proper discovery packet.  See libevent documentation for details on the callback structure for that
///          library.
/// @params sock Socket on which discovery is received
/// @params what What kind of event has occurred on the socket
/// @params ctx A pointer to a pointer of the address structure to fill in for discovery.
static void discovery_cb(evutil_socket_t sock, short what, void* ctx)
{
   struct sockaddr_in** addrptr = (struct sockaddr_in**) ctx;
   ssize_t bytes_received;
   struct waveform_vita_packet packet;
   sds ip;
   sds port_string;

   if (!(what & EV_READ))
   {
      fprintf(stderr, "Callback is not for a read!?\n");
      return;
   }

   if ((bytes_received = recv(sock, &packet, sizeof(packet), 0)) == -1)
   {
      fprintf(stderr, "Discovery read failed: %s\n", strerror(errno));
      return;
   }

   for (uint32_t* word = (uint32_t*) &packet; word < (uint32_t*) (((uint8_t*) &packet) + VITA_PACKET_HEADER_SIZE(&packet)); ++word)
   {
      *word = ntohl(*word);
   }

   if (packet.class_id != DISCOVERY_CLASS_ID)
   {
      fprintf(stderr, "Received packet with invalid ID: 0x%lX\n", packet.class_id);
      return;
   }

   if (packet.packet_type != VITA_PACKET_TYPE_EXT_DATA_WITH_STREAM_ID)
   {
      fprintf(stderr, "Received packet is not correct type: 0x%x\n", packet.packet_type);
      return;
   }

   if (packet.stream_id != DISCOVERY_STREAM_ID)
   {
      fprintf(stderr, "Received packet does not have correct stream id: 0x%x\n", packet.stream_id);
      return;
   }

   sds discovery_string = sdsnewlen(packet.raw_payload, bytes_received - VITA_PACKET_HEADER_SIZE(&packet));
   fprintf(stderr, "Discovery: %s\n", discovery_string);
   int argc;
   sds* argv = sdssplitargs(discovery_string, &argc);

   if ((ip = find_kwarg(argc, argv, "ip")) == NULL)
   {
      fprintf(stderr, "Cannot find IP in discovery packet\n");
      goto fail;
   }

   if ((port_string = find_kwarg(argc, argv, "port")) == NULL)
   {
      fprintf(stderr, "No port number in discovery packet\n");
      goto fail;
   }

   struct sockaddr_in* addr = *addrptr = calloc(1, sizeof(struct sockaddr_in));

   if (inet_aton(ip, &addr->sin_addr) == 0)
   {
      fprintf(stderr, "Received discovery has invalid IP: %s\n", ip);
      goto fail_addr;
   }

   char* endptr;
   unsigned long port = strtoul(port_string, &endptr, 10);
   if ((errno == ERANGE && port == ULONG_MAX) ||
       (errno != 0 && port == 0))
   {
      fprintf(stderr, "Error parsing port number in discovery: %s\n",
              strerror(errno));
      goto fail_addr;
   }

   if (port > USHRT_MAX)
   {
      fprintf(stderr, "Port number %lu in discovery packet is out of range\n", port);
      goto fail_addr;
   }

   addr->sin_port = htons(port);
   addr->sin_family = AF_INET;

   sdsfreesplitres(argv, argc);
   sdsfree(discovery_string);
   event_base_loopbreak(base);
   return;

fail_addr:
   free(addr);
   *addrptr = NULL;
fail:
   sdsfreesplitres(argv, argc);
   sdsfree(discovery_string);
}

static void timeout_cb(evutil_socket_t sock, short what, void* ctx)
{
   struct sockaddr_in** addrptr = ctx;
   *addrptr = NULL;
   event_base_loopbreak(base);
}

// ****************************************
// Public API Functions
// ****************************************
struct sockaddr_in* waveform_discover_radio(const struct timeval* timeout)
{
   int sock;
   const int one = 1;
   struct event* evt;
   struct event* timeout_evt;

   struct sockaddr_in* addr = NULL;

   struct sockaddr_in local_address = {
         .sin_family = AF_INET,
         .sin_addr.s_addr = htonl(INADDR_ANY),
         .sin_port = htons(DISCOVERY_PORT),
   };

   if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
   {
      fprintf(stderr, "Cannot open discovery socket: %s\n", strerror(errno));
      goto fail;
   }

   if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == -1)
   {
      fprintf(stderr, "Cannot set discovery socket for reuse: %s\n", strerror(errno));
      goto fail_socket;
   }

   if (bind(sock, (struct sockaddr*) &local_address, sizeof(local_address)) == -1)
   {
      fprintf(stderr, "Cannot bind socket on port %d: %s\n", DISCOVERY_PORT, strerror(errno));
      goto fail_socket;
   }

   if ((base = event_base_new()) == NULL)
   {
      fprintf(stderr, "Cannot create discovery event base\n");
      goto fail_socket;
   }

   if ((evt = event_new(base, sock, EV_READ | EV_PERSIST, discovery_cb, &addr)) == NULL)
   {
      fprintf(stderr, "Cannot create discovery event\n");
      goto fail_base;
   }

   if (event_add(evt, NULL) == 1)
   {
      fprintf(stderr, "Cannot add discovery event to base\n");
      goto fail_evt;
   }

   if ((timeout_evt = evtimer_new(base, timeout_cb, &addr)) == NULL)
   {
      fprintf(stderr, "Cannot create discovery timeout event\n");
      goto fail_evt;
   }

   if ((evtimer_add(timeout_evt, timeout)) == 1)
   {
      fprintf(stderr, "Cannot add discovery timeout event to base\n");
      goto fail_disc_evt;
   }

   event_base_dispatch(base);

   event_free(evt);
   event_base_free(base);
   close(sock);
   return addr;

fail_disc_evt:
   event_free(timeout_evt);
fail_evt:
   event_free(evt);
fail_base:
   event_base_free(base);
fail_socket:
   close(sock);
fail:
   free(addr);
   return NULL;
}
