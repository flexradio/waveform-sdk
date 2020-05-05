// SPDX-Licence-Identifier: GPL-3.0-or-later
/*
 * utils.h - General Utilities
 *
 * Author: Annaliese McDermond <nh6z@nh6z.net>
 *
 * Copyright 2019 Annaliese McDermond
 *
 * This file is part of smartsdr-codec2.
 *
 * smartsdr-codec2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Foobar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with smartsdr-codec2.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef UTILS_H_
#define UTILS_H_

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef int (*dispatch_handler_t)(char **, int);

struct dispatch_entry {
    char name[256];
    dispatch_handler_t handler;
};

void output(const char *fmt,...);
int parse_argv(char *string, char **argv, int max_args);
short float_to_fixed(double input, unsigned char fractional_bits);
int dispatch_from_table(char *message, const struct dispatch_entry *dispatch_table);
struct kwarg *parse_kwargs(char **argv, int argc, int start);
char *find_kwarg(struct kwarg *args, char *key);
void kwargs_destroy(struct kwarg **args);

#endif /* UTILS_H_ */
