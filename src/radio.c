// SPDX-License-Identifier: LGPL-3.0-or-later
/// @file radio.c
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

//  I have to come first.  The almighty template cannot be obeyed.
#define _GNU_SOURCE

// ****************************************
// System Includes
// ****************************************
#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ****************************************
// Third Party Library Includes
// ****************************************
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/thread.h>

#include <pthread_workqueue.h>

#include <sds.h>
#include <utlist.h>

// ****************************************
// Project Includes
// ****************************************
#include "meters.h"
#include "radio.h"
#include "utils.h"
#include "waveform.h"

// ****************************************
// Structs, Enums, typedefs
// ****************************************

// These are descriptors for passing parameters to the
// functions that run on the work queue.  We only get to
// pass one variable to the work queue function, so we have
// to marshall a bunch of variables in a struct.  These are
// created and passed to the callbacks for responses, status
// message callbacks, and command callbacks.

enum cmd_cb_type
{
   CMD_CB_QUEUED,
   CMD_CB_COMPLETE
};

struct resp_cb_wq_desc {
   unsigned int code;
   sds message;
   enum cmd_cb_type type;
   struct response_queue_entry* rq_entry;
};

struct status_cb_wq_desc {
   struct waveform_t* wf;
   sds message;
   struct waveform_cb_list* cb;
};

struct cmd_cb_wq_desc {
   int sequence;
   sds message;
   struct waveform_t* wf;
   struct waveform_cb_list* cb;
};

struct state_cb_wq_desc {
   struct waveform_t* wf;
   enum waveform_state state;
   struct waveform_cb_list* cb;
};

// ****************************************
// Static Functions
// ****************************************

/// @brief Add a callback to the queue of responses
/// @details When a command is issued for which we would like a response, we have
///          to track this command such that when the response comes in later that
///          we can call the callback requested by the user.  This function adds
///          the callback and the sequence number to a linked list of response
///          queue entries to be looked up when we recieve a command response.
/// @param waveform A reference to the waveform
/// @param cb A pointer to the function that we will call when the response to this
///           command arrives.
/// @param ctx A pointer to a user-defined context structure that will be provided to the
///            callback function when the command response arrives.
static void add_sequence_to_response_queue(struct waveform_t* waveform,
                                           waveform_response_cb_t cb, waveform_response_cb_t queued_cb, void* ctx)
{
   struct response_queue_entry* new_entry;

   new_entry = (struct response_queue_entry*) malloc(sizeof(*new_entry));
   new_entry->cb = cb;
   new_entry->queued_cb = queued_cb;
   new_entry->sequence = waveform->radio->sequence;
   new_entry->ctx = ctx;
   new_entry->wf = waveform;

   pthread_mutex_lock(&(waveform->radio->rq_lock));
   LL_APPEND(waveform->radio->rq_head, new_entry);
   pthread_mutex_unlock(&(waveform->radio->rq_lock));
}

/// @brief Work queue function to execute callback for a command response
/// @details When we receive a response to a command, we need to execute the user's callback code
///          in the work queue so that it will run in a different thread and not gum up the works of
///          the main command processing thread.  This is the work queue function to execute the
///          callbacks themselves.
/// @param arg The work queue descriptor for a command callback.
static void rq_call_cb(void* arg)
{
   struct resp_cb_wq_desc* desc = (struct resp_cb_wq_desc*) arg;
   waveform_response_cb_t cb;

   if (desc->type == CMD_CB_COMPLETE)
   {
      cb = desc->rq_entry->cb;
   }
   else if (desc->type == CMD_CB_QUEUED)
   {
      cb = desc->rq_entry->queued_cb;
   }
   else
   {
      //  We shouldn't get here.  This isn't a valid command callback type.
      return;
   }

   if (cb)
   {
      cb(desc->rq_entry->wf, desc->code, desc->message, desc->rq_entry->ctx);
   }

   //  Only free the response queue entry if we're complete, or if we have failed
   //  to queue the command.  Otherwise we need it in there to call the response
   //  callback when the command actually executes.
   if (desc->type == CMD_CB_COMPLETE || (desc->type == CMD_CB_QUEUED && desc->code != 0))
   {
      free(desc->rq_entry);
   }

   sdsfree(desc->message);
   free(desc);
}

/// @brief Runs any callbacks for a command and removes entries from the command queue
/// @details When a command for which we would like a response is executed, we must wait for the
///          response to be delivered back from the radio.  This function is called when we recieve
///          the response and need to have any callbacks called and the command queue entry removed
///          from the linked list.
/// @param radio A reference to the radio receiving the command
/// @param type Whether the callback is for a comamnd that is acknowledging queueing or sending a final response
/// @param sequence The sequence number that we assigned to the command when we executed it.  This will be
///                 returned back to us in the response to identify it.
/// @param code The response code to the comamand returned from the radio.  This is inevitably 0 for success or some
///             other arbitrary integer value for failure.
/// @param message A text response for the command.  This may be passed back for either success or failure.  For example
///                the meter creation command returns the meter index number in this field.
static void complete_response_entry(struct radio_t* radio,
                                    enum cmd_cb_type type,
                                    unsigned int sequence, unsigned int code,
                                    sds message)
{
   struct response_queue_entry* current_entry;
   pthread_workitem_handle_t handle;
   unsigned int gencountp;
   struct resp_cb_wq_desc* desc = calloc(1, sizeof(*desc));

   pthread_mutex_lock(&(radio->rq_lock));
   LL_SEARCH_SCALAR(radio->rq_head, current_entry, sequence, sequence);
   pthread_mutex_unlock(&(radio->rq_lock));
   if (!current_entry)
   {
      free(desc);
      return;
   }

   desc->code = code;
   desc->rq_entry = current_entry;
   desc->message = sdsdup(message);
   desc->type = type;

   //  Only remove the entry from the queue if we're complete, or if we have failed
   //  to queue the command.  Otherwise we need it in there to call the response
   //  callback when the command actually executes.
   if (type == CMD_CB_COMPLETE || (type == CMD_CB_QUEUED && code != 0))
   {
      pthread_mutex_lock(&(radio->rq_lock));
      LL_DELETE(radio->rq_head, current_entry);
      pthread_mutex_unlock(&(radio->rq_lock));
   }

   pthread_workqueue_additem_np(radio->cb_wq, rq_call_cb, desc, &handle,
                                &gencountp);
}

/// @brief Destroys the command response queue
/// @param waveform A reference to the waveform where the response queue lives.
static void destroy_response_queue(struct waveform_t* waveform)
{
   struct response_queue_entry *current_entry, *tmp_entry;

   pthread_mutex_lock(&(waveform->radio->rq_lock));
   LL_FOREACH_SAFE(waveform->radio->rq_head, current_entry, tmp_entry)
   {
      LL_DELETE(waveform->radio->rq_head, current_entry);
      free(current_entry);
   }
   pthread_mutex_unlock(&(waveform->radio->rq_lock));
}

/// @brief Work queue function to execute callback for a state change
/// @details When a state change is detected, user-defined callbacks are executed for that message.  These
///          callbacks are registered with waveform_register_state_cb.  These callbacks are executed in the normal
///          priority work queue so that we do not stall the main command processing thread.
/// @param arg The work queue descriptor for a state callback.
static void radio_call_state_cb(void* arg)
{
   struct state_cb_wq_desc* desc = (struct state_cb_wq_desc*) arg;

   desc->cb->state_cb(desc->wf, desc->state, desc->cb->arg);

   free(desc);
}

/// @brief Process changes in the interlock state
/// @details when the radio is about to enter transmit or recieve state, the interlock will change state and
///          we will be notified of that fact in a status message.  This function handles those state notifications
///          and passes that notifications to any waveforms that may be interested by way of a callback registered
///          by that waveform.
/// @param radio The radio receiving the state change
/// @param state A string from the radio representing the current state of the interlock.
static void interlock_state_change(struct radio_t* radio, sds state)
{
   enum waveform_state cb_state;

   if (strcmp(state, "PTT_REQUESTED") == 0)
   {
      cb_state = PTT_REQUESTED;
   }
   else if (strcmp(state, "UNKEY_REQUESTED") == 0)
   {
      cb_state = UNKEY_REQUESTED;
   }
   else
   {
      return;
   }

   radio_waveforms_for_each (radio, cur_wf)
   {
      waveform_cb_for_each (cur_wf, state_cbs, cur_cb)
      {
         struct state_cb_wq_desc* desc = calloc(1, sizeof(*desc));
         pthread_workitem_handle_t handle;
         unsigned int gencountp;

         desc->wf = cur_wf;
         desc->state = cb_state;
         desc->cb = cur_cb;

         pthread_workqueue_additem_np(radio->cb_wq, radio_call_state_cb, desc, &handle, &gencountp);
      }
   }
}

/// @brief Process mode changes from the radio
/// @details When the radio changes mode, we are notified of that fact by a status message on the API.
///          We need to detect that new mode and see if one of the waveforms we are managing handles that
///          mode.  We also check to see if the waveform is already processing a slice and lock that out
///          as the current API is not specified to handle multiple slices using a single waveform.
/// @param radio A reference to the radio receiving the status message
/// @param mode The new mode that the slice is moving to
/// @param slice The slice changing mode
static void mode_change(struct radio_t* radio, sds mode, char slice)
{
   waveform_log(WF_LOG_INFO, "Got a request for mode %s on slice %d\n", mode, slice);

   radio_waveforms_for_each (radio, cur_wf)
   {
      //  User has deselected this waveform's mode.
      if (cur_wf->active_slice == slice &&
          strcmp(cur_wf->short_name, mode) != 0)
      {
         waveform_cb_for_each (cur_wf, state_cbs, cur_cb)
         {
            struct state_cb_wq_desc* desc = calloc(1, sizeof(*desc));
            pthread_workitem_handle_t handle;
            unsigned int gencountp;

            desc->wf = cur_wf;
            desc->state = INACTIVE;
            desc->cb = cur_cb;

            pthread_workqueue_additem_np(radio->cb_wq, radio_call_state_cb, desc, &handle, &gencountp);
         }
         cur_wf->active_slice = -1;
         vita_destroy(cur_wf);
      }

      // User has selected this waveform's mode and we're not busy.
      if (cur_wf->active_slice == -1 &&
          strcmp(cur_wf->short_name, mode) == 0)
      {
         waveform_cb_for_each (cur_wf, state_cbs, cur_cb)
         {
            struct state_cb_wq_desc* desc = calloc(1, sizeof(*desc));
            pthread_workitem_handle_t handle;
            unsigned int gencountp;

            desc->wf = cur_wf;
            desc->state = ACTIVE;
            desc->cb = cur_cb;

            pthread_workqueue_additem_np(radio->cb_wq, radio_call_state_cb, desc, &handle, &gencountp);
         }
         cur_wf->active_slice = slice;
         vita_init(cur_wf);
      }
   }
}

/// @brief Work queue function to execute callback for a status message
/// @details When a status message is recieved, user-defined callbacks are executed for that message.  These
///          callbacks are registered with waveform_register_status_cb.  These callbacks are executed in the normal
///          priority work queue so that we do not stall the main command processing thread.
/// @param arg The work queue descriptor for a status callback.
static void radio_call_status_cb(void* arg)
{
   int argc;
   struct status_cb_wq_desc* desc = (struct status_cb_wq_desc*) arg;

   sds* argv = sdssplitargs(desc->message, &argc);
   if (argc < 1)
   {
      sdsfree(desc->message);
      sdsfreesplitres(argv, argc);
      free(desc);
      return;
   }

   (desc->cb->cmd_cb)(desc->wf, argc, argv, desc->cb->arg);

   sdsfreesplitres(argv, argc);
   sdsfree(desc->message);
   free(desc);
}

/// @brief Handle a status message received from the radio
/// @details When we receive a message from the radio prefixed with 'S', this is a status message.  This function
///          handles processing of that message including seeing if it's a status for which we do internal handling,
///          such as slice mode changes or interlock state changes.  We also execute any callbacks that the user may have
///          registered for that message.
/// @param radio A reference to the radio receiving the status message
/// @param message the unprocessed message from the radio API
static void process_status_message(struct radio_t* radio, sds message)
{
   int argc;

   sds* argv = sdssplitargs(message, &argc);
   if (argc < 1)
   {
      sdsfreesplitres(argv, argc);
      return;
   }

   if (strcmp(argv[0], "slice") == 0)
   {
      sds mode = find_kwarg(argc, argv, "mode");
      if (mode)
      {
         errno = 0;
         char* endptr;

         long slice = strtoul(argv[1], &endptr, 10);
         if ((errno == ERANGE && slice == ULONG_MAX) ||
             (errno != 0 && slice == 0))
         {
            waveform_log(WF_LOG_ERROR, "Error finding slice: %s\n",
                         strerror(errno));
         }
         else
         {
            mode_change(radio, mode, (char) slice);
         }
         sdsfree(mode);
      }
   }
   else if (strcmp(argv[0], "interlock") == 0)
   {
      sds state = find_kwarg(argc, argv, "state");
      if (state)
      {
         interlock_state_change(radio, state);
         sdsfree(state);
      }
   }

   radio_waveforms_for_each (radio, cur_wf)
   {
      waveform_cb_for_each (cur_wf, status_cbs, cur_cb)
      {
         struct status_cb_wq_desc* desc =
               calloc(1, sizeof(*desc));
         pthread_workitem_handle_t handle;
         unsigned int gencountp;

         desc->wf = cur_wf;
         desc->message = sdsdup(message);
         desc->cb = cur_cb;

         pthread_workqueue_additem_np(radio->cb_wq,
                                      radio_call_status_cb, desc,
                                      &handle, &gencountp);
      }
   }

   sdsfreesplitres(argv, argc);
}

/// @brief Work queue function to execute callback for a waveform command message
/// @details When a waveform comamnd is recieved, user-defined callbacks are executed for that message.  These
///          callbacks are registered with waveform_register_command_cb.  These callbacks are executed in the normal
///          priority work queue so that we do not stall the main command processing thread.  We also send and appropriate
///          response to the radio based on the return value from the user callback.
/// @param arg The work queue descriptor for a command callback.
static void radio_call_command_cb(void* arg)
{
   int argc;
   struct cmd_cb_wq_desc* desc = (struct cmd_cb_wq_desc*) arg;

   sds* argv = sdssplitargs(desc->message, &argc);
   if (argc < 1)
   {
      sdsfree(desc->message);
      sdsfreesplitres(argv, argc);
      free(desc);
      return;
   }

   int ret =
         (desc->cb->cmd_cb)(desc->wf, argc - 2, argv + 2, desc->cb->arg);
   if (ret)
   {
      waveform_send_api_command_cb(desc->wf, NULL, NULL,
                                   "waveform response %d|%08x",
                                   desc->sequence, ret + 0x50000000);
   }
   else
   {
      waveform_send_api_command_cb(desc->wf, NULL, NULL,
                                   "waveform response %d|0",
                                   desc->sequence);
   }

   sdsfreesplitres(argv, argc);
   sdsfree(desc->message);
   free(desc);
}

/// @brief Handle a waveform command received from the radio
/// @details When we receive a message from the radio prefixed with 'C', this is a waveform command.  This function
///          handles processing of that command by executing any user registered callbacks for that command.
/// @param radio A reference to the radio receiving the status message
/// @param sequence The sequence number for the command required to be executed.  We have to have this for when the
///                 callback sends a response.
/// @param message the unprocessed command string from the radio API
static void process_waveform_command(struct radio_t* radio, int sequence,
                                     sds message)
{
   int argc;

   sds* argv = sdssplitargs(message, &argc);
   if (argc < 3 || strcmp(argv[0], "slice") != 0)
   {
      sdsfreesplitres(argv, argc);
      return;
   }

   errno = 0;
   char* endptr;
   long slice = strtoul(argv[1], &endptr, 10);
   if ((errno == ERANGE && slice == ULONG_MAX) ||
       (errno != 0 && slice == 0))
   {
      waveform_log(WF_LOG_ERROR, "Error finding slice: %s\n", strerror(errno));

      sdsfreesplitres(argv, argc);
      return;
   }

   radio_waveforms_for_each (radio, cur_wf)
   {
      if (slice != cur_wf->active_slice)
      {
         continue;
      }

      waveform_cb_for_each (cur_wf, cmd_cbs, cur_cb)
      {
         if (strcmp(cur_cb->name, argv[2]) != 0)
         {
            continue;
         }

         struct cmd_cb_wq_desc* desc = calloc(1, sizeof(*desc));
         pthread_workitem_handle_t handle;
         unsigned int gencountp;

         desc->wf = cur_wf;
         desc->message = sdsdup(message);
         desc->cb = cur_cb;
         desc->sequence = sequence;

         pthread_workqueue_additem_np(radio->cb_wq,
                                      radio_call_command_cb,
                                      desc, &handle, &gencountp);
      }
   }

   sdsfreesplitres(argv, argc);
}

/// @brief Process a line from the radio api
/// @details When the waveform receives a line of text from the radio, we process this.  We acertain the type of the message
///          from the first character and then dispatch it to the appropriate handler function.  We also do basic parsing here
///          like trying to figure out sequence numbers and such.
/// @param radio A reference to the radio receiving the line
/// @param line A string containing the line received from the radio.
static void radio_process_line(struct radio_t* radio, sds line)
{
   char* endptr;
   int ret;
   unsigned long code;
   unsigned long handle;
   unsigned long sequence;
   unsigned int api_version[4];
   int count;

   assert(radio != NULL);
   assert(line != NULL);

   waveform_log(WF_LOG_TRACE, "Rx: %s\n", line);
   char command = *line;
   sdsrange(line, 1, -1);
   sds* tokens = sdssplitlen(line, sdslen(line), "|", 1, &count);

   switch (command)
   {
      case 'V':
         // TODO: Fix me so that I read into the api struct.
         errno = 0;
#pragma clang diagnostic push
#pragma ide diagnostic ignored "cert-err34-c"
         ret = sscanf(line, "%d.%d.%d.%d", &api_version[0],
                      &api_version[1], &api_version[2], &api_version[3]);
#pragma clang diagnostic pop
         if (ret != 4)
            waveform_log(WF_LOG_ERROR, "Error converting version string: %s\n",
                         line);

         waveform_log(WF_LOG_INFO, "Radio API Version: %d.%d(%d.%d)\n",
                      api_version[0], api_version[1], api_version[2],
                      api_version[3]);
         break;

      case 'H':
         errno = 0;
         radio->handle = strtoul(line, &endptr, 16);
         if ((errno == ERANGE && radio->handle == ULONG_MAX) ||
             (errno != 0 && radio->handle == 0))
         {
            waveform_log(WF_LOG_ERROR, "Error finding session handle: %s\n",
                         strerror(errno));
            break;
         }

         if (endptr == line)
         {
            waveform_log(WF_LOG_ERROR, "Cannot find session handle in: %s\n",
                         line);
            break;
         }

         break;

      case 'S':
         errno = 0;
         if (count != 2)
         {
            waveform_log(WF_LOG_ERROR, "Invalid status line: %s", line);
            break;
         }

         handle = strtoul(tokens[0], &endptr, 16);
         if ((errno == ERANGE && handle == ULONG_MAX) ||
             (errno != 0 && handle == 0))
         {
            waveform_log(WF_LOG_ERROR, "Error finding status handle: %s\n",
                         strerror(errno));
            break;
         }

         if (endptr == tokens[0])
         {
            break;
         }

         process_status_message(radio, tokens[1]);
         break;

      case 'M':
         break;

      case 'R':
      case 'Q':
         errno = 0;
         if (count != 3)
         {
            waveform_log(WF_LOG_ERROR, "Invalid response line: %s\n", line);
            break;
         }

         sequence = strtoul(tokens[0], &endptr, 10);
         if ((errno == ERANGE && sequence == ULONG_MAX) ||
             (errno != 0 && sequence == 0))
         {
            waveform_log(WF_LOG_ERROR, "Error finding response sequence: %s\n",
                         strerror(errno));
            break;
         }

         if (endptr == tokens[0])
         {
            waveform_log(WF_LOG_ERROR,
                         "Cannot find response sequence in: %s\n", line);
            break;
         }

         errno = 0;
         code = strtoul(tokens[1], &endptr, 16);
         if ((errno == ERANGE && code == ULONG_MAX) ||
             (errno != 0 && code == 0))
         {
            waveform_log(WF_LOG_ERROR, "Error finding response code: %s\n",
                         strerror(errno));
            break;
         }

         if (endptr == tokens[1])
         {
            waveform_log(WF_LOG_ERROR, "Cannot find response code in: %s\n",
                         line);
            break;
         }

         enum cmd_cb_type type = command == 'R' ? CMD_CB_COMPLETE : CMD_CB_QUEUED;
         complete_response_entry(radio, type, sequence, code, tokens[2]);

         break;

      case 'C':
         errno = 0;
         if (count != 2)
         {
            waveform_log(WF_LOG_ERROR, "Invalid command line: %s\n", line);
            break;
         }

         sequence = strtoul(tokens[0], &endptr, 10);
         if ((errno == ERANGE && sequence == ULONG_MAX) ||
             (errno != 0 && sequence == 0))
         {
            waveform_log(WF_LOG_ERROR, "Error finding command sequence: %s\n",
                         strerror(errno));
            break;
         }

         if (tokens[0] == endptr)
         {
            waveform_log(WF_LOG_ERROR, "Cannot find command sequence in: %s\n", line);
            break;
         }

         process_waveform_command(radio, sequence, tokens[1]);
         break;

      default:
         waveform_log(WF_LOG_DEBUG, "Unknown command: %s\n", line);
         break;
   }

   sdsfreesplitres(tokens, count);
}

static void radio_set_waveform_streams(struct waveform_t* waveform, unsigned int code, char* message, void* arg)
{
   sds tx_stream_in_id;
   sds rx_stream_in_id;
   sds tx_stream_out_id;
   sds rx_stream_out_id;
   int argc;

   if (code != 0)
   {
      waveform_log(WF_LOG_ERROR, "Couldn't register waveform: %s (%d)\n", message, code);
      return;
   }

   sds* argv = sdssplitargs(message, &argc);

   if (false == find_kwarg_as_int(argc, argv, "tx_stream_in_id", &waveform->vita.tx_stream_in_id))
   {
      waveform_log(WF_LOG_ERROR, "Cannot find Incoming TX stream ID\n");
   }
   else
   {
      waveform_log(WF_LOG_DEBUG, "Found Incoming TX stream ID: 0x%08x\n", waveform->vita.tx_stream_in_id);
   }

   if (false == find_kwarg_as_int(argc, argv, "rx_stream_in_id", &waveform->vita.rx_stream_in_id))
   {
      waveform_log(WF_LOG_ERROR, "Cannot find Incoming RX stream ID\n");
   }
   else
   {
      waveform_log(WF_LOG_DEBUG, "Found Incoming RX stream ID: 0x%08x\n", waveform->vita.rx_stream_in_id);
   }

   //  TODO: These two streams come to us via the waveform command, but we can't send to them
   //        successfully: tx_stream_out_id, rx_stream_out_id.
   if (false == find_kwarg_as_int(argc, argv, "tx_stream_out_id", &waveform->vita.tx_stream_out_id))
   {
      waveform_log(WF_LOG_ERROR, "Cannot find Outgoing TX stream ID\n");
   }
   else
   {
      waveform_log(WF_LOG_DEBUG, "Found Outgoing TX stream ID: 0x%08x\n", waveform->vita.tx_stream_out_id);
   }

   if (false == find_kwarg_as_int(argc, argv, "rx_stream_out_id", &waveform->vita.rx_stream_out_id))
   {
      waveform_log(WF_LOG_ERROR, "Cannot find Outgoing RX stream ID\n");
   }
   else
   {
      waveform_log(WF_LOG_DEBUG, "Found Outgoing RX stream ID: 0x%08x\n", waveform->vita.rx_stream_out_id);
   }

   if (false == find_kwarg_as_int(argc, argv, "byte_stream_in_id", &waveform->vita.byte_stream_in_id))
   {
      waveform_log(WF_LOG_ERROR, "Cannot find Incoming Byte stream ID\n");
   }
   else
   {
      waveform_log(WF_LOG_DEBUG, "Found Incoming Byte stream ID: 0x%08x\n", waveform->vita.byte_stream_in_id);
   }

   if (false == find_kwarg_as_int(argc, argv, "byte_stream_out_id", &waveform->vita.byte_stream_out_id))
   {
      waveform_log(WF_LOG_ERROR, "Cannot find Outgoing Byte stream ID\n");
   }
   else
   {
      waveform_log(WF_LOG_DEBUG, "Found Outgoing Byte stream ID: 0x%08x\n", waveform->vita.byte_stream_out_id);
   }

   sdsfreesplitres(argv, argc);
}

/// @brief Initialize radio after connection established
/// @details Once we connect the API socket to the radio we need to execute certain functions to prepare for running the waveform.
///          These include registering the waveforms and modes that we handle, properly registering any meters, setting filter widths, etc.
/// @param radio A reference to the radio that has connected
static void radio_init(struct radio_t* radio)
{
   int sub = 0;

   radio_waveforms_for_each (radio, cur_wf)
   {
      if (sub == 0)
      {
         waveform_send_api_command_cb(cur_wf, NULL, NULL,
                                      "sub slice all");
         waveform_send_api_command_cb(cur_wf, NULL, NULL,
                                      "sub radio all");
         waveform_send_api_command_cb(cur_wf, NULL, NULL,
                                      "sub client all");
         sub = 1;
      }

      waveform_send_api_command_cb(
            cur_wf, radio_set_waveform_streams, NULL,
            "waveform create name=%s mode=%s underlying_mode=%s version=%s",
            cur_wf->name, cur_wf->short_name,
            cur_wf->underlying_mode, cur_wf->version);
      waveform_send_api_command_cb(cur_wf, NULL, NULL,
                                   "waveform set %s tx=1",
                                   cur_wf->name);
      waveform_send_api_command_cb(
            cur_wf, NULL, NULL,
            "waveform set %s rx_filter depth=%d", cur_wf->name,
            cur_wf->rx_depth);
      waveform_send_api_command_cb(
            cur_wf, NULL, NULL,
            "waveform set %s tx_filter depth=%d", cur_wf->name,
            cur_wf->tx_depth);

      waveform_create_meters(cur_wf);
   }
}

/// @brief Libevent callback for connection/disconnection events from the radio
/// @details Libevent requires you to implement a callback that fires whenever the TCP connection has actually become connected, or when
///          other significant lifecycle events happen on the connection.  Our implementation handles initialization of the radio when
///          first connected, and should eventually handle disconnections more gracefully.  See libevent documentation for more details.
/// @param bev Buffer event reference from libevent
/// @param what What kind of event occurred
/// @param ctx A reference to the radio structure for which this TCP connection serves as the API connection.
static void radio_event_cb(struct bufferevent* bev,
                           short what, void* ctx)
{
   struct radio_t* radio = (struct radio_t*) ctx;

   if (what & BEV_EVENT_CONNECTED)
   {
      waveform_log(WF_LOG_INFO, "Connected to radio at %s\n",
                   inet_ntoa(radio->addr.sin_addr));
      radio_init(radio);
      return;
   }

   if (what & BEV_EVENT_TIMEOUT)
   {
      waveform_log(WF_LOG_SEVERE, "Connection to the radio at %s timed out\n", inet_ntoa(radio->addr.sin_addr));
      event_base_loopbreak(radio->base);
      return;
   }

   if (what & BEV_EVENT_EOF)
   {
      waveform_log(WF_LOG_SEVERE, "Radio has disconnected\n");
      event_base_loopbreak(radio->base);
      return;
   }

   if (what & BEV_EVENT_ERROR)
   {
      waveform_log(WF_LOG_SEVERE, "Radio TCP connection has encountered an error: %s\n",
                   evutil_socket_error_to_string(evutil_socket_geterror(bufferevent_getfd(bev))));
      event_base_loopbreak(radio->base);
      return;
   }

   waveform_log(WF_LOG_INFO, "Received unknown radio event\n");
}

/// @brief Callback called when the radio has some data
/// @details When libevent detects that there is some data available from the API socket, it calls this callback to let us know.
///          We then see if there is a line's worth of data there, and dispatch it to radio_process_line to be dealt with.  See
///          libevent documentation for more details.
/// @param bev A reference to the bufferevent for this event
/// @param ctx A reference to the radio for which this event is being triggered
static void radio_read_cb(struct bufferevent* bev, void* ctx)
{
   struct radio_t* radio = (struct radio_t*) ctx;
   char* line;
   size_t chars_read;

   struct evbuffer* input_buffer = bufferevent_get_input(bev);

   while ((line = evbuffer_readln(input_buffer, &chars_read,
                                  EVBUFFER_EOL_ANY)))
   {
      sds newline = sdsnewlen(line, chars_read);
      free(line);
      radio_process_line(radio, newline);
      sdsfree(newline);
   }
}

/// @brief Main radio event loop
/// @details An event loop for the radio that opens a socket to communicate with the radio, sets up
///          appropriate callbacks so that we can handle events and then executes event_base_dispatch
///          which will run in an infinite loop until there is no more connection there.  See the libevent
///          documentation for more details.
/// @param arg The radio for which to run the event loop
static void* radio_evt_loop(void* arg)
{
   struct radio_t* radio = (struct radio_t*) arg;

   evthread_use_pthreads();

   radio->base = event_base_new();

   radio->bev = bufferevent_socket_new(
         radio->base, -1, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
   if (!radio->bev)
   {
      waveform_log(WF_LOG_FATAL, "Could not create buffer event socket\n");
      goto eb_abort;
   }

   bufferevent_setcb(radio->bev, radio_read_cb, NULL, radio_event_cb,
                     radio);
#pragma clang diagnostic push
#pragma ide diagnostic ignored "hicpp-signed-bitwise"
   if (bufferevent_enable(
             radio->bev,
             EV_READ | EV_WRITE))
   {//  XXX Do we really need write here?
#pragma clang diagnostic pop
      waveform_log(WF_LOG_FATAL, "Could not enable buffer event\n");
      goto bev_abort;
   }

   if (bufferevent_socket_connect(radio->bev,
                                  (struct sockaddr*) &radio->addr,
                                  sizeof(struct sockaddr_in)))
   {
      waveform_log(WF_LOG_FATAL, "Could not connect to radio\n");
      goto bev_abort;
   }

   event_base_dispatch(radio->base);

   //  Clean up all the VITA loops hanging out there in waveforms.
   struct waveform_t* cur_wf;
   LL_FOREACH(wf_list, cur_wf)
   {
      if (cur_wf->radio == radio)
      {
         vita_destroy(cur_wf);
      }
   }

bev_abort:
   bufferevent_free(radio->bev);

eb_abort:
   event_base_free(radio->base);

   return NULL;
}

// ****************************************
// Global Functions
// ****************************************
int32_t waveform_radio_send_api_command_cb_va(struct waveform_t* wf,
                                              struct timespec* at,
                                              waveform_response_cb_t cb, waveform_response_cb_t queued_cb, void* arg,
                                              char* command, va_list ap)
{
   int cmdlen;
   char* message_format;
   va_list aq;

   struct evbuffer* output = bufferevent_get_output(wf->radio->bev);
   if (at)
   {
      cmdlen = asprintf(&message_format, "C%" PRIu32 "|@%ld.%ld|%s\n", wf->radio->sequence, at->tv_sec, at->tv_nsec * 1000,
                        command);
   }
   else
   {
      cmdlen = asprintf(&message_format, "C%" PRIu32 "|%s\n", wf->radio->sequence,
                        command);
   }

   if (cmdlen < 0)
   {
      return -1;
   }

   va_copy(aq, ap);
   sds debugstring = sdscatvprintf(sdsnew("Tx: "), message_format, aq);
   va_end(aq);
   waveform_log(WF_LOG_TRACE, "%s", debugstring);
   sdsfree(debugstring);

   evbuffer_add_vprintf(output, message_format, ap);

   free(message_format);

   if (cb)
   {
      add_sequence_to_response_queue(wf, cb, queued_cb, arg);
   }

   wf->radio->sequence = (wf->radio->sequence + 1) & ~(1 << 31);

   return (uint32_t) wf->radio->sequence;
}

// ****************************************
// Public API Functions
// ****************************************
struct radio_t* waveform_radio_create(struct sockaddr_in* addr)
{
   struct radio_t* radio = calloc(1, sizeof(*radio));
   if (!radio)
   {
      return NULL;
   }

   memcpy(&radio->addr, addr, sizeof(struct sockaddr_in));

   pthread_workqueue_init_np();

   pthread_mutex_init(&(radio->rq_lock), NULL);

   return radio;
}

void waveform_radio_destroy(struct radio_t* radio)
{
   pthread_mutex_destroy(&(radio->rq_lock));
   free(radio);
}

int waveform_radio_start(struct radio_t* radio)
{
   int ret;
   pthread_workqueue_attr_t wq_attr;

   ret = pthread_workqueue_attr_init_np(&wq_attr);
   if (ret)
   {
      waveform_log(WF_LOG_SEVERE, "Creating WQ attributes: %s\n", strerror(ret));
      return -1;
   }

   //    ret = pthread_workqueue_attr_setovercommit_np(&wq_attr, 1);
   //    if (ret) {
   //        waveform_log(WF_LOG_ERROR, "Couldn't set WQ to overcommit: %s\n", strerror(ret));
   //        //  Purposely not returning here because this is a non-fatal error.  Things will still work,
   //        //  but potentially suck.
   //    }

   ret = pthread_workqueue_attr_setqueuepriority_np(&wq_attr, WORKQ_DEFAULT_PRIOQUEUE);
   if (ret)
   {
      waveform_log(WF_LOG_ERROR, "Couldn't set WQ priority: %s\n",
                   strerror(ret));
      //  Purposely not returning here because this is a non-fatal error.  Things will still work,
      //  but potentially suck.
   }

   ret = pthread_workqueue_create_np(&radio->cb_wq, &wq_attr);
   if (ret)
   {
      waveform_log(WF_LOG_SEVERE, "Couldn't create callback WQ: %s\n",
                   strerror(ret));
      return -1;
   }

   ret = pthread_create(&radio->thread, NULL, radio_evt_loop, radio);
   if (ret)
   {
      free(radio);
      waveform_log(WF_LOG_SEVERE, "Creating thread: %s\n", strerror(ret));
      return -1;
   }

   return 0;
}

int waveform_radio_wait(struct radio_t* radio)
{
   return pthread_join(radio->thread, NULL);
}
