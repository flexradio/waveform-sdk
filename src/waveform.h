// SPDX-License-Identifier: LGPL-3.0-or-later
/// @file waveform.h
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

#ifndef WAVEFORM_WAVEFORM_H
#define WAVEFORM_WAVEFORM_H

// ****************************************
// Third Party Library Includes
// ****************************************
#include <sds.h>

// ****************************************
// Project Includes
// ****************************************
#include "vita.h"
#include "waveform_api.h"

// ****************************************
// Macros
// ****************************************
#define radio_waveforms_for_each(radio, pos)                \
   for (struct waveform_t * (pos) = wf_list; (pos) != NULL; \
        (pos) = (pos)->next)                                \
      if ((pos)->radio == (radio))

#define waveform_cb_for_each(wf, cb_list, pos)                          \
   for (struct waveform_cb_list * (pos) = (wf)->cb_list; (pos) != NULL; \
        (pos) = (pos)->next)

// ****************************************
// Structs, Enums, typedefs
// ****************************************
struct waveform_cb_list {
   sds name;
   union
   {
      waveform_cmd_cb_t cmd_cb;
      waveform_state_cb_t state_cb;
      waveform_data_cb_t data_cb;
   };
   void* arg;
   struct waveform_cb_list* next;
};

struct waveform_meter {
   sds name;
   float min;
   float max;
   enum waveform_units unit;

   uint16_t id;
   int value;

   struct waveform_meter* next;
};

struct waveform_t {
   char* name;
   char* short_name;
   char* underlying_mode;
   char* version;
   char active_slice;

   int rx_depth;
   int tx_depth;

   struct radio_t* radio;

   struct vita vita;

   struct waveform_cb_list* status_cbs;
   struct waveform_cb_list* state_cbs;
   struct waveform_cb_list* rx_data_cbs;
   struct waveform_cb_list* tx_data_cbs;
   struct waveform_cb_list* cmd_cbs;

   struct waveform_meter* meter_head;

   void* ctx;

   struct waveform_t* next;
};

// ****************************************
// Global Variables
// ****************************************
extern struct waveform_t* wf_list;

#endif//WAVEFORM_WAVEFORM_H
