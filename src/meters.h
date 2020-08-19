// SPDX-License-Identifier: LGPL-3.0-or-later
/// @file meters.h
/// @brief Implementation of the metering components of the API
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

#include "waveform.h"

#ifndef WAVEFORM_SDK_METER_H
#define WAVEFORM_SDK_METER_H


// ****************************************
// Global Functions
// ****************************************

/// @brief Create the meters registered to the waveform in the API
/// @details Creates the meters that have been previously registered by the user with waveform_register_meter.
///          This function is called when the radio is finally connected to the waveform when we start it as a
///          part of the initialization procedure.
/// @param wf Reference to the waveform structure to create the meters
void waveform_create_meters(struct waveform_t* wf);

#endif// WAVEFORM_SDK_METER_H
