// SPDX-License-Identifier: LGPL-3.0-or-later
/// @file vita.h
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

#ifndef WAVEFORM_SDK_VITA_H
#define WAVEFORM_SDK_VITA_H

// ****************************************
// Third Party Library Includes
// ****************************************
#include <event2/event.h>
#include <pthread_workqueue.h>
#include <stdint.h>
#include <tgmath.h>

#include "waveform_api.h"

// ****************************************
// Macros
// ****************************************
#define VITA_PACKET_TYPE_EXT_DATA_WITH_STREAM_ID 0x38u
#define VITA_PACKET_TYPE_IF_DATA_WITH_STREAM_ID 0x18u


#define FLEX_OUI 0x00001c2dLLU
#define DISCOVERY_CLASS_ID ((0x534cffffLLU << 32u) | FLEX_OUI)
#define DISCOVERY_STREAM_ID 0x00000800u
#define METER_STREAM_ID 0x88000000u
#define METER_CLASS_ID ((0x534c8002LLU << 32u) | FLEX_OUI)
#define AUDIO_CLASS_ID ((0x534c03e3LLU << 32u) | FLEX_OUI)
#define DATA_CLASS_ID ((0x534c0100LLU << 32u) | FLEX_OUI)

#define VITA_PACKET_HEADER_SIZE(packet) \
   (sizeof((packet)->header))

/// @brief Swap byte order of a data structure word by word
/// @details The VITA-49 specification specifies that the byte order of the
///          words in the packet is big endian.  This means we must swap
///          each word for major portions of the packet.  This function will
///          do that swap for an arbitrary chunk of data.
/// @param data A pointer to the data to be byte swapped.
inline static void vita_swap_data(void* data)
{
   for (uint32_t* word = (uint32_t*) data; word < (uint32_t*) (data + 1); ++word)
   {
      *word = ntohl(*word);
   }
}

// ****************************************
// Structs, Enums, typedefs
// ****************************************
#pragma clang diagnostic push
#pragma ide diagnostic ignored "altera-struct-pack-align"
#pragma pack(push, 1)
struct waveform_vita_packet {
   struct
   {
      uint16_t length;
      uint8_t timestamp_type;
      uint8_t packet_type;
      uint32_t stream_id;
      uint64_t class_id;
      uint32_t timestamp_int;
      uint64_t timestamp_frac;
   } header;
   union
   {
      uint8_t raw_payload[1440];
      float if_samples[360];
      struct {
         uint32_t length;
         uint8_t data[1436];
      } byte_payload;
   };
};

struct waveform_vita_packet_sans_ts {
   struct
   {
      uint16_t length;
      uint8_t timestamp_type;
      uint8_t packet_type;
      uint32_t stream_id;
      uint64_t class_id;
   } header;
   union
   {
      uint8_t raw_payload[1452];
      //  Note that these are *intentionally* backwards.  This way when the byte
      //  swap happens, everything ends up in the correct order that SSDR expects.
      //  This order probably isn't technically correct (VITA likes to work per-word),
      //  But that's how it's coded in SSDR.
      struct {
         uint16_t value;
         uint16_t id;
      } meter[363];
   };
};
#pragma pack(pop)

struct vita {
   int sock;
   unsigned short port;// XXX Do we really need to keep this around?
   pthread_t thread;
   struct event_base* base;
   struct event* read_evt;
   _Atomic uint8_t meter_sequence;
   _Atomic uint8_t data_sequence;
   uint32_t tx_stream_id;
   uint32_t rx_stream_id;
   uint32_t tx_bytes_stream_id;
   uint32_t rx_bytes_stream_id;
};
#pragma clang diagnostic pop

// ****************************************
// Global Functions
// ****************************************
/// @brief Create a VITA-49 processing loop on a waveform
/// @details When the waveform becomes active, we will want to create an event loop upon which to process the data.
///          Call this function to create and initialize the loop.
/// @param wf The waveform upon which to create the loop.
/// @returns 0 on success or -1 on failure.
int vita_init(struct waveform_t* wf);

/// @brief Sends a VITA packet to the radio
/// @details This is a low level function to send data to the radio.  It is designed to be generic enough to send data packets
///          as well as send meter packets.  The user is expected to have filled in the appropriate parts of the packet including
///          the packet type, class id, the number of samples in the packet (only in the payload, not header), sequence number for
///          the stream, and the stream id itself.
/// @param vita The VITA loop to which to send the packet
/// @param packet a reference to the packet contents
/// @returns 0 on success or a negative value on an error.  Return values are negative values of errno.h and will return
///          -E2BIG on a short write to the network.
ssize_t vita_send_packet(struct vita* vita, struct waveform_vita_packet* packet);

/// @brief Sends a data packet to the radio
/// @details
/// @param vita The VITA loop to which to send the packet
/// @param samples A reference to an array of floating point samples to send
/// @param num_samples The number of floating point samples in the samples array
/// @param type The type of data packet to send, either TRANSMITTER_DATA to send the samples to the radio transmitter, or SPEAKER_DATA
///        to send it to the radio's speaker.
/// @returns 0 on success or a negative value on an error.  Return values are negative values of errno.h and will return
///          -E2BIG on a short write to the network.
ssize_t vita_send_data_packet(struct vita* vita, float* samples, size_t num_samples, enum waveform_packet_type type);

/// @brief Stops a VITA processing loop and releases all of its resources
/// @details When you are done using a VITA loop use this function to clean up resources.  Usage would be, for example, when the
///          waveform becomes inactive because the user has selected another mode.
/// @param wf The waveform upon which to stop the VITA loop
void vita_destroy(struct waveform_t* wf);

#endif//WAVEFORM_SDK_VITA_H
