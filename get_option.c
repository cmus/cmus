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

#include <get_option.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern char *program_name;

static int get_short_option(char **argvp[], const struct option *options,
		int print_error, int *idx, char **arg, int *pos)
{
	char **argv = *argvp;
	int i;
	char ch;

	ch = (*argv)[1 + *pos];
	i = 0;
	while (1) {
		if (options[i].short_opt == 0 && options[i].long_opt == NULL) {
			/* unrecognized */
			if (print_error)
				fprintf(stderr, "%s: unrecognized option `-%c'\n",
						program_name, ch);
			return 2;
		}
		if (ch == options[i].short_opt) {
			*idx = i;
			break;
		}
		i++;
	}
	if (options[*idx].has_arg) {
		/* has argument */
		if ((*argv)[2 + *pos]) {
			/* argument concated */
			*arg = *argv + 2 + *pos;
			*pos = 0;
			argv++;
			*argvp = argv;
			return 0;
		}
		argv++;
		if (*argv == NULL) {
			/* missing parameter */
			if (print_error)
				fprintf(stderr, "%s: option `-%c' requires an argument\n",
						program_name, ch);
			return 4;
		}
		/* argument in next argv */
		*arg = *argv;
		*pos = 0;
		argv++;
		*argvp = argv;
	} else {
		/* no argument */
		*arg = NULL;
		if ((*argv)[2 + *pos] == 0) {
			/* no more short options in this argv */
			argv++;
			*argvp = argv;
			*pos = 0;
		} else {
			/* more short options in this argv */
			(*pos)++;
		}
	}
	return 0;
}

static int get_long_option(char **argvp[], const struct option *options,
		int print_error, int *idx, char **arg)
{
	char **argv = *argvp;
	char *opt;
	int len, i, num;

	if ((*argv)[2] == 0)
		return 1;
	opt = (*argv) + 2;
	len = strlen(opt);
	i = 0;
	*idx = -1;
	num = 0;
	while (1) {
		if (options[i].short_opt == 0 && options[i].long_opt == NULL)
			break;
		if (options[i].long_opt && strncmp(opt, options[i].long_opt, len) == 0) {
			*idx = i;
			num++;
			if (options[i].long_opt[len] == 0) {
				/* exact */
				num = 1;
				break;
			}
		}
		i++;
	}
	if (num > 1) {
		if (print_error)
			fprintf(stderr, "%s: option `--%s' is ambiguous\n",
					program_name, opt);
		return 3;
	}
	if (num == 0) {
		if (print_error)
			fprintf(stderr, "%s: unrecognized option `--%s'\n",
					program_name, opt);
		return 2;
	}
	if (options[*idx].has_arg) {
/* 		printf("option %s has argument\n", opt); */
		argv++;
		if (*argv == NULL) {
			/* missing parameter */
			if (print_error)
				fprintf(stderr, "%s: option `--%s' requires an argument\n",
						program_name, opt);
			return 4;
		}
		*arg = *argv;
		argv++;
	} else {
/* 		printf("option %s has no argument\n", opt); */
		*arg = NULL;
		argv++;
	}
	*argvp = argv;
	return 0;
}

int get_option(char **argvp[], const struct option *options,
		int print_error, int *idx, char **arg)
{
	static int argv_pos = 0;
	int rc;

	if (argv_pos) {
		rc = get_short_option(argvp, options, print_error, idx, arg,
				&argv_pos);
	} else {
		char **argv = *argvp;

		if (*argv == NULL)
			return 1;
		if ((*argv)[0] != '-')
			return 1;
		if ((*argv)[1] == 0)
			return 1;
		if ((*argv)[1] == '-') {
			if ((*argv)[2] == 0) {
				/* '--' => no more options */
				*argvp = argv + 1;
				return 1;
			}
			rc = get_long_option(argvp, options, print_error, idx,
					arg);
		} else {
			rc = get_short_option(argvp, options, print_error, idx,
					arg, &argv_pos);
		}
	}
	if (rc > 1 && print_error)
		fprintf(stderr, "Try `%s --help' for more information.\n",
				program_name);
	return rc;
}
