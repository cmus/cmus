/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2005 Timo Hirvonen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CMUS_PROG_H
#define CMUS_PROG_H

#include "compiler.h"

/* set in beginning of main */
extern char *program_name;

struct option {
	/* short option or 0 */
	int short_opt;

	/* long option or NULL */
	const char *long_opt;

	/* does option have an argument */
	int has_arg;
};

/*
 * arg: returned argument if .has_arg is 1
 *
 * returns: index to options array or -1 of no more options
 */
int get_option(char **argv[], const struct option *options, char **arg);

void warn(const char *format, ...) CMUS_FORMAT(1, 2);
void warn_errno(const char *format, ...) CMUS_FORMAT(1, 2);
void die(const char *format, ...) CMUS_FORMAT(1, 2) CMUS_NORETURN;
void die_errno(const char *format, ...) CMUS_FORMAT(1, 2) CMUS_NORETURN;

#endif
