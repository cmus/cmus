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

#ifndef _COMMAND_MODE_H
#define _COMMAND_MODE_H

#include <list.h>
#include <uchar.h>

struct command_mode_option;

typedef void (*option_get_func)(const struct command_mode_option *opt, char **value);
typedef void (*option_set_func)(const struct command_mode_option *opt, const char *value);

struct command_mode_option {
	struct list_head node;
	const char *name;
	option_get_func get;
	option_set_func set;
	void *data;
};

extern int confirm_run;

extern void command_mode_ch(uchar ch);
extern void command_mode_key(int key);
extern void option_add(const char *name, option_get_func get, option_set_func set, void *data);
extern void commands_init(void);
extern void commands_exit(void);
extern void run_command(const char *buf);

#endif
