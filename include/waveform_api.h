// SPDX-License-Identifier: LGPL-3.0-or-later
/// @file waveform_api.h
/// @brief Public definitions of waveform library functions
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

#ifndef WAVEFORM_SDK_WAVEFORM_API_H
#define WAVEFORM_SDK_WAVEFORM_API_H

#include <netinet/in.h>
#include <sys/time.h>
#include <sys/types.h>

/// @struct waveform_t
/// @brief Opaque structure to keep track of the waveform.
struct waveform_t;
/// @struct waveform_meter_t
/// @brief Opaque structure for a meter object
struct waveform_meter_t;
/// @struct waveform_meter_list_t
/// @brief Opaque structure for a meter list object
struct waveform_meter_list_t;
struct radio_t;
struct waveform_args_t;
struct waveform_vita_packet;

/// @brief Enumeration for waveform meter units
enum waveform_units
{
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

/// @brief The possible states passed to a state callback waveform_state_cb_t
enum waveform_state
{
   ACTIVE,
   INACTIVE,
   PTT_REQUESTED,
   UNKEY_REQUESTED
};

/// @brief The type of packet to send to the radio, either to the speaker or the transmitter.
enum waveform_packet_type
{
   SPEAKER_DATA,
   TRANSMITTER_DATA,
};

/// @brief The levels for log messages.  Higher is more severe.
enum waveform_log_levels
{
   WF_LOG_TRACE = 100,
   WF_LOG_DEBUG = 200,
   WF_LOG_INFO = 300,
   WF_LOG_WARNING = 400,
   WF_LOG_ERROR = 500,
   WF_LOG_SEVERE = 600,
   WF_LOG_FATAL = 700
};

/// @brief A structure to hold a description of a meter
struct waveform_meter_entry {
   char* name;              ///< The name of the meter
   float min;               ///< The minimum value the meter can take
   float max;               ///< The maximum value the meter can take
   enum waveform_units unit;///< The units in which the meter is measured.
};

/// @brief Called when the waveform state changes
/// @details When the waveform changes state this callback is called to inform the waveform plugin of that fact.
/// @param waveform The waveform changing state.
/// @param state The state to which the waveform is transitioning.
/// @param arg A user-defined argument passed to waveform_register_activate_cb() or waveform_register_deactivate_cb()
typedef void (*waveform_state_cb_t)(struct waveform_t* waveform,
                                    enum waveform_state state, void* arg);

/// @brief Called when a command is requested
/// @details when a command is requested from the client, this callback is called.
/// @param waveform The waveform the command was given to
/// @param argc The number of arguments to the command
/// @param argv The arguments to the command.
/// @param arg A user-defined argument passed to waveform_register_command()
typedef int (*waveform_cmd_cb_t)(struct waveform_t* waveform, unsigned int argc,
                                 char* argv[], void* arg);

/// @brief Called when data is ready for the waveform
/// @details When new data arrives for the waveform, this callback is called.  It is recommended that you create a
///          buffer system to get the data off of this thread as quickly as possible and do your processing in a
///          separate thread.  Be efficient about this function as it will be called thousands of times a second when
///          the waveform is active.  Samples are returned as 32-bit floats in host byte order.  They are in pairs,
///          either Left first and Right second or I first and Q second depending on the underlying mode.
/// @param waveform The waveform receiving data
/// @param packet A pointer to the received data
/// @param packet_size the size of the received packet in bytes
/// @param arg A user-defined argument passed to the data callback creation functions.
typedef void (*waveform_data_cb_t)(struct waveform_t* waveform,
                                   struct waveform_vita_packet* packet,
                                   size_t packet_size, void* arg);

/// @brief Called when a response to a waveform command is received
/// @details This is called when a response is received to a command you issued to your waveform.
/// @param waveform The waveform the command was executed on
/// @param code The return code of the command.
/// @param arg A user-defined argument passed to the data callback creation functions.
/// @param message The text message from the command result.  Upon completion of this callback the storage for this
///                string will be freed.  If you need it past the context of this callback function you should copy
///                it to storage that you allocate.
typedef void (*waveform_response_cb_t)(struct waveform_t* waveform,
                                       unsigned int code, char* message,
                                       void* arg);

/// @brief Create a waveform.
/// @details Creates a waveform for processing.  This will register the waveform with the SDK and set it up to be
/// handled in the event loop when executed.  This function can be called more than once if you would like to
/// set up multiple modes in the same waveform program.
/// @param radio A pointer to the radio structure to which you wish to registe this waveform.  This is the output
///              of waveform_radio_create()
/// @param name The full name of the waveform
/// @param short_name A short name for the waveform.  This should be limited to four characters.
/// @param underlying_mode The "underlying mode" of the waveform.  This effects the type of data that you will receive
///        If you would like real audio data, either the LSB or USB modes are appropriate.  There will also be a mode
///        named IQ that will give you largely unmolested I/Q samples.  This string must be the *short* name of the
///        underlying mode in the radio.
/// @param version The version number of your waveform.
/// @return A pointer to an allocated structure representing the waveform.  This structure is opaque and you should not
///         attempt to modify it in any way.  You are responsible for freeing the structure using waveform_destroy()
///         when you are done with it.
struct waveform_t* waveform_create(struct radio_t* radio, const char* name,
                                   const char* short_name, const char* underlying_mode,
                                   const char* version);

/// @brief Destroy a waveform
/// @details Destroys a previously allocated waveform freeing all resources it consumes.
/// @param waveform A pointer to the waveform structure to be destroyed.
void waveform_destroy(struct waveform_t* waveform);

/// @brief Register a stauts change callback for a waveform.
/// @details When the slice to which the waveform is attached changes state, such as activating or deactivating the
///          waveform, or tx/rx state changes, the callback is called.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param cb The callback function
/// @param arg A user-defined argument to be passed to the callback upon execution. Can be NULL.
/// @return 0 upon succes, -1 on failure
int waveform_register_state_cb(struct waveform_t* waveform,
                               waveform_state_cb_t cb, void* arg);

/// @brief Register a transmitter data callback for a waveform.
/// @details Registers a callback that is called when there is data from the incoming audio source to be transmitted.
///          You are expected to do any processing on the data and send appropriate packets back to the radio
///          to be transmitted.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param cb The callback function
/// @param arg A user-defined argument to be passed to the callback on execution.  Can be NULL.
/// @return 0 upon success, -1 on failure
int waveform_register_tx_data_cb(struct waveform_t* waveform,
                                 waveform_data_cb_t cb, void* arg);

/// @brief Register a receive data callback for a waveform.
/// @details Registers a callback that is called when there is data from the incoming RF data from the receiver.
///          You are expected to do any processing on the data and send appropriate packets back to the radio
///          to be sent out to the audio output devices.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param cb The callback function
/// @param arg A user-defined argument to be passed to the callback on execution.  Can be NULL.
/// @return 0 upon success, -1 on failure
int waveform_register_rx_data_cb(struct waveform_t* waveform,
                                 waveform_data_cb_t cb, void* arg);

/// @brief Register a unknown data packet callback for a waveform.
/// @details Registers a callback that is called when there is an unknown VITA-49 packet from the radio.  This could
///          be anything from a 1PPS packet, to other various packets that we don't handle in other ways.  The framework
///          handles RX and TX data callbacks with the above waveform_register_rx_data_cb() and waveform_register_tx_data_cb()
///          but any other VITA-49 packets that are received will cause this callback to be triggered.  RX and TX callbacks
///          handle microphone, speaker, transmitter and receiver data.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param cb The callback function
/// @param arg A user-defined argument to be passed to the callback on execution.  Can be NULL.
/// @return 0 upon success, -1 on failure
int waveform_register_unknown_data_cb(struct waveform_t* waveform, waveform_data_cb_t cb, void* arg);

/// @brief Register a raw byte data packet callback for a waveform.
/// @details Registers a callback that is called when there is a raw byte VITA-49 packet from the radio.  This could
///          be data inbound from the radio's serial port or other data producing process.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param cb The callback function
/// @param arg A user-defined argument to be passed to the callback on execution.  Can be NULL.
/// @return 0 upon success, -1 on failure
int waveform_register_byte_data_cb(struct waveform_t* waveform, waveform_data_cb_t cb, void* arg);

/// @brief Register a status callback.
/// @details Registers a callback is called when the radio status changes.  This function also handles creating the
///          event subscription in the API.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param status_name The name of the subsystem for which you would like to receive status.  For example, if you
///                    would like to receive slice status updates, set this parameter to "slice".
/// @param cb The callback function
/// @param arg A user-defined argument to be passed to the callback on execution.  Can be NULL.
/// @return 0 upon success, -1 on failure
int waveform_register_status_cb(struct waveform_t* waveform, const char* status_name,
                                waveform_cmd_cb_t cb, void* arg);

/// @brief Register a command callback.
/// @details Registers a callback is called when a waveform command is requested.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param command_name The name of the command for which you would like notification.  This is implementation
///                     specific to the waveform.  If a waveform command is received and there is no callback
///                     registered for that command, an error will be returned to the caller.
/// @param cb The callback function
/// @param arg A user-defined argument to be passed to the callback on execution.  Can be NULL.
/// @return 0 upon success, -1 on failure
int waveform_register_command_cb(struct waveform_t* waveform,
                                 const char* command_name, waveform_cmd_cb_t cb,
                                 void* arg);

/// @brief Sends a command to the radio
/// @details Does not wait for a response from the radio.  This is a shortcut for passing NULL to the cb parameter of
///          send_api_command_cb()
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param command A format string in printf(3) format.
/// @param ... Arguments for format specification
/// @returns The sequence number on success or -1 on failure.
#define waveform_send_api_command(waveform, command, ...)      \
   waveform_send_api_command_cb(waveform, NULL, NULL, command, \
                                ##__VA_ARGS__)

/// @brief Sends a command to the radio and invokes callback
/// @details This version of the command processing waits for a response from the radio and invokes your desired
///          callback.  Your callback should be thread-safe as it may not be executed on the same thread as you
///          invoked the command.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param cb Pointer to a command completion callback function.  If this is NULL, no callback will be performed.
/// @param arg A user-defined argument to be passed to the callback on execution.  Can be NULL.
/// @param command A format string in printf(3) format.
/// @param ... Arguments for format specification
/// @returns The sequence number on success or -1 on failure.
int32_t waveform_send_api_command_cb(struct waveform_t* waveform,
                                     waveform_response_cb_t cb, void* arg,
                                     char* command, ...);

/// @brief Sends a command to the radio at a specified time
/// @details Does not wait for a response from the radio.  This is a shortcut for passing NULL to the callback parameters of
///          send_timed_api_command_cb()
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param at The time in the future in which to execute the commamnd
/// @param command A format string in printf(3) format.
/// @param ... Arguments for format specification
/// @returns The sequence number on success or -1 on failure.
#define waveform_send_timed_api_command(waveform, at, command, ...) \
   waveform_send_timed_api_command_cb(waveform, at, NULL, NULL, NULL, command##__VA_ARGS__)

/// @brief Sends a timed command to the radio and invokes callbacks
/// @details This version of the command processing waits for a response from the radio and invokes your desired
///          callback.  It also will optionally call a callback when the command is successfully queued.
///          Your callbacks should be thread-safe as it may not be executed on the same thread as you
///          invoked the command.
/// @param waveform Pointer to the waveform structure returned by waveform_create()
/// @param at The time in the future in which to execute the commamnd.
/// @param complete_cb Pointer to a command completion callback function.  If this is NULL, no callback will be performed.
/// @param queued_cb Pointer to a command queued callback function.  If this is NULL, no callback will be performed.
/// @param arg A user-defined argument to be passed to the callbacks on execution.  Can be NULL.
/// @param command A format string in printf(3) format.
/// @param ... Arguments for format specification
/// @returns The sequence number on success or -1 on failure.
int32_t waveform_send_timed_api_command_cb(struct waveform_t* waveform, struct timespec* at, waveform_response_cb_t complete_cb,
                                           waveform_response_cb_t queued_cb, void* arg, char* command, ...);

/// @brief Adds a new meter to a meter list
/// @details Adds a new meter to a meter list and registers it with the radio.
/// @param waveform The waveform containing the meter
/// @param name the name of the meter
/// @param min The minimum value the meter can take on
/// @param max The maximum value the meter can take on
/// @param unit The unit of the meter
/// @returns 0 for success and -1 for failure
void waveform_register_meter(struct waveform_t* waveform, const char* name,
                             float min, float max, enum waveform_units unit);

/// @brief Sets the value of a meter given the name
/// @details This has the same functionality as a waveform_meter_find() followed by a waveform_meter_set_value()
/// @param waveform The waveform containing the meter
/// @param name The name of the meter to set
/// @param value The value of the meter
/// @returns -1 if the meter name cannot be found in the list, otherwise 0 for success.
int waveform_meter_set_float_value(struct waveform_t* waveform, char* name,
                                   float value);

/// @brief Sets the value of a meter given the name
/// @details Sets the meter value given a short.  This is a *raw* function and will set whatever you send it
///          without doing any sanity checking.  For advanced usage only.  Not recommended for normal use.
/// @param waveform The waveform containing the meter
/// @param name The name of the meter to set
/// @param value The value of the meter
/// @returns -1 if the meter name cannot be found in the list, otherwise 0 for success.
int waveform_meter_set_int_value(struct waveform_t* waveform, char* name,
                                 short value);

/// @brief Send a meter list to the radio
/// @details The meter values in the list will be sent to the radio.  Note that this will cause one or more UDP packets
///          to be send and is therefore not a "cheap" operation.  As many meters as possible should be combined into
///          a list and sent simultaneously.
/// @param waveform The waveform containing the meter
/// @returns 0 on success or a negative value on an error.  Return values are negative values of errno.h.  Will return
///          -E2BIG on a short write to the network and -EFBIG if you attempt to send too many meters in a single
///          packet.
ssize_t waveform_meters_send(struct waveform_t* waveform);

/// @brief Creates a radio definition
/// @details Creates a radio structure.  This does not connect to the radio.  A radio should be created and
///          have all of the configuration done on it before calling waveform_radio_start() to connect and begin
///          operation of the waveform.
/// @param addr The address of the radio you intend to connect to.  There will eventually be a discovery function to
///             be able to find radios out there.
/// @returns An opaque structure representing the radio.
struct radio_t* waveform_radio_create(struct sockaddr_in* addr);

/// @brief Destroys a radio
/// @details Destroys a radio created previously by waveform_radio_create() and frees all associated memory.
/// @param radio The radio to destroy
void waveform_radio_destroy(struct radio_t* radio);

/// @brief Waits for the radio processing to complete
/// @details After your main thread has finished launching the radio with waveform_radio_start(), you will need to
///          wait for completion of your event loop and stop execution of the program.  You accomplish this by calling
///          waveform_radio_wait() after starting the radio.
/// @param radio The radio on which to wait.
/// @returns 0 on success or -1 for failure.
int waveform_radio_wait(struct radio_t* radio);

/// @brief Start the radio
/// @details Connects to the radio and starts the event loop to begin processing commands.  All callbacks should be
///          set up and registered by the time you call this function.  Registering a callback while the radio is
///          running will have undefined behavior.  This command will immediately return and you must use
///          waveform_radio_wait() to wait for its completion.
/// @param radio The radio on which to wait.
/// @returns 0 on success or -1 for failure.
int waveform_radio_start(struct radio_t* radio);

/// @brief Sends a data packet to the radio
/// @details After doing any processing necessary in the waveform, you must send back an output packet to the radio
///          representing either audio data to provide to the speaker, or transmit data to supply to the transmitter.
///          This function allows you to queue that data for transmission to the radio.
/// @param waveform The waveform sending the data
/// @param samples An array containing the samples to send.  This must be num_samples * sizeof(uint32_t) long.
/// @param num_samples Samples for the radio.  This is either baseband data in L/R format, or complex data in I/Q format
///                depending on the underlying mode.  These samples should be in 32-bit float format in host byte order
///                as the library will do any necessary byte swapping for transmission.
/// @param type The type of packet to send.  This is either SPEAKER_DATA for playing on the audio output of the radio
///             or TRANSMITTER_DATA for sending to the RF transmitter.
/// @returns 0 on success or a negative value on an error.  Return values are negative values of errno.h.  Will return
///          -E2BIG on a short write to the network and -EFBIG if you attempt to send too many samples in a single
///          packet.
ssize_t waveform_send_data_packet(struct waveform_t* waveform, float* samples,
                                  size_t num_samples,
                                  enum waveform_packet_type type);

/// @brief Sends a raw byte data packet to the radio
/// @details
/// @param waveform The waveform sending the data
/// @param data A reference to an array of bytes to send
/// @param data_size The number of bytes in the samples array
/// @returns 0 on success or a negative value on an error.  Return values are negative values of errno.h and will return
///          -E2BIG on a short write to the network.
ssize_t waveform_send_byte_data_packet(struct waveform_t* waveform, uint8_t* data, size_t data_size);

/// @brief Gets the length of a received packet
/// @details Returns the length of the data in a packet received from the radio.
/// @param packet A packet returned from the radio in the waveform_data_cb_t callback.
/// @returns Length of the data from the radio in 32-bit words.
uint16_t get_packet_len(struct waveform_vita_packet* packet);

/// @brief Get the packet data
/// @details Returns an array of floating point values representing either L/R or I/Q pairs depending on the underlying
///          mode.  The length of the array returned can be ascertained by calling get_packet_len().  The data in the
///          returned array will be freed when the callback returns to the library.
/// @param packet A packet returned from the radio in the waveform_data_cb_t callback.
/// @returns 32-bit floating point values from the radio represening data from the microphone or receiver in host byte
///          order.
float* get_packet_data(struct waveform_vita_packet* packet);

/// @brief Gets the integer timestamp from a received packet.
/// @details Returns the integral timestamp from the VITA-49 packet from the radio.
/// @param packet A packet returned from the radio in the waveform_data_cb_t callback.
/// @returns An integer representing the integral timestamp of the packet in host byte order.
uint32_t get_packet_ts_int(struct waveform_vita_packet* packet);

/// @brief Gets the fractional timestamp from a received packet.
/// @details Returns the fractional timestamp from the VITA-49 packet from the radio.
/// @param packet A packet returned from the radio in the waveform_data_cb_t callback.
/// @returns An integer representing the fractional timestamp of the packet in host byte order.
uint64_t get_packet_ts_frac(struct waveform_vita_packet* packet);

/// @brief Gets the timestamp from a received pakcet.
/// @details Returns the fractional timestamp from the VITA-49 packet from the radio in a user-provided structure.
/// @param packet A packet returned from the radio in the waveform_data_cb_t callback.
/// @param ts A user-provided structure in which to store the timestamp.  The user must have allocated memory for this
///           structure and is responsible for freeing it.
void get_packet_ts(struct waveform_vita_packet* packet, struct timespec* ts);

/// @brief Gets the stream ID from a received packet.
/// @details Returns the stream ID field from the VITA-49 packet from the radio.
/// @param packet A packet returned from the radio in the waveform_data_cb_t callback.
/// @returns an integer representing the stream ID of the packet in host byte order.
uint32_t get_stream_id(struct waveform_vita_packet* packet);

/// @brief Gets the class ID from a received packet.
/// @details Returns the class ID field from the VITA-49 packet from the radio.
/// @param packet A packet returned from the radio in the waveform_data_cb_t callback.
/// @returns an integer representing the class ID of the packet in host byte order.
uint64_t get_class_id(struct waveform_vita_packet* packet);

/// @brief Gets the current packet count from a received packet.
/// @details Gets the current packet count field from the VITA-49 packet.  This value is a 4-bit
///          counter that is incremented for every subsequent packet transmitted by a VITA
///          speaker.  The specification states that this is a 4-bit value, so the value returned
///          from this function will be increasing between 0x00 and 0x0F, rolling over to 0x00 on an
///          overflow.
/// @param packet A packet returned from the radio in the waveform_data_cb_t callback.
/// @returns an integer representing the number of packets received.
uint8_t get_packet_count(struct waveform_vita_packet* packet);

/// @brief Sets a structure for waveform context
/// @details State is sometimes necessary for a waveform to preserve values.  This function allows you to register
///          a pointer to a context structure that will be available during all waveform callbacks.  This call is
///          *NOT* locked by the API so it is incumbent on the user to ensure thread safety of any of the contents.
/// @param wf The waveform to set context
/// @param ctx A pointer to a context structure
void waveform_set_context(struct waveform_t* wf, void* ctx);

/// @brief Retrieves the waveform context
/// @details This function retrieves the context pointer set by waveform_set_context()
/// @param wf The waveform to set context
/// @returns The context structure
void* waveform_get_context(struct waveform_t* wf);

/// @brief Registers a set of meters
/// @details Registers an array of meters.  This is essentially a loop around waveform_register_meter as a convenience shortcut.
/// @param wf The waveform on which to register the meter.
/// @param list The array of meters to register.
/// @param num_meters The number of meters in the list
void waveform_register_meter_list(struct waveform_t* wf,
                                  const struct waveform_meter_entry list[],
                                  int num_meters);

/// @brief Finds a radio on the network
/// @details Listens to the discovery broadcasts being performed by radios and returns the first one it hears.  Returns NULL
///          if a radio can't be found.
/// @param timeout A timeout value after which the discovery will return unsuccessfully.
/// @returns A reference to the address of the radio.  You are responsible for freeing this memory when done.
struct sockaddr_in* waveform_discover_radio(const struct timeval* timeout);

/// @brief Sets the log verbosity of the library
/// @details Sets the logging verbosity of the library.  Any log messages with a level higher than this setting will be logged
///          to stdout.  See the above enum for relative levels of the logs.
/// @param level The level of the logging desired
void waveform_set_log_level(enum waveform_log_levels level);

/// @brief Get the data portion of a raw byte data packet
/// @details For a raw data packet returned from the waveform_register_rx_bytes_data_cb or waveform_register_tx_bytes_data_cb
///          registered callbacks, gets the raw data from the radio.  The length of this data can be obtained with the
///          get_packet_byte_data_length() call.
/// @param packet A packet returned from the radio in the waveform_data_cb_t callback.
/// @returns An array of bytes received in the radio packet
uint8_t* get_packet_byte_data(struct waveform_vita_packet* packet);

/// @brief Get the length of the raw byte data of a packet
/// @details For a raw data packet returned from the waveform_register_rx_bytes_data_cb or waveform_register_tx_bytes_data_cb
///          registered callbacks, gets the length of the raw data from the radio.  The data can be obtained with the
///          get_packet_byte_data() call.
/// @param A packet returned from the radio in the waveform_data_cb_t callback.
/// @returns An unsigned integer representing the number of bytes in the array returned by get_packet_byte_data()
uint32_t get_packet_byte_data_length(struct waveform_vita_packet* packet);

#endif//WAVEFORM_SDK_WAVEFORM_H