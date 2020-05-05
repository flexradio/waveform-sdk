// SPDX-Licence-Identifier: GPL-3.0-or-later
/*
 * utils.c - General Utilities
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

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <ctype.h>

#include "utils.h"

#define MAX_ARGS 128

void output(const char *fmt,...)
{
    va_list args;

    va_start(args, fmt);
    vprintf(fmt, args);
    fflush(stdout);
    va_end(args);
}

struct kwarg {
    char *key;
    char *value;
    struct kwarg *next;
};

static void kwarg_add(struct kwarg **args, char *token)
{
    assert(token != NULL);

    struct kwarg *current_arg;
    if (*args == NULL) {
        current_arg = *args = (struct kwarg *) malloc(sizeof(struct kwarg));
    } else {
        for (current_arg = *args; current_arg->next != NULL; current_arg = current_arg->next);
        current_arg->next = (struct kwarg *) malloc(sizeof(struct kwarg));
        current_arg = current_arg->next;
    }

    current_arg->next = NULL;
    current_arg->value = current_arg->key = token;
    strsep(&current_arg->value, "=");
    if (current_arg->value == NULL) {
        output("Couldn't find delimeter in %s\n", token);
        current_arg->value = "";
    }
}

void kwargs_destroy(struct kwarg **args)
{
    struct kwarg *current_arg, *next_arg;
    for (current_arg = *args, next_arg = (*args)->next; next_arg != NULL; current_arg = next_arg, next_arg = current_arg->next) {
        free(current_arg);
    }
    free(current_arg);
    *args = NULL;
}

char *find_kwarg(struct kwarg *args, char *key)
{
    if (args == NULL)
        return NULL;

    for(struct kwarg *current_arg = args; current_arg != NULL; current_arg = current_arg->next) {
        if(strcmp(current_arg->key, key) == 0) {
            return current_arg->value;
        }
    }

    return NULL;
}

struct kwarg *parse_kwargs(char **argv, int argc, int start)
{
    struct kwarg *kwargs = NULL;

    for (int i = start; i < argc; ++i)
        kwarg_add(&kwargs, argv[i]);

    return kwargs;
}

static char *trim(char *string)
{
    char *end;

    for (end = string + strlen(string) - 1; end > string && isspace((unsigned char) *end); end--);
    end[1] = '\0';

    return string;
}

int parse_argv(char *string, char **argv, int max_args)
{
	char **argptr;
	char *argsstring;
	int argc = 1;

	trim(string);

	argsstring = string;
	for(argptr = argv; (*argptr = strsep(&argsstring, " \t")) != NULL; ++argc)
		if (**argptr != '\0')
			if (++argptr >= &argv[max_args])
				break;

	return(argc - 1);
}

short float_to_fixed(double input, unsigned char fractional_bits)
{
    return (short)(round(input * (1u << fractional_bits)));
}

int dispatch_from_table(char *message, const struct dispatch_entry *dispatch_table)
{
    char *argv[MAX_ARGS];
    int argc;
    int i;

    assert(message != NULL);
    assert(dispatch_table != NULL);

    argc = parse_argv(message, argv, MAX_ARGS);
    if (argc < 1)
        return -1;

    for (i = 0; strlen(dispatch_table[i].name) > 0; ++i) {
        if (strncmp(dispatch_table[i].name, argv[0], 256) == 0) {
            assert(dispatch_table[i].handler != NULL);
            return (dispatch_table[i].handler)(argv, argc);
        }
    }

    return -1;
}