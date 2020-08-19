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

// ****************************************
// Third Party Library Includes
// ****************************************
#include <event2/bufferevent.h>

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
struct waveform_t* waveform_create(struct radio_t* radio, char* name,
                                   char* short_name, char* underlying_mode,
                                   char* version)
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

void waveform_destroy(struct waveform_t* waveform)
{
   struct waveform_t* cur;
   for (cur = wf_list; cur->next == waveform; cur = cur->next)
      ;
   cur->next = waveform->next;

   free(waveform->name);
   free(waveform->short_name);
   free(waveform->underlying_mode);
   free(waveform);
}

int waveform_register_status_cb(struct waveform_t* waveform, char* status_name,
                                waveform_cmd_cb_t cb, void* arg)
{
   struct waveform_cb_list* new_cb = calloc(1, sizeof(*new_cb));
   if (!new_cb)
   {
      return -1;
   }

   new_cb->name = sdsnew(status_name);
   if (!new_cb->name)
   {
      free(new_cb);
      return -1;
   }

   new_cb->cmd_cb = cb;
   new_cb->arg = arg;

   if (!waveform->status_cbs)
   {
      waveform->status_cbs = new_cb;
   }
   else
   {
      struct waveform_cb_list* cur;
      for (cur = waveform->status_cbs; cur->next != NULL;
           cur = cur->next)
         ;
      cur->next = new_cb;
   }
}

inline long waveform_send_api_command_cb(struct waveform_t* waveform,
                                         waveform_response_cb_t cb, void* arg,
                                         char* command, ...)
{
   va_list ap;
   long ret;

   va_start(ap, command);
   ret = waveform_radio_send_api_command_cb_va(waveform, cb, arg, command,
                                               ap);
   va_end(ap);

   return ret;
}

int waveform_register_state_cb(struct waveform_t* waveform,
                               waveform_state_cb_t cb, void* arg)
{
   struct waveform_cb_list* new_cb = calloc(1, sizeof(*new_cb));
   if (!new_cb)
   {
      return -1;
   }

   new_cb->name = NULL;

   new_cb->state_cb = cb;
   new_cb->arg = arg;

   if (!waveform->state_cbs)
   {
      waveform->state_cbs = new_cb;
   }
   else
   {
      struct waveform_cb_list* cur;
      for (cur = waveform->state_cbs; cur->next != NULL;
           cur = cur->next)
         ;
      cur->next = new_cb;
   }
}

int waveform_register_rx_data_cb(struct waveform_t* waveform,
                                 waveform_data_cb_t cb, void* arg)
{
   struct waveform_cb_list* new_cb = calloc(1, sizeof(*new_cb));
   if (!new_cb)
   {
      return -1;
   }

   new_cb->name = NULL;

   new_cb->data_cb = cb;
   new_cb->arg = arg;

   if (!waveform->rx_data_cbs)
   {
      waveform->rx_data_cbs = new_cb;
   }
   else
   {
      struct waveform_cb_list* cur;
      for (cur = waveform->rx_data_cbs; cur->next != NULL;
           cur = cur->next)
         ;
      cur->next = new_cb;
   }
}

int waveform_register_tx_data_cb(struct waveform_t* waveform,
                                 waveform_data_cb_t cb, void* arg)
{
   struct waveform_cb_list* new_cb = calloc(1, sizeof(*new_cb));
   if (!new_cb)
   {
      return -1;
   }

   new_cb->name = NULL;

   new_cb->data_cb = cb;
   new_cb->arg = arg;

   if (!waveform->tx_data_cbs)
   {
      waveform->tx_data_cbs = new_cb;
   }
   else
   {
      struct waveform_cb_list* cur;
      for (cur = waveform->tx_data_cbs; cur->next != NULL;
           cur = cur->next)
         ;
      cur->next = new_cb;
   }
}

int waveform_register_command_cb(struct waveform_t* waveform,
                                 char* command_name, waveform_cmd_cb_t cb,
                                 void* arg)
{
   struct waveform_cb_list* new_cb = calloc(1, sizeof(*new_cb));
   if (!new_cb)
   {
      return -1;
   }

   new_cb->name = sdsnew(command_name);

   new_cb->cmd_cb = cb;
   new_cb->arg = arg;

   if (!waveform->cmd_cbs)
   {
      waveform->cmd_cbs = new_cb;
   }
   else
   {
      struct waveform_cb_list* cur;
      for (cur = waveform->cmd_cbs; cur->next != NULL;
           cur = cur->next)
         ;
      cur->next = new_cb;
   }
}

inline void waveform_send_data_packet(struct waveform_t* waveform,
                                      float* samples, size_t num_samples,
                                      enum waveform_packet_type type)
{
   vita_send_data_packet(&waveform->vita, samples, num_samples, type);
}

void waveform_set_context(struct waveform_t* wf, void* ctx)
{
   wf->ctx = ctx;
}

void* waveform_get_context(struct waveform_t* wf)
{
   return wf->ctx;
}