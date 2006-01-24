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

#ifndef _UI_CURSES_H
#define _UI_CURSES_H

#include <search.h>
#include <compiler.h>

enum ui_input_mode {
	NORMAL_MODE,
	COMMAND_MODE,
	SEARCH_MODE
};

extern int ui_initialized;
extern enum ui_input_mode input_mode;
extern int cur_view;
extern struct searchable *searchable;
extern int display_errors;

/* usually ~/.config/cmus/lib.pl and ~/.config/cmus/playlist.pl */
extern char *lib_autosave_filename;
extern char *pl_autosave_filename;

/* current filename given by user */
extern char *lib_filename;
extern char *pl_filename;

void update_titleline(void);
void update_statusline(void);
void update_color(int idx);
void info_msg(const char *format, ...) __FORMAT(1, 2);
void error_msg(const char *format, ...) __FORMAT(1, 2);
int yes_no_query(const char *format, ...) __FORMAT(1, 2);
void search_not_found(void);

/* bindable */
void set_view(int view);
void enter_command_mode(void);
void enter_search_mode(void);
void enter_search_backward_mode(void);
void quit(void);

#endif
