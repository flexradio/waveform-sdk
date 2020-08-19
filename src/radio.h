// SPDX-License-Identifier: LGPL-3.0-or-later
/// @file radio.h
/// @brief Implementation of radio communciations functionality
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

#ifndef WAVEFORM_SDK_RADIO_H
#define WAVEFORM_SDK_RADIO_H

// ****************************************
// Third Party Library Includes
// ****************************************
#include <pthread_workqueue.h>

// ****************************************
// Project Includes
// ****************************************
#include "waveform_api.h"

// ****************************************
// Structs, Enums, typedefs
// ****************************************
struct response_queue_entry {
   struct waveform_t* wf;
   unsigned int sequence;
   waveform_response_cb_t cb;
   void* ctx;
   struct response_queue_entry* next;
};

struct radio_t {
   struct sockaddr_in addr;
   pthread_t thread;
   struct event_base* base;
   struct bufferevent* bev;
   unsigned long handle;
   long sequence;
   pthread_workqueue_t cb_wq;
   struct response_queue_entry* rq_head;
};

// ****************************************
// Global Functions
// ****************************************
/// @brief Send a command to the radio
/// @details Sends a command to the radio.  This is the varargs version that takes a va_list so that we can build other
///          commands on top of it.  This is the "base" function for a few other functions presented to the user.
/// @param wf A reference to the waveform
/// @param cb Callback that we will call when the command response arrives.  Can be NULL to not execute a callback.
/// @param arg Argument to be passed to the callback when the command response arrives.
/// @param command The format string for the command to send to the radio.
/// @param ap A va_list to complete the format string specified in the command parameter.
long waveform_radio_send_api_command_cb_va(struct waveform_t* wf,
                                           waveform_response_cb_t cb, void* arg,
                                           char* command, va_list ap);

#endif//WAVEFORM_SDK_RADIO_H
