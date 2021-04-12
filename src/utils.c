// SPDX-License-Identifier: LGPL-3.0-or-later
/// @file utils.c
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

// ****************************************
// System Includes
// ****************************************
#include <math.h>
#include <string.h>

// ****************************************
// Third Party Library Includes
// ****************************************
#include <sds.h>

// ****************************************
// Project Includes
// ****************************************
#include "utils.h"

// ****************************************
// Global Variables
// ****************************************
enum waveform_log_levels waveform_log_level = WF_LOG_ERROR;

static const struct waveform_log_messages {
   int level;
   const char* message;
} waveform_log_messages[] = {
      {WF_LOG_TRACE, "trace"},
      {WF_LOG_DEBUG, "debug"},
      {WF_LOG_INFO, "info"},
      {WF_LOG_WARNING, "warning"},
      {WF_LOG_ERROR, "error"},
      {WF_LOG_SEVERE, "severe"},
      {WF_LOG_FATAL, "fatal"}};

// ****************************************
// Static Functions
// ****************************************


// ****************************************
// Global Functions
// ****************************************
sds find_kwarg(int argc, sds* argv, sds key)
{
   for (int i = 0; i < argc; ++i)
   {
      int count;

      sds* kvp =
            sdssplitlen(argv[i], sdslen(argv[i]), "=", 1, &count);
      if (count != 2)
      {
         sdsfreesplitres(kvp, count);
         continue;
      }

      if (strcmp(kvp[0], key) == 0)
      {
         sds value = kvp[1];
         kvp[1] = NULL;
         sdsfreesplitres(kvp, count);
         return value;
      }

      sdsfreesplitres(kvp, count);
   }

   return NULL;
}

inline short float_to_fixed(double input, unsigned char fractional_bits)
{
   return (short) (round(input * (1u << fractional_bits)));
}

const char* waveform_log_level_describe(int level)
{
   for (size_t i = 0; i < sizeof(waveform_log_messages) / sizeof(waveform_log_messages)[0]; ++i)
   {
      if (level == waveform_log_messages[i].level)
      {
         return waveform_log_messages[i].message;
      }
   }
   return "unknown";
}

// ****************************************
// Public API Functions
// ****************************************
void waveform_set_log_level(enum waveform_log_levels level)
{
   waveform_log_level = level;
}

//  Flip around the order of the words in the fractional timestamp field if we're on a little endian
//  platform.  Otherwise they're in the wrong order when put into a uint64_t.
void swap_frac_timestamp(uint32_t* components)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
   uint32_t tmp = components[0];

   components[0] = components[1];
   components[1] = tmp;
#else
   return;
#endif
}