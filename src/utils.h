// SPDX-License-Identifier: LGPL-3.0-or-later
/// @file utils.h
/// @brief General Utilities
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

#ifndef UTILS_H_
#define UTILS_H_

// ****************************************
// Third Party Library Includes
// ****************************************
#include <sds.h>

// ****************************************
// Project Includes
// ****************************************
#include "waveform_api.h"

// ****************************************
// Macros
// ****************************************
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) ); })

/// @brief Log a message to the console
/// @details Logs a message to the console given a log level.  Any messages at the
///          current log level or above will be logged to the console.
/// @param level The log level at which to log this message
/// @param fmt printf(3) style format string for the log message
#define waveform_log(level, fmt, ...) \
   if (level >= waveform_log_level)   \
      fprintf(stderr, "%s:%d(%s): %s: " fmt, FILE_BASENAME, __LINE__, __func__, waveform_log_level_describe(level), ##__VA_ARGS__);

// ****************************************
// External Definitions
// ****************************************
extern enum waveform_log_levels waveform_log_level;
extern const char* waveform_log_level_describe(int level);

// ****************************************
// Global Functions
// ****************************************
/// @brief Converts a floating point format number to a fixed point number.
/// @details Converts a floating point value into a fixed point value in a 16-bit wide signed field.
/// @param input The number to convert
/// @param fractional_bits The number of bits to reserve for the fractional portion of the value
/// @returns A fixed-point value in a 16-bit integer representing the floating point value
short float_to_fixed(double input, unsigned char fractional_bits);

/// @brief Find a "Keyword" argument in a set of parsed arguments
/// @details A keyword argument is in the format "keyword=value".  These are very common structures in the API, so we provide functionality to
///          parse them easily.  Given the key and set of arguments, we'll return the value as a string.
/// @param argc The number of arguments passed in
/// @param argv A reference to an array of string arguments to parse
/// @param key The key of the keyword argument you wish to extract
/// @returns A string value of the value of the argument with the given keyword or NULL on failure to find an element with that keyword.
sds find_kwarg(int argc, sds* argv, sds key);

#endif// UTILS_H_
