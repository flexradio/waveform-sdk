#FlexRadio Waveform Application Programming Interface

**NOTICE: This documentation and the API within are in a preliminary state. The API is subject to change at any time until finalized.** 

##About Waveforms
Waveforms are pluggable modules used in FLEX radios to extend functionality by providing additional modulation and demodulation modes. These modes can implement data and voice based modes for use by the radio's user.

A waveform is implemented using two distinct but cooperating interfaces: the control or API interface, and the data or VITA-49 interface. The control interface uses TCP/IP to present a text based interface for the waveform to perform functions such as commanding the radio to tune frequencies, key the transmitter, change filter parameters, and even register the waveform itself with the radio. Documentation of the control interface is available on the [FlexRadio Wiki](http://wiki.flexradio.com/index.php?title=SmartSDR_TCP/IP_API) which details the structure and syntax of commands issued to the radio over the control socket.

The data interface exchanges UDP/IP data packets representing the signals from the receiver, to the transmitter, from the microphone and to the speaker. These data packets can contain either raw in-phase and qudrature, or (de)modulated right and left stero channel samples. Using a combination of these interfaces allows a waveform to implement alternative modes in the radio. These packets are formatted in [VITA-49](https://www.vita.com/resources/Learn/VITA49%20Brief%20for%20HPEC%209Sept2014%20Final.pptx) standard format.  

The data interface also provides the ability for the waveform to pass back "meter" packets to the radio to inform it of statistics specific to the waveform. This can include parameters such as BER, packet loss, frequency offset, timing and other parameters specific to the waveform's implementation. These can optionally be displayed by clients connecting to the radio.

All communications between the radio and the waveform occur over a network API, even when the waveform runs on a CPU internal to the radio.

##The Waveform API
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

###Basic API Structure
There are a few steps required to instantiate and use the waveform.

1. Optionally discover a radio on the network. If you already know the IP address of the radio (if, for example, it is passed to you on a command line), you may skip this step.
2. Instantiate an instance of a radio using the [`waveform_radio_create`](html/waveform__api_8h.html#a28984b2443be194b7236db2dd5ceb1f7) function.
3. Register your new mode(s) to the radio by using the [`waveform_create`](file:///Users/anna.mcdermond/CLionProjects/waveform-sdk/docs/html/waveform__api_8h.html#a72e601dbcd68cd896c262b57e9f86018) function.
4. Register your desired callbacks to respond to when events happen on the radio.  These will include data packets being presented for processing, status events occuring on the radio, commands being issued for the waveform itself, or waveform lifecycle state changes.
5. Start the main waveform event loop using the [`waveform_radio_start`](html/waveform__api_8h.html#a35e070d291054bfac3961ca172d8e77a) function.
6. Wait for the event loop to stop because the waveform has been forcibly disconnected by using the [`waveform_radio_wait`](html/waveform__api_8h.html#adfcb97da8ed61e04229cb2e6fb87be7c) function.
7. Clean up your resources and cease execution.

Please note that it is currently _not_ supported to register new callbacks to the API after `waveform_radio_start` has been called. While this may indeed work, it is not supported and to do so is undefined and may exhibit undesired behavior. You have been warned.

###Waveform Lifecycle
A waveform's state callback is invoked for one of four events:

1. The waveform has become active (usually by the user selecting a mode the waveform handles on the user interface device).
2. The waveform has been deactivated (usually by the user selecting another mode on the user interface device that the waveform does not handle).
3. The radio intends to begin transmitting (usually by a user pressing the "PTT" or "MOX" buttons on the radio).
4. The radio intends to stop transmitting (usually by the user relasing the "PTT" or toggling the "MOX" button on the radio).

####The Waveform Activates or Deactivates
When the waveform recieves this event, it needs to make all preparations to become active. The waveform API will begin recieving data packets from the radio and routing them to the data callbacks when it queues the callback for this event. Any preparation needed to process these packets must be done in the waveform activation callback.

Conversely when the waveform deactivates, all resources used by the waveform for processing data should be released and processing stopped. The user may not activate the waveform for a long time (days, weeks, months) so resources should not be kept around just for optimization purposes of a single waveform.

####The Waveform Intends to Begin or Stop Transmitting
This callback is called when the user has requested the radio start or stop transmitting for some reason. This could be because the user has pressed a UI interface control, or possibly because the waveform itself has executed a transmit command over the command interface. Either way, the waveform must finish any processing for and cease the corresponding recieve or transmit stream before starting the converse stream. For example, if the `PTT_REQUESTED` event is received, the waveform must cease processing the receive stream and sending data to the speaker. The radio will not begin transmitting until it has ceased receiving packets for the speaker stream.
