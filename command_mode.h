/* 
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _COMMAND_MODE_H
#define _COMMAND_MODE_H

#include "uchar.h"

struct command {
	const char *name;
	void (*func)(char *arg);

	/* min/max number of arguments */
	int min_args;
	int max_args;

	void (*expand)(const char *str);

	/* bind count (0 means: unbound) */
	int bc;
};

extern struct command commands[];

void command_mode_ch(uchar ch);
void command_mode_key(int key);
void commands_init(void);
void commands_exit(void);
void run_command(const char *buf);

struct command *get_command(const char *str);

void view_clear(int view);
void view_add(int view, char *arg, int prepend);
void view_load(int view, char *arg);
void view_save(int view, char *arg);

#endif
