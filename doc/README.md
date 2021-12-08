# FlexRadio Waveform Application Programming Interface

**NOTICE: This documentation and the API within are in a preliminary state. The API is subject to change at any time until finalized.** 

## About Waveforms
Waveforms are pluggable modules used in FLEX radios to extend functionality by providing additional modulation and demodulation modes. These modes can implement data and voice based modes for use by the radio's user.

A waveform is implemented using two distinct but cooperating interfaces: the control or API interface, and the data or VITA-49 interface. The control interface uses TCP/IP to present a text based interface for the waveform to perform functions such as commanding the radio to tune frequencies, key the transmitter, change filter parameters, and even register the waveform itself with the radio. Documentation of the control interface is available on the [FlexRadio Wiki](http://wiki.flexradio.com/index.php?title=SmartSDR_TCP/IP_API) which details the structure and syntax of commands issued to the radio over the control socket.

The data interface exchanges UDP/IP data packets representing the signals from the receiver, to the transmitter, from the microphone and to the speaker. These data packets can contain either raw in-phase and qudrature, or (de)modulated right and left stero channel samples. Using a combination of these interfaces allows a waveform to implement alternative modes in the radio. These packets are formatted in [VITA-49](https://www.vita.com/resources/Learn/VITA49%20Brief%20for%20HPEC%209Sept2014%20Final.pptx) standard format.  

The data interface also provides the ability for the waveform to pass back "meter" packets to the radio to inform it of statistics specific to the waveform. This can include parameters such as BER, packet loss, frequency offset, timing and other parameters specific to the waveform's implementation. These can optionally be displayed by clients connecting to the radio.

All communications between the radio and the waveform occur over a network API, even when the waveform runs on a CPU internal to the radio.

## The Waveform API
The Waveform API makes the job of implementing a waveform for a FLEX radio easier and more straight forward. The API handles standard functionality for all waveforms such as:

* Discovering the radio
* Connecting to the radio
* Detecting when the waveform is activated or deactivated
* Processing and parsing radio status
* Starting and stopping data packet processing
* Reformatting and parsing data packets to host-friendly formats
* Issuing commands to the radio and relaying back responses

The API is implemented in a C language library that you link with your executable and call functions to utilize its services. The API is fully documented in [doxygen format](html/waveform__api_8h.html) for your convienience. While this document strives to be as up to date as possible, because of the way it is maintained, the doxygen format documentation is expected to be more accurate. When there are conflicts, it, not this document, should control.

All public functions of the API begin with the string `waveform_`.  Please reserve this informal namespace for future API usage.

Any C structure postfixed with `_t` is expected to be opaque. You should not access any members of that structure or count on any particular implementation of it. The structure member and layout may change without notice.

The Waveform API is based around an event driven callback model where when events happen on the radio such as status changes, waveform lifecycle changes or even data packets arriving, the API will execute user-defined callbacks to allow the waveform to react appropriately. Callbacks are registered for desired events on the waveform and are called when the corresponding events fire. When callbacks are registered, the API user has the option of sending it a private context pointer for that function. There is also a global context pointer implemented using the [`waveform_set_context`](html/waveform__api_8h.html#a417c2357020deba99e97a860c9a021a1) and [`waveform_get_context`](html/waveform__api_8h.html#a3c46e68997f0d7fafaa2011b12fea349) functions.

#### Notes on Threading
The Waveform API is a multi-threaded library and the astute waveform author will realize that this presents its own set of problems with thread synchronization and preventing high priority threads from stalling. To solve many of these problems, the Waveform API uses the [pthread_workqueues](https://github.com/mheily/libpwq) library.  Each callback is placed into one of two workqueues depending on its priority. Callbacks registered with the `waveform_register_tx_data_cb` and `waveform_register_rx_data_cb` are placed in a high priority work queue that runs under the Linux FIFO scheduler. All other callbacks run at regular priority in their work queue. Note that a single work queue does not imply a single thread. Two tasks in the same work queue could execute on different threads depending on system load and conditions at the time of task creation. The tasks will execute sequentially, but do not expect them to run on the same thread. Utilize thread synchronization techniques to ensure consistent access.

The API itself utilzies two threads, one to handle the command port, and one to handle the data port. The data handling thread is only activated when the waveform is active. The command thread is active at all times. Both of these threads run event loops to handle data incoming on their respecive network sockets.

### Basic API Structure
There are a few steps required to instantiate and use the waveform.

1. Optionally discover a radio on the network. If you already know the IP address of the radio (if, for example, it is passed to you on a command line), you may skip this step.
2. Instantiate an instance of a radio using the [`waveform_radio_create`](html/waveform__api_8h.html#a28984b2443be194b7236db2dd5ceb1f7) function.
3. Register your new mode(s) to the radio by using the [`waveform_create`](html/waveform__api_8h.html#a72e601dbcd68cd896c262b57e9f86018) function.
4. Register your desired callbacks to respond to when events happen on the radio.  These will include data packets being presented for processing, status events occuring on the radio, commands being issued for the waveform itself, or waveform lifecycle state changes.
5. Start the main waveform event loop using the [`waveform_radio_start`](html/waveform__api_8h.html#a35e070d291054bfac3961ca172d8e77a) function.
6. Wait for the event loop to stop because the waveform has been forcibly disconnected by using the [`waveform_radio_wait`](html/waveform__api_8h.html#adfcb97da8ed61e04229cb2e6fb87be7c) function.
7. Clean up your resources and cease execution.

Please note that it is currently _not_ supported to register new callbacks to the API after `waveform_radio_start` has been called. While this may indeed work, it is not supported and to do so is undefined and may exhibit undesired behavior. You have been warned.

You may register more than one callback to the same event. In this case all callbacks will eventually be run, but the order in which they are run is undefined. Do not count on a particular ordering of callback execution.

A single waveform may handle more than one mode. This is useful, for example, when a waveform may have the same basic structure but rely on two different underlying modes. Be aware that it is only supported for a waveform to handle a single stream at a time. This means that although you may register two modes to a single waveform, only one may be active at a time.

### Underlying Modes
Every waveform must have an "underlying mode" attached to it as a base mode. This can be any of the "built-in" modes of the radio. This will include USB, LSB, DIGU, DIGL, CW, AM, etc. Although you can use any of these, DIGU and DIGL are probably the most appropriate. These modes turn off most signal processing steps such as the transmit speech processor.

A special "underlying mode" has been implemented for waveform use. This is the `RAW` mode. `RAW` provides a waveform with I/Q samples with the least amount of processing possible. Data is mixed down from the ADC, filtered into the desired slice bandwidth and passed directly to the waveform. This will provide the least amount of latency, but will require the waveform author to implement a great deal of the signal processing chain themselves. This will include things such as AGC, final filtering and demodulation. Latency through the radio to the waveform is expected to be on the order of 10ms, but is not currently characterized or guarnateed.

### Waveform Lifecycle
A waveform's state callback is invoked for one of four events:

1. The waveform has become active (usually by the user selecting a mode the waveform handles on the user interface device).
2. The waveform has been deactivated (usually by the user selecting another mode on the user interface device that the waveform does not handle).
3. The radio intends to begin transmitting (usually by a user pressing the "PTT" or "MOX" buttons on the radio).
4. The radio intends to stop transmitting (usually by the user relasing the "PTT" or toggling the "MOX" button on the radio).

#### The Waveform Activates or Deactivates
When the waveform recieves this event, it needs to make all preparations to become active. The waveform API will begin recieving data packets from the radio and routing them to the data callbacks when it queues the callback for this event. Any preparation needed to process these packets must be done in the waveform activation callback.

Conversely when the waveform deactivates, all resources used by the waveform for processing data should be released and processing stopped. The user may not activate the waveform for a long time (days, weeks, months) so resources should not be kept around just for optimization purposes of a single waveform.

#### The Waveform Intends to Begin or Stop Transmitting
This callback is called when the user has requested the radio start or stop transmitting for some reason. This could be because the user has pressed a UI interface control, or possibly because the waveform itself has executed a transmit command over the command interface. Either way, the waveform must finish any processing for and cease the corresponding recieve or transmit stream before starting the converse stream. For example, if the `PTT_REQUESTED` event is received, the waveform must cease processing the receive stream and sending data to the speaker. The radio will not begin transmitting until it has ceased receiving packets for the speaker stream.

### Data Handling
The main job of the waveform is to handle and process data, of course. When the waveform becomes active, the API library arranges to start an event loop to handle the data packets and pass the resulting data to the user-defined callbacks. A single callback is invoked for every VITA-49 packet recieved by the API. `waveform_register_rx_cb` is used to register a callback to handle data coming from the radio's receiver. Similarly, `waveform_register_tx_cb` is used to register a callback to handle data coming from the radio's microphone to be transmitted.

There are utility functions to parse the opaque VITA-49 packet structure passed to these callback functions. Do not be
tempted to directly access members of the structure as their names, types, and layouts may change due to needs of the
API implementation.

* [`get_packet_data`](html/waveform__api_8h.html#a874c71a5961a9cb5f4730f839da59035) will return an array of floating
  point numbers representing the data from the radio. They will be in interleaved format with either I first followed by
  Q in the case of a `RAW` underlying mode, or Left followed by Right in the case of any other underlying mode.
* [`get_packet_len`](html/waveform__api_8h.html#a91dd9bb3ff6c0e998947026f57012496) will return a 16-bit unsigned integer
  representing the length of the array returned by `get_packet_data`. Note that there are *two* 32-bit words per sample,
  meaning that the array has twice as many values (I/Q or L/R) as there are samples.
* [`get_packet_ts_int`](html/waveform__api_8h.html#ad9061cb8be95e5fc2616b3a5f689b398) will return the value of the
  integral part of the packet's timestamp. The VITA-49 spec specifies that this will be a 32-bit unsigned integer
  specifying seconds since the UNIX epoch; 00:00:00Z, January 1, 1970.
* [`get_packet_ts_frac`](html/waveform__api_8h.html#a8ad0a47c35102bc63f49f01de9f28732) will return the value of the
  fractional portioin of the packet's timestamp. The VITA-49 spec specifies multiple formats for this. The only one
  currently utilized by the radio is a count of the samples since the top of the second. This function is likely to
  change in the future as timestamp formats are refined.

When the waveform has processed the incoming data from whatever source it can send data back to the radio in response.
This can either take the form of outgoing packets to the speaker for use in recieve chains, or outgoing packets to the
transmitter for the creation of RF.
The [`waveform_send_data_packet`](html/waveform__api_8h.html#a52e6b7f438ef37e8bd74f5e83be3f40d) handles both of these
cases. When invoked with the `TRANSMITTER_DATA` type, it will send samples to the transmitter. When invoked with
the `SPEAKER_DATA` type, the samples will be played as audio through the speaker.

### Byte Stream Data Handling

*Note that byte streams are not currently useful on the FLEX-6000 series radios*

A waveform may have occasion to deal with byte-oriented data. This could be because a digital waveform may want to
source data from the serial port, or a waveform may wish to send digital data to the RapidM module for further
modulation and processing. To accomplish this, the radio will create an input and output "byte stream" when the waveform
is created. These streams can be connected to a variety of sources and sinks within the radio. You can connect the
streams together with the `byte_stream connect` command. The syntax is
`byte_stream connect input=MyWaveform output=MyWaveform`. The name of the input and output streams to the waveform are
the same as the name you have given the waveform in your `waveform_create` call. For testing and validation the above
syntax will connect the input to the output essentially creating a loopback interface.

Byte data can be sent to the radio with the
[`waveform_send_byte_data_packet`](html/waveform__api_8h.html#a8ad0a47c35102bc63f49f01de9f28732) call. Conversely, raw
data packets can be recieved by registering a callback with
[`waveform_register_byte_data_cb`](html/waveform__api_8h.html#a8ad0a47c35102bc63f49f01de9f28732). The functions
[`get_packet_byte_data_length`](html/waveform__api_8h.html#a8ad0a47c35102bc63f49f01de9f28732) and
[`get_packet_byte_data`](html/waveform__api_8h.html#a8ad0a47c35102bc63f49f01de9f28732) allow access to the data. Please
note that no data is buffered, and your waveform will receive data in chunks as VITA-49 packets are received from the
radio. Also the transfer of data is transparent; no byte order correction or other data modification is performed by
either the radio or the waveform API. Since the transport for the data is via VITA-49 packets, there is an upper limit
on the amount of data you can send in a single `waveform_send_byte_data` call. This should be set to 1436 bytes, but
`waveform_send_byte_data` will return an error if you exceed the limit.

### Sending Control Commands

Commands may be sent to the radio control channel to affect its operation. These can be used for tuning frequency,
requesting that the transmitter be keyed, changing filter parameters, or a variety of other functions. Detailed command
syntax information is available on the [FlexRadio Wiki](http://wiki.flexradio.com/index.php?title=SmartSDR_TCP/IP_API).

The API library has functions to assist you in sending control commands to the radio. The primary of these
is [`waveform_send_api_command_cb`](html/waveform__api_8h.html#a0255c1e92befc590cd54b55aee8143af). This function uses
a `printf(3)` style varargs syntax to send command strings to the radio. In addition, this command provides for the API
author to assign a response callback that will be called when the radio responds to the command. This is useful for when
the command returns a parameter in its message that the waveform may wish to use. The API itself uses this to determine
mappings between meter names and index numbers. Since the API library is based on asynchronous concepts, there is no
idea of blocking a call until the response returns, so you must structure your callbacks such that your desired program
flow is achieved. The callback can be passed an optional context to store call specific data as necessary. This pointer
is passed unmodified to the callback.

If you do not care about the response from a command, you may
use [`waveform_send_api_command`](html/waveform__api_8h.html#a2950e21aa7ceef26c56148bbf1f6b82c) which does not invoke a
callback. The implementation of this function is as a macro that passes `NULL` to both the callback and argument
parameters of the `waveform_send_api_command_cb` function. Note that this usages are slightly more efficient than their
callback counterparts because the implementation doesn't store state for these nonexistent callback requests.

### Keyword Arguments - Not Yet Implemented
Many radio messages contain "keyword arguments" in the format `key=value`.  For example, a status message may contain, in part, `radio slices=4 panadapters=4 lineout_gain=60 lineout_mute=0 headphone_gain=80 headphone_mute=0 remote_on_enabled=0 pll_done=0`.  There are numerous keyword arguments in this line: `slices=4`, `lineout_gain=60`, etc. Many times a waveform only cares about a particular key in a status line. Since callbacks of this type almost universally recieve the `argc`/`argv` format arguments common in UNIX implementations, the API provides some functions to parse these.

### Radio Status
The radio sends status updates to the waveform over the control connection. These status updates include changes to the slice reciever parameters, updates about client connections, or any number of other radio operating parameters. By default, the API library will subscribe to interlock and slice updates as those are necessary for the internal operation of the library. For any other status messages to be sent from the radio, you must subscribe to them using the [`sub`](http://wiki.flexradio.com/index.php?title=TCP/IP_sub) command.

Once you subscribe to these status messages, you can set up callbacks to be called when the messages are recieved by the control connection using the [`waveform_register_status_cb`](html/waveform__api_8h.html#acddede717f247e00d3a40081084b60ab) function. The function will take the name of the status for which you wish to have callbacks called. For example, if you wish to be notified on changes to the radio's slice recievers, you would use a name of `slice` for the `command_name` parameter in the fucntion call.  Once again, the callback can recieve a context pointer argument for you to pass context to the status. Remember you can also use the global waveform context as well.

### Waveform Commands
Clients can send commands to waveforms in order to be able to change waveform parameters like baud rates, modulation subtypes, and other waveform specific operating parameters. These parameters are up to the waveform author to define and implement. The API delivers notification of these commands via the [`waveform_register_command_cb`](html/waveform__api_8h.html#a8fe52b3ac24f8fa2a43fe37f3aa9b237) function. Like with status callbacks, you will register the callback with a command name which represents the first parameter passed back from the client (`argv[0]` if you will).  The callback is passed the command parsed into `argc`/`argv` format. It is suggested that a good pattern is the implementation of a set/get command set followed by keyword arguments to set the various parameters.

The callback is expected to return a signed integer representing the status of the command. The value `0` is assumed to be a successfullly executed command, while anything else is an error. Error codes are to be defined by the waveform author and are passed back to the client upon command completion.

### Metering
It is sometimes useful for the waveform to pass back qualitative data about its functioning. These can be things like BER, bits recieved, frequency offset, timing offset, etc. The waveform API provides "metering" functionality to be able to pass these back to any interested clients. The metering API utilizes VITA-49 packets to send the values back to the radio.

The API library provides a few functions to assist with manging the metering API. First, meters must be registered via the command socket to be active. The [`waveform_register_meter`](html/waveform__api_8h.html#a614e0ea5956c33b17d1c6b673a8fb12b) function allows the waveform author to create the meter in the radio. This function takes a name for the meter, minimum and maximum values, and an enum representing the units of the meter. Commonly this is dB, but call also be volts, amps, and other electrical values. There is currently no value for "unitless" values.

Once a meter is registered, the meter value can be set by using the [`waveform_meter_set_float_value`](html/waveform__api_8h.html#adccf6f1a025e3174678e9dc52f493d42) or [`waveform_meter_set_int_value`]() functions. These will set meter values to integer or floating point values respectively. Note that the actual meter in the VITA-49 packet is a 16-bit unsigned integer. When `waveform_meter_set_float_value` is used, the float is converted to a fixed-point value with 6 bits used for the fractional portion. Consider this accordingly when you set floating point values.

Once you have set all of your desirec meter values, you need to arrange for them to be sent to the radio. This is accomplished with the [`waveform_meters_send`](html/waveform__api_8h.html#acdf6df9cbbef4b2f40a35ee144ab14cd) function. Once the meters are sent to the radio, the values are reset to an "unset" state until set again. When sending, `waveform_meters_send` *only* sends values that have been subsequently set using the `waveform_meter_set*` functions. This allows you to optimize your meters by sending only values that may have changed.

### Discovery
Radios broadcast a discovery packet to the network once per second to announce their presence. This packet contains various information on how to contact the radio. The waveform API library contains a function [`waveform_discover_radio`](html/waveform__api_8h.html#a879406fb6396641466e35a4e18dd0f07) to utilize this functionality.  Upon execution, the function will wait for the `timeout` listening for radio packets. It will return either the IP address of the *first* radio it hears, or NULL if no radios were found before the timeout. This `struct sockaddr_in *` can be used with `waveform_radio_create` to create your radio structure.

It should be noted that this feature is of some limited value in the waveform context for a couple of reasons:

1. The waveform usually will be passed the IP address of the corresponding radio when it is executed on the command line from the radio. This should be used in preference to any discovery since it is guaranteed to be correct.
2. The discovery packets do not discriminate local vs. remote radios. This can cause the nonsensical condition where a waveform executing on one radio is servicing another radio on the network, which is undesireable and difficult to debug.

The discovery can be useful, though, under development scenarios and can be a fallback if other methods of finding a radio fail or do not exist.
