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
// System Includes
// ****************************************
#include <asm/byteorder.h>
#include <stdbool.h>

// ****************************************
// Third Party Library Includes
// ****************************************
#include <event2/event.h>
#include <pthread_workqueue.h>
#include <stdint.h>
#include <tgmath.h>

// ****************************************
// Project Includes
// ****************************************
#include "utils.h"
#include "waveform_api.h"

// ****************************************
// Macros
// ****************************************
#define VITA_PACKET_HEADER_SIZE(packet) \
   ((packet)->header.integer_timestamp_type != INTEGER_TIMESTAMP_NOT_PRESENT ? MEMBER_SIZE(struct waveform_vita_packet, header) : MEMBER_SIZE(struct waveform_vita_packet_sans_ts, header))

// ****************************************
// Structures, Enums, typedefs
// ****************************************
enum vita_packet_type
{
   VITA_PACKET_TYPE_IF_DATA_WITHOUT_STREAM_ID = 0x00,
   VITA_PACKET_TYPE_IF_DATA_WITH_STREAM_ID = 0x01,
   VITA_PACKET_TYPE_EXT_DATA_WITHOUT_STREAM_ID = 0x02,
   VITA_PACKET_TYPE_EXT_DATA_WITH_STREAM_ID = 0x03,
   VITA_PACKET_TYPE_CTX = 0x04,
   VITA_PACKET_TYPE_EXT_CTX = 0x05,
   VITA_PACKET_TYPE_CMD = 0x06,
   VITA_PACKET_TYPE_EXT_CMD = 0x07
};

// Integer timestamp constants
enum integer_timestamp_type
{
   INTEGER_TIMESTAMP_NOT_PRESENT = 0x00,
   INTEGER_TIMESTAMP_UTC = 0x01,
   INTEGER_TIMESTAMP_GPS = 0x02,
   INTEGER_TIMESTAMP_OTHER = 0x03,
};

// Fractional timestamp constants
enum fractional_timestamp_type
{
   FRACTIONAL_TIMESTAMP_NOT_PRESENT = 0x00,
   FRACTIONAL_TIMESTAMP_SAMPLE_COUNT = 0x01,
   FRACTIONAL_TIMESTAMP_REAL_TIME = 0x02,
   FRACTIONAL_TIMESTAMP_FREE_RUNNING_COUNT = 0x03
};

enum sample_rate
{
   SR_3K = 0x00,
   SR_6K = 0x01,
   SR_12K = 0x02,
   SR_24K = 0x03,
   SR_48K = 0x04,
   SR_96K = 0x05,
   SR_192K = 0x06,
   SR_384K = 0x07,
   SR_768K = 0x08,
   SR_1568K = 0x09,
   SR_3072K = 0x0A,
   SR_6144K = 0x0B,
   SR_12288K = 0x0C,
   SR_24576K = 0x0D,
   SR_49152K = 0x0E,
   SR_98304K = 0x0F,
   SR_4K = 0x10,
   SR_8K = 0x11,
   SR_16K = 0x12,
   SR_32K = 0x13,
   SR_64K = 0x14,
   SR_128K = 0x15,
   SR_256K = 0x16,
   SR_512K = 0x17,
   SR_1024K = 0x18,
   SR_2048K = 0x19,
   SR_4096K = 0x1A,
   SR_8192K = 0x1B,
   SR_16384K = 0x1C,
   SR_32768K = 0x1D,
   SR_65536K = 0x1E,
   SR_131072K = 0x1F
};

enum bits_per_sample
{
   BPS_8 = 0x00,
   BPS_16 = 0x01,
   BPS_24 = 0x02,
   BPS_32 = 0x03
};

enum frames_per_sample
{
   FPS_1 = 0x00,
   FPS_2 = 0x01
};

#pragma clang diagnostic push
#pragma ide diagnostic ignored "altera-struct-pack-align"
#pragma pack(push, 1)
struct waveform_vita_packet {
   struct
   {
#if __BYTE_ORDER == __LITTLE_ENDIAN
      uint8_t reserved1 : 2;
      bool trailer_present : 1;
      bool class_present : 1;
      enum vita_packet_type packet_type : 4;

      uint8_t sequence : 4;
      enum fractional_timestamp_type fractional_timestamp_type : 2;
      enum integer_timestamp_type integer_timestamp_type : 2;

      uint16_t length;
#elif __BYTE_ORDER == __BIG_ENDIAN
      uint8_t packet_type : 4;
      bool class_present : 1;
      bool trailer_present : 1;
      uint8_t reserved1 : 2;

      uint8_t integer_timestamp : 2;
      uint8_t fractional_timestamp : 2;
      uint8_t sequence : 4;

      uint16_t length;
#else
#error "Please fix <bits/endian.h>"
#endif
      uint32_t stream_id;
#if __BYTE_ORDER == __LITTLE_ENDIAN
      uint32_t oui;
      uint16_t information_class;
      union
      {
         uint16_t packet_class_byte;
         struct {
            bool is_audio : 1;
            bool is_float : 1;
            uint8_t padding : 6;

            enum sample_rate sample_rate : 5;
            enum bits_per_sample bits_per_sample : 2;
            enum frames_per_sample frames_per_sample : 1;
         } packet_class;
      };
#elif __BYTE_ORDER == __BIG_ENDIAN
      uint32_t oui;
      uint16_t information_class;
      union
      {
         uint16_t packet_class_byte;
         struct {
            uint8_t padding : 6;
            bool is_float : 1;
            bool is_audio : 1;

            enum frames_per_sample frames_per_sample : 1;
            enum bits_per_sample bits_per_sample : 2;
            enum sample_rate sample : rate 5;
         } packet_class;
      };
#else
#error "Please fix <bits/endian.h>"
#endif
      uint32_t timestamp_int;
      uint64_t timestamp_frac;
   } header;
   union
   {
      uint8_t raw_payload[1440];
      uint32_t word_payload[360];
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
#if __BYTE_ORDER == __LITTLE_ENDIAN
      uint8_t reserved1 : 2;
      bool trailer_present : 1;
      bool class_present : 1;
      enum vita_packet_type packet_type : 4;

      uint8_t sequence : 4;
      enum fractional_timestamp_type fractional_timestamp_type : 2;
      enum integer_timestamp_type integer_timestamp_type : 2;

      uint16_t length;
#elif __BYTE_ORDER == __BIG_ENDIAN
      uint8_t packet_type : 4;
      bool class_present : 1;
      bool trailer_present : 1;
      uint8_t reserved1 : 2;

      uint8_t integer_timestamp : 2;
      uint8_t fractional_timestamp : 2;
      uint8_t sequence : 4;

      uint16_t length;
#else
#error "Please fix <bits/endian.h>"
#endif
      uint32_t stream_id;
#if __BYTE_ORDER == __LITTLE_ENDIAN
      uint32_t oui;
      uint16_t information_class;
      union
      {
         uint16_t packet_class_byte;
         struct {
            bool is_audio : 1;
            bool is_float : 1;
            uint8_t padding : 6;

            enum sample_rate sample_rate : 5;
            enum bits_per_sample bits_per_sample : 2;
            enum frames_per_sample frames_per_sample : 1;
         } packet_class;
      };
#elif __BYTE_ORDER == __BIG_ENDIAN
      uint32_t oui;
      uint16_t information_class;
      union
      {
         uint16_t packet_class_byte;
         struct {
            uint8_t padding : 6;
            bool is_float : 1;
            bool is_audio : 1;

            enum frames_per_sample frames_per_sample : 1;
            enum bits_per_sample bits_per_sample : 2;
            enum sample_rate sample : rate 5;
         } packet_class;
      };
#else
#error "Please fix <bits/endian.h>"
#endif
   } header;

   union
   {
      uint8_t raw_payload[1452];
      struct {
         uint16_t id;
         uint16_t value;
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
   uint32_t tx_stream_in_id;
   uint32_t rx_stream_in_id;
   uint32_t tx_stream_out_id;
   uint32_t rx_stream_out_id;
   uint32_t byte_stream_in_id;
   uint32_t byte_stream_out_id;
};
#pragma clang diagnostic pop

// ****************************************
// Constants
// ****************************************
#define DISCOVERY_STREAM_ID 0x00000800u
#define METER_STREAM_ID 0x88000000u

#define FLEX_OUI 0x00001c2dU
#define SMOOTHLAKE_INFORMATION_CLASS 0x534cU

#define METER_PACKET_CLASS 0x8002

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

/// @brief Sends a raw byte data packet to the radio
/// @details
/// @param vita The VITA loop to which to send the packet
/// @param data A reference to an array of bytes to send
/// @param data_size The number of bytes in the samples array
/// @param type The type of data packet to send, either RAW_DATA_TX to send the samples to the radio transmitter, or RAW_DATA_RX
///        to send it to the radio's serial port.
/// @returns 0 on success or a negative value on an error.  Return values are negative values of errno.h and will return
///          -E2BIG on a short write to the network.
ssize_t vita_send_byte_data_packet(struct vita* vita, void* data, size_t data_size, enum waveform_packet_type type);

/// @brief Stops a VITA processing loop and releases all of its resources
/// @details When you are done using a VITA loop use this function to clean up resources.  Usage would be, for example, when the
///          waveform becomes inactive because the user has selected another mode.
/// @param wf The waveform upon which to stop the VITA loop
void vita_destroy(struct waveform_t* wf);

#endif//WAVEFORM_SDK_VITA_H
