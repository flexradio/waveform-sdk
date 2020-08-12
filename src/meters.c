#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <sds.h>
#include <utlist.h>

#include "meters.h"
#include "utils.h"
#include "waveform.h"
#include "waveform_api.h"

static const char* unit_to_string(enum waveform_units unit)
{
   switch (unit)
   {
      case DB:
         return ("DB");
      case DBM:
         return ("DBM");
      case DBFS:
         return ("DBFS");
      case VOLTS:
         return ("VOLTS");
      case AMPS:
         return ("AMPS");
      case RPM:
         return ("RPM");
      case TEMP_F:
         return ("TEMP_F");
      case TEMP_C:
         return ("TEMP_C");
      case SWR:
         return ("SWR");
      case WATTS:
         return ("WATTS");
      case PERCENT:
         return ("PERCENT");
      case NONE:
      default:
         return ("NONE");
   }
}

static void register_meter_cb(struct waveform_t* waveform, unsigned int code, sds message, void* arg)
{
   char* endptr;
   unsigned long id;

   struct waveform_meter* entry = (struct waveform_meter*) arg;
   if (code != 0)
   {
      fprintf(stderr, "Error registering meter: %s (%d)", message, code);
      goto register_failed;
   }

   id = strtoul(message, &endptr, 10);
   if ((errno == ERANGE && id == ULONG_MAX) || (errno != 0 && id == 0))
   {
      fprintf(stderr, "Error finding meter id: %s\n", strerror(errno));
      goto register_failed;
   }

   if (endptr == message)
   {
      fprintf(stderr, "Cannot find meter id in: %s\n", message);
      goto register_failed;
   }

   if (id > UINT16_MAX)
   {
      fprintf(stderr, "Meter ID is out of range: %lu\n", id);
      goto register_failed;
   }

   entry->id = (uint16_t) id;
   return;

register_failed:
   LL_DELETE(waveform->meter_head, entry);
   free(entry);
}

void waveform_register_meter(struct waveform_t* wf, const char* name, float min, float max, enum waveform_units unit)
{
   struct waveform_meter* meter;

   LL_FOREACH(wf->meter_head, meter)
   {
      if (strcmp(meter->name, name) == 0)
      {
         fprintf(stderr, "Meter %s already exists\n", name);
         return;
      }
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

void waveform_create_meters(struct waveform_t* wf)
{
   struct waveform_meter* meter;

   LL_FOREACH(wf->meter_head, meter)
   {
      waveform_send_api_command_cb(wf, register_meter_cb, meter,
                                   "meter create name=%s type=WAVEFORM min=%f max=%f unit=%s fps=20", meter->name,
                                   meter->min, meter->max, unit_to_string(meter->unit));
   }
}

int waveform_meter_set_int_value(struct waveform_t* wf, char* name, short value)
{
   struct waveform_meter* meter;

   if (value > UINT16_MAX)
   {
      fprintf(stderr, "Meter value out of range: %hu\n", value);
      return -1;
   }

   LL_FOREACH(wf->meter_head, meter)
   {
      if (strcmp(meter->name, name) == 0)
      {
         meter->value = value;
         return 0;
      }
   }

   fprintf(stderr, "Meter not found: %s\n", name);
   return -1;
}

inline int waveform_meter_set_float_value(struct waveform_t* wf, char* name, float value)
{
   return waveform_meter_set_int_value(wf, name, float_to_fixed(value, 6));
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
         fprintf(stderr, "Meters exceed max size\n");
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
