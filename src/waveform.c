// SPDX-License-Identifier: LGPL-3.0-or-later
/// @file waveform.c
/// @brief Functions to manage a waveform's lifecycle
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
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

// ****************************************
// Third Party Library Includes
// ****************************************
#include <event2/bufferevent.h>
#include <utlist.h>

// ****************************************
// Project Includes
// ****************************************
#include "radio.h"
#include "utils.h"
#include "waveform.h"

// ****************************************
// Macros
// ****************************************
// XXX I should probably be defined somewhere common.
#define MAX_STRING_SIZE 255

// ****************************************
// Global Variables
// ****************************************
struct waveform_t* wf_list;

// ****************************************
// Public API Functions
// ****************************************
struct waveform_t* waveform_create(struct radio_t* radio, const char* name,
                                   const char* short_name, const char* underlying_mode,
                                   const char* version)
{
   struct waveform_t* wave = calloc(1, sizeof(*wave));
   if (!wave)
   {
      return NULL;
   }

   wave->name = strndup(name, MAX_STRING_SIZE);
   if (!wave->name)
   {
      goto abort_name;
   }

   wave->short_name = strndup(short_name, MAX_STRING_SIZE);
   if (!wave->short_name)
   {
      goto abort_short_name;
   }

   wave->underlying_mode = strndup(underlying_mode, MAX_STRING_SIZE);
   if (!wave->underlying_mode)
   {
      goto abort_mode;
   }
   wave->version = strndup(version, MAX_STRING_SIZE);
   if (!wave->version)
   {
      goto abort_version;
   }

   wave->tx_depth = 8;
   wave->rx_depth = 8;

   wave->radio = radio;

   wave->active_slice = -1;

   if (!wf_list)
   {
      wf_list = wave;
   }
   else
   {
      struct waveform_t* cur;
      for (cur = wf_list; cur->next != NULL; cur = cur->next)
         ;
      cur->next = wave;
   }

   return wave;

abort_version:
   free(wave->underlying_mode);
abort_mode:
   free(wave->short_name);
abort_short_name:
   free(wave->name);
abort_name:
   free(wave);
   return NULL;
}

static inline void free_cb_list(struct waveform_cb_list* list)
{

   struct waveform_cb_list* cur;
   struct waveform_cb_list* tmp;
   LL_FOREACH_SAFE(list, cur, tmp)
   {
      LL_DELETE(list, cur);
      if (cur->name != NULL)
      {
         sdsfree(cur->name);
      }
      free(cur);
   }
}

void waveform_destroy(struct waveform_t* waveform)
{
   LL_DELETE(wf_list, waveform);

   free_cb_list(waveform->status_cbs);
   free_cb_list(waveform->state_cbs);
   free_cb_list(waveform->cmd_cbs);
   free_cb_list(waveform->rx_data_cbs);
   free_cb_list(waveform->tx_data_cbs);
   free_cb_list(waveform->unknown_data_cbs);

   free(waveform->name);
   free(waveform->short_name);
   free(waveform->underlying_mode);
   free(waveform);
}

inline int32_t waveform_send_api_command_cb(struct waveform_t* waveform,
                                            waveform_response_cb_t cb, void* arg,
                                            char* command, ...)
{
   va_list ap;
   long ret;

   va_start(ap, command);
   ret = waveform_radio_send_api_command_cb_va(waveform, NULL, cb, NULL, arg, command,
                                               ap);
   va_end(ap);

   return ret;
}

int32_t waveform_send_timed_api_command_cb(struct waveform_t* waveform, struct timespec* at, waveform_response_cb_t complete_cb,
                                           waveform_response_cb_t queued_cb, void* arg, char* command, ...)
{
   va_list ap;
   long ret;

   va_start(ap, command);
   ret = waveform_radio_send_api_command_cb_va(waveform, at, complete_cb, queued_cb, arg, command,
                                               ap);
   va_end(ap);

   return ret;
}

static int waveform_register_cb(struct waveform_cb_list** cb_list, const char* name,
                                waveform_cmd_cb_t cb, void* arg)
{
   // Freed in waveform_destroy()
   struct waveform_cb_list* new_cb = calloc(1, sizeof(*new_cb));
   if (!new_cb)
   {
      return -1;
   }

   if (name != NULL)
   {
      // Freed in waveform_destroy()
      new_cb->name = sdsnew(name);
      if (!new_cb->name)
      {
         free(new_cb);
         return -1;
      }
   }
   else
   {
      new_cb->name = NULL;
   }

   new_cb->cmd_cb = cb;
   new_cb->arg = arg;

   LL_APPEND(*cb_list, new_cb);

   return 0;
}

inline int waveform_register_status_cb(struct waveform_t* waveform, const char* status_name,
                                       waveform_cmd_cb_t cb, void* arg)
{
   return waveform_register_cb(&waveform->status_cbs, status_name, cb, arg);
}

inline int waveform_register_state_cb(struct waveform_t* waveform,
                                      waveform_state_cb_t cb, void* arg)
{
   return waveform_register_cb(&waveform->state_cbs, NULL, (waveform_cmd_cb_t) cb, arg);
}

inline int waveform_register_command_cb(struct waveform_t* waveform,
                                        const char* command_name, waveform_cmd_cb_t cb,
                                        void* arg)
{
   return waveform_register_cb(&waveform->cmd_cbs, command_name, cb, arg);
}

#define REGISTER_DATA_CB(name)                                                                           \
   int waveform_register_##name##_data_cb(struct waveform_t* waveform, waveform_data_cb_t cb, void* arg) \
   {                                                                                                     \
      return waveform_register_cb(&waveform->name##_data_cbs, NULL, (waveform_cmd_cb_t) cb, arg);        \
   }

REGISTER_DATA_CB(rx)
REGISTER_DATA_CB(tx)
REGISTER_DATA_CB(tx_byte)
REGISTER_DATA_CB(rx_byte)
REGISTER_DATA_CB(unknown)

inline ssize_t waveform_send_data_packet(struct waveform_t* waveform,
                                         float* samples, size_t num_samples,
                                         enum waveform_packet_type type)
{
   vita_send_data_packet(&waveform->vita, samples, num_samples, type);
}

inline ssize_t waveform_send_raw_data_packet(struct waveform_t* waveform, uint8_t* data, size_t data_size, enum waveform_packet_type type)
{
   vita_send_raw_data_packet(&waveform->vita, data, data_size, type);
}

void waveform_set_context(struct waveform_t* wf, void* ctx)
{
   wf->ctx = ctx;
}

void* waveform_get_context(struct waveform_t* wf)
{
   return wf->ctx;
}