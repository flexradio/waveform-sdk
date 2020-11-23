// SPDX-License-Identifier: LGPL-3.0-or-later
/// @file meters.c
/// @brief Implementation of the metering components of the API
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
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

// ****************************************
// Third Party Library Includes
// ****************************************
#include <sds.h>
#include <utlist.h>

// ****************************************
// Project Includes
// ****************************************
#include "meters.h"
#include "utils.h"
#include "waveform.h"
#include "waveform_api.h"

// ****************************************
// Structs, Enums, typedefs
// ****************************************
struct unit_info {
   const uint32_t radix;
   const char* name;
   const float min;
   const float max;
};

// ****************************************
// Constants
// ****************************************
static const struct unit_info units[] = {
      [DB] = {
            .name = "DB",
            .radix = 7,
            .min = -255.0f,
            .max = 255.0f},
      [DBM] = {.name = "DBM", .radix = 7, .min = -255.0f, .max = 255.0f},
      [DBFS] = {.name = "DBFS", .radix = 7, .min = -255.0f, .max = 255.0f},
      [VOLTS] = {.name = "VOLTS", .radix = 8, .min = -127.0f, .max = 127.0f},
      [AMPS] = {.name = "AMPS", .radix = 8, .min = -127.0f, .max = 127.0f},
      [RPM] = {.name = "RPM", .radix = 0, .min = (float) INT16_MIN, .max = (float) INT16_MAX},
      [TEMP_F] = {.name = "TEMP_F", .radix = 6, .min = -511.0f, .max = 511.0f},
      [TEMP_C] = {.name = "TEMP_C", .radix = 6, .min = -511.0f, .max = 511.0f},
      [SWR] = {.name = "SWR", .radix = 7, .min = -255.0f, .max = 255.0f},
      [WATTS] = {.name = "WATTS", .radix = 0, .min = (float) INT16_MIN, .max = (float) INT16_MAX},
      [PERCENT] = {.name = "PERCENT", .radix = 0, .min = (float) INT16_MIN, .max = (float) INT16_MAX},
      [NONE] = {.name = "NONE", .radix = 0, .min = (float) INT16_MIN, .max = (float) INT16_MAX}};

// ****************************************
// Static Functions
// ****************************************

/// @brief Callback for meter registration
/// @details When we send a call to register a meter in waveform_create_meters, we need a callback to
///          harvest the meter id number from the return.  This callback works with the waveform_send_api_command
///          infrastructure to harvest that meter id and handle the return.
/// @param waveform Reference to the waveform structure
/// @param code Return code from the radio API
/// @param message Text message from teh radio API's response
/// @param arg Argument passed to waveform_send_api_command_cb, in this case the waveform_meter structure that
///            we are working with for this callback.
static void register_meter_cb(struct waveform_t* waveform, unsigned int code, sds message, void* arg)
{
   char* endptr;
   unsigned long id;

   struct waveform_meter* entry = (struct waveform_meter*) arg;
   if (code != 0)
   {
      waveform_log(WF_LOG_ERROR, "Error registering meter: %s (%d)", message, code);
      goto register_failed;
   }

   id = strtoul(message, &endptr, 10);
   if ((errno == ERANGE && id == ULONG_MAX) || (errno != 0 && id == 0))
   {
      waveform_log(WF_LOG_ERROR, "Error finding meter id: %s\n", strerror(errno));
      goto register_failed;
   }

   if (endptr == message)
   {
      waveform_log(WF_LOG_ERROR, "Cannot find meter id in: %s\n", message);
      goto register_failed;
   }

   if (id > UINT16_MAX)
   {
      waveform_log(WF_LOG_ERROR, "Meter ID is out of range: %lu\n", id);
      goto register_failed;
   }

   entry->id = (uint16_t) id;
   return;

register_failed:
   LL_DELETE(waveform->meter_head, entry);
   free(entry);
}

/// @brief Finds a meter structure given its name
/// @param wf Reference to the waveform the meter is in
/// @param name The name of the meter to find
/// @returns A pointer to the waveform meter structure representing the meter or NULL if
///          no meter was found.  The user should not free this structure.
static struct waveform_meter* find_meter_by_name(struct waveform_t* wf, const char* name)
{
   struct waveform_meter* meter;

   LL_FOREACH(wf->meter_head, meter)
   {
      if (strcmp(meter->name, name) == 0)
      {
         return meter;
      }
   }

   waveform_log(WF_LOG_ERROR, "Meter not found: %s\n", name);

   return NULL;
}

// ****************************************
// Global Functions
// ****************************************
void waveform_create_meters(struct waveform_t* wf)
{
   struct waveform_meter* meter;

   LL_FOREACH(wf->meter_head, meter)
   {
      waveform_send_api_command_cb(wf, register_meter_cb, meter,
                                   "meter create name=%s type=WAVEFORM min=%f max=%f unit=%s fps=20", meter->name,
                                   meter->min, meter->max, units[meter->unit].name);
   }
}

// ****************************************
// Public API Functions
// ****************************************
void waveform_register_meter(struct waveform_t* wf, const char* name, float min, float max, enum waveform_units unit)
{
   struct waveform_meter* meter;

   if (find_meter_by_name(wf, name) != NULL)
   {
      waveform_log(WF_LOG_ERROR, "Meter %s already exists\n", name);
      return;
   }

   struct waveform_meter* new_entry = calloc(1, sizeof(*new_entry));
   new_entry->name = sdsnew(name);
   new_entry->min = min;
   new_entry->max = max;
   new_entry->unit = unit;
   new_entry->value = -1;
   LL_APPEND(wf->meter_head, new_entry);
}

inline void waveform_register_meter_list(struct waveform_t* wf, const struct waveform_meter_entry list[],
                                         int num_meters)
{
   for (int i = 0; i < num_meters; ++i)
   {
      waveform_register_meter(wf, list[i].name, list[i].min, list[i].max, list[i].unit);
   }
}

int waveform_meter_set_int_value(struct waveform_t* wf, char* name, short value)
{
   struct waveform_meter* meter;

   if ((meter = find_meter_by_name(wf, name)) == NULL) {
      waveform_log(WF_LOG_ERROR, "Meter not found: %s\n", name);
      return -1;
   }

   meter->value = value;
   return 0;
}

int waveform_meter_set_float_value(struct waveform_t* wf, char* name, float value)
{
   struct waveform_meter* meter;

   if ((meter = find_meter_by_name(wf, name)) == NULL)
   {
      waveform_log(WF_LOG_ERROR, "Meter not found: %s\n", name);
      return -1;
   }

   if (value > units[meter->unit].max || value < units[meter->unit].min)
   {
      waveform_log(WF_LOG_ERROR, "Meter value %f is out of range (%f to %f)\n", value, units[meter->unit].min,
                   units[meter->unit].max);
      return -1;
   }

   meter->value = float_to_fixed(value, units[meter->unit].radix);
   return 0;
}

int waveform_meters_send(struct waveform_t* wf)
{
   struct waveform_meter* meter;
   int i = 0;
   struct waveform_vita_packet_sans_ts* packet = calloc(1, sizeof(*packet));

   packet->packet_type = VITA_PACKET_TYPE_EXT_DATA_WITH_STREAM_ID;
   packet->stream_id = METER_STREAM_ID;
   packet->class_id = METER_CLASS_ID;
   //  XXX This is an issue because the pointer dereference won't be atomic.  If two threads go at this at once
   //  XXX the value could get corrupted.  It's not greatly important, but it will screw up the sequence.  It should
   //  XXX probably be done on the IO thread, but we lose the reference to the struct vita when we call the
   //  XXX event callback.
   packet->timestamp_type = wf->vita.meter_sequence++ & 0x0fu;

   LL_FOREACH(wf->meter_head, meter)
   {
      if (i > sizeof(packet->meter) / sizeof(packet->meter[0]))
      {
         waveform_log(WF_LOG_ERROR, "Meters exceed max size\n");
         return -1;
      }

      if (meter->value != -1)
      {
         packet->meter[i].id = meter->id;
         packet->meter[i].value = meter->value;
         meter->value = -1;
         ++i;
      }
   }

   packet->length = i;

   vita_send_packet(&wf->vita, (struct waveform_vita_packet*) packet);
   return 0;
}
