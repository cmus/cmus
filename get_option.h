/* 
 * Copyright 2004 Timo Hirvonen
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _GET_OPTION_H
#define _GET_OPTION_H

struct option {
	/* short option or 0 */
	int short_opt;

	/* long option or NULL */
	char *long_opt;

	/* does option have an argument */
	unsigned int has_arg : 1;
};

/*
 * print_error:
 *     if 1 prints error message in standard format when needed
 *
 * idx:
 *     returned index to options array
 *
 * arg:
 *     returned argument or NULL
 *
 * returns:
 *     0 - ok
 *     1 - no more options
 *     2 - unrecognized option
 *     3 - ambiguous match
 *     4 - missing parameter
 *     (5 - extraneous parameter)
 */
extern int get_option(char **argv[], const struct option *options,
		int print_error, int *idx, char **arg);

/* extern int optind; */

#endif
