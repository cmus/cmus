/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2004-2006 Timo Hirvonen
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

#ifndef _COMMAND_MODE_H
#define _COMMAND_MODE_H

#include "uchar.h"

enum {
	/* executing command is disabled over net */
	CMD_UNSAFE	= 1 << 0,
	/* execute command after every typed/deleted character */
	CMD_LIVE	= 1 << 1,
};

struct command {
	const char *name;
	void (*func)(char *arg);

	/* min/max number of arguments */
	int min_args;
	int max_args;

	void (*expand)(const char *str);

	/* bind count (0 means: unbound) */
	int bc;

	/* CMD_* */
	unsigned int flags;
};

extern struct command commands[];
extern int run_only_safe_commands;

void command_mode_ch(uchar ch);
void command_mode_escape(int c);
void command_mode_key(int key);
void commands_init(void);
void commands_exit(void);
int parse_command(const char *buf, char **cmdp, char **argp);
void run_parsed_command(char *cmd, char *arg);
void run_command(const char *buf);

struct command *get_command(const char *str);

void view_clear(int view);
void view_add(int view, char *arg, int prepend);
void view_load(int view, char *arg);
void view_save(int view, char *arg, int to_stdout, int filtered, int extended);

#endif
