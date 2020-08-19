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
   }

   return NULL;
}

inline short float_to_fixed(double input, unsigned char fractional_bits)
{
   return (short) (round(input * (1u << fractional_bits)));
}