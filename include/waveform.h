// SPDX-Licence-Identifier: GPL-3.0-or-later
/// @file waveform.h
/// @brief Public definitions of waveform library functions
/// @authors Annaliese McDermond <anna@flex-radio.com>
/// 
/// @copyright Copyright (c) 2020 FlexRadio Systems
///
/// This program is free software: you can redistribute it and/or modify
/// it under the terms of the GNU General Public License as published by
/// the Free Software Foundation, version 3.
///
/// This program is distributed in the hope that it will be useful, but
/// WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
/// General Public License for more details.
///
/// You should have received a copy of the GNU General Public License
/// along with this program. If not, see <http://www.gnu.org/licenses/>.
///

#ifndef WAVEFORM_SDK_WAVEFORM_H
#define WAVEFORM_SDK_WAVEFORM_H

// Opaque structure to keep track of the waveform.
struct waveform_t;
struct waveform_meter_t;
struct waveform_meter_list_t;

enum waveform_units {
    DB,
    DBM,
    DBFS,
    VOLTS,
    AMPS,
    RPM,
    TEMP_F,
    TEMP_C,
    SWR,
    WATTS,
    PERCENT,
    NONE
};

//  Callback formats
typedef int (*waveform_state_cb_t)(waveform_t* waveform, void *arg);
typedef int (*waveform_cmd_cb_t)(waveform_t* waveform, char* command, void *arg);
typedef int (*waveform_data_cb_t)(waveform_t* waveform, void* data, size_t data_size, void* arg);

//  You are expected to be through with *message when you return.  We will free it.  Copy if needed.
typedef void (*waveform_response_cb_t)(waveform_t* waveform, int code, char* message);

/// @brief Create a waveform.
/// @details Creates a waveform for processing.  This will register the waveform with the SDK and set it up to be
/// handled in the event loop when executed.  This function can be called more than once if you would like to
/// set up multiple modes in the same waveform program.
/// @param name The full name of the waveform
/// @param short_name A short name for the waveform.  This should be limited to four characters.
/// @param underlying_mode The "underlying mode" of the waveform.  This effects the type of data that you will receive
///        If you would like real audio data, either the LSB or USB modes are appropriate.  There will also be a mode
///        named IQ that will give you largely unmolested I/Q samples.  This string must be the *short* name of the
///        underlying mode in the radio.
/// @return A pointer to an allocated structure representing the waveform.  This structure is opaque and you should not
///         attempt to modify it in any way.  You are responsible for freeing the structure using waveform_destroy()
///         when you are done with it.
struct waveform_t* waveform_create(char* name, char* short_name, char* underlying_mode);

/// @brief Destroy a waveform
/// @details Destroys a previously allocated waveform freeing all resources it consumes.
/// @param waveform A pointer to the waveform structure to be destroyed.
void waveform_destroy(struct waveform_t* waveform);

/// @brief Register an activation callback for a waveform.
/// @details When a slice on the radio switches to a mode handled by this waveform, this callback is called.
///          You should do any initial setup necessary to prepare your waveform to receive data.  The data handling
///          will not be started until this callback has completed.  Note that because of this you will be blocking
///          the main API event handling thread.  Do not perform long-lived tasks in this callback.
/// @param waveform Pointer to the waveform structure returned by waveform_create
/// @param cb A pointer to the callback function
/// @param arg A user-defined argument to be passed to the callback upon execution
/// @return 0 upon success, -1 on failure
int waveform_register_activate_cb(struct waveform_t* waveform, waveform_state_cb_t *cb, void *arg);

/// @brief Register an dectivation callback for a waveform.
/// @details When a slice on the radio switches away from a mode handled by this waveform, this callback is called.
///          You should do any final cleanup necessary to prepare your waveform to be dormant.  The data handling
///          will not be stopped until this callback has completed.  Note that because of this you will be blocking
///          the main API event handling thread.  Do not perform long-lived tasks in this callback.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param cb A pointer to the callback function
/// @param arg A user-defined argument to be passed to the callback upon execution.  Can be NULL.
/// @return 0 upon success, -1 on failure
int waveform_register_deactivate_cb(struct waveform_t* waveform, waveform_state_cb_t *cb, void *arg);

/// @brief Register a transmitter data callback for a waveform.
/// @details Registers a callback that is called when there is data from the incoming audio source to be transmitted.
///          You are expected to do any processing on the data and send appropriate packets back to the radio
///          to be transmitted.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param cb Pointer to the callback function
/// @param arg A user-defined argument to be passed to the callback on execution.  Can be NULL.
/// @return 0 upon success, -1 on failure
int waveform_register_tx_data_cb(struct waveform_t* waveform, waveform_data_cb_t *cb, void *arg);

/// @brief Register a receive data callback for a waveform.
/// @details Registers a callback that is called when there is data from the incoming RF data from the receiver.
///          You are expected to do any processing on the data and send appropriate packets back to the radio
///          to be sent out to the audio output devices.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param cb Pointer to the callback function
/// @param arg A user-defined argument to be passed to the callback on execution.  Can be NULL.
/// @return 0 upon success, -1 on failure
int waveform_register_rx_data_cb(struct waveform_t* waveform, waveform_data_cb_t *cb, void *arg);

/// @brief Register a prepare for transmit callback.
/// @details Registers a callback is called when the user has asserted PTT and the transmitter is preparing to
///          activate.  You must handle any reconfiguration for your waveform to be prepared to receive TX data
///          and output samples to the transmitter.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param cb Pointer to the callback function
/// @param arg A user-defined argument to be passed to the callback on execution.  Can be NULL.
/// @return 0 upon success, -1 on failure
int waveform_register_tx_prepare_cb(struct waveform_t* waveform, waveform_state_cb_t *cb, void *arg);

/// @brief Register a prepare for receive callback.
/// @details Registers a callback is called when the user has released PTT and the receiver is preparing to
///          activate.  You must handle any reconfiguration for your waveform to be prepared to receive RX data
///          and process samples from the receiver.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param cb Pointer to the callback function
/// @param arg A user-defined argument to be passed to the callback on execution.  Can be NULL.
/// @return 0 upon success, -1 on failure
int waveform_register_rx_prepare_cb(struct waveform_t* waveform, waveform_state_cb_t *cb, void *arg);

/// @brief Register a status callback.
/// @details Registers a callback is called when the radio status changes.  This function also handles creating the
///          event subscription in the API.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param status_name The name of the subsystem for which you would like to receive status.  For example, if you
///                    would like to receive slice status updates, set this parameter to "slice".
/// @param cb Pointer to the callback function
/// @param arg A user-defined argument to be passed to the callback on execution.  Can be NULL.
/// @return 0 upon success, -1 on failure
int waveform_register_status_cb(struct waveform_t* waveform, char* status_name, waveform_cmd_cb_t *cb, void *arg);

/// @brief Register a command callback.
/// @details Registers a callback is called when a waveform command is requested.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param command_name The name of the command for which you would like notification.  This is implementation
///                     specific to the waveform.  If a waveform command is received and there is no callback
///                     registered for that command, an error will be returned to the caller.
/// @param cb Pointer to the callback function
/// @param arg A user-defined argument to be passed to the callback on execution.  Can be NULL.
/// @return 0 upon success, -1 on failure
int waveform_register_command_cb(struct waveform_t* waveform, char *command_name, waveform_cmd_cb_t *cb, void *arg);

/// @brief Runs the event loop
/// @details This function should be called when you have registered all of your callbacks for the waveform and are
///          ready to begin operation.  No event callback registration should be performed with the event loop running
///          The loop will return upon completion to allow for any cleanup for waveform termination.  Waveform
///          authors must not trap SIGINT or SIGTERM.
void waveform_evt_loop(void);

/// @brief Sends a command to the radio
/// @details Does not wait for a response from the radio.  This is a shortcut for passing NULL to the cb parameter of
///          send_api_command_cb()
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param command A format string in printf(3) format.
/// @param ... Arguments for format specification
#define waveform_send_api_command(waveform, command, ...) \
   waveform_send_api_command_cb(waveform, NULL, NULL, command, ##__VA_ARGS__)

/// @brief Sends a command to the radio and invokes callback
/// @details This version of the command processing waits for a response from the radio and invokes your desired
///          callback.  Your callback should be thread-safe as it may not be executed on the same thread as you
///          invoked the command.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param cb Pointer to a command completion callback function.  If this is NULL, no callback will be performed.
/// @param arg A user-defined argument to be passed to the callback on execution.  Can be NULL.
/// @param command A format string in printf(3) format.
/// @param ... Arguments for format specification
void waveform_send_api_command_cb(struct waveform_t* waveform, waveform_response_cb_t* cb, void* arg, char* command, ...);

/// @brief Creates a list of meters to be sent to the radio
/// @details The meter list is intended to be reusable multiple times.  The implementation will clear the values
///          of each of the meters after sending them to the radio.  You may have multiple meter lists that send
///          a group of meters to the radio simultaneously. A meter can only be a part of one meter list at a time.
///          Please keep in mind that each list that gets sent causes a packet to be created and sent, which takes
///          a not insignificant amount of time; be efficient.  You are required to destory the meter list using
///          waveform_meter_list_destroy() when you are done using the list.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @returns An empty list of meters
waveform_meter_list_t waveform_meter_list_create(struct waveform_t* waveform);

/// @brief Adds a new meter to a meter list
/// @details Adds a new meter to a meter list and registers it with the radio.
/// @param meter_list An already created meter list
/// @param name the name of the meter
/// @param min The minimum value the meter can take on
/// @param max The maximum value the meter can take on
/// @param unit The unit of the meter
/// @returns 0 for success and -1 for failure
int waveform_register_meter(struct waveform_meter_list_t meter_list, char* name, float min, float max, enum waveform_units unit);

/// @brief Finds a meter in a list
/// @details Given a name, find the meter in a meter list.
/// @param meter_list The list of meters in which to find the meter
/// @param name The name of the meter
/// @returns an opaque structure representing the meter or NULL if the meter was not found.
waveform_meter_t* waveform_meter_find(struct waveform_meter_list_t meter_list, char *name);

/// @brief Sets the value of the meter
/// @details Sets the value of a meter in preparation for sending.  waveform_meter_list_send() will clear all the
///          values in the meter list upon sending, so you will have to reset the meter each time you want to perform
///          an update.
/// @param meter The opaque structure representing the meter obtained by using waveform_meter_find()
/// @param value The value of the meter
void waveform_meter_set_value(struct waveform_meter_t meter, float value);

/// @brief Sets the value of a meter given the name
/// @details This has the same functionality as a waveform_meter_find() followed by a waveform_meter_set_value()
/// @param meter_list The list of meters in which to find the meter
/// @param name The name of the meter to set
/// @param value The value of the meter
/// @returns -1 if the meter name cannot be found in the list, otherwise 0 for success.
int waveform_meter_set_value_by_name(struct waveform_meter_list_t meter_list, char* name, float value);

/// @brief Send a meter list to the radio
/// @details The meter values in the list will be sent to the radio.  Note that this will cause one or more UDP packets
///          to be send and is therefore not a "cheap" operation.  As many meters as possible should be combined into
///          a list and sent simultaneously.
/// @param meter_list The list of meters to send
/// @returns 0 for success or -1 for failure
int waveform_meter_list_send(struct waveform_meter_list_t meter_list);

/// @brief Frees a meter list
/// @details Use this function during shutdown when you are completely done with sending meters to destroy the meter
///          list.  Meter lists are reusable entities and you should be refilling and recycling them on each send
///          rather than allocating new ones.
/// @param meter_list The list of meters
void waveform_meter_list_destroy(struct waveform_meter_list_t meter_list);

#endif //WAVEFORM_SDK_WAVEFORM_H