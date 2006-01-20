/* 
 * Copyright 2004-2005 Timo Hirvonen
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

/* other views are defined in pl.h */
#define PLAYLIST_VIEW	2
#define QUEUE_VIEW	3
#define BROWSER_VIEW	4
#define FILTERS_VIEW	5

enum {
	COLOR_ROW,
	COLOR_ROW_CUR,
	COLOR_ROW_SEL,
	COLOR_ROW_SEL_CUR,
	COLOR_ROW_ACTIVE,
	COLOR_ROW_ACTIVE_CUR,
	COLOR_ROW_ACTIVE_SEL,
	COLOR_ROW_ACTIVE_SEL_CUR,
	COLOR_SEPARATOR,
	COLOR_TITLE,
	COLOR_COMMANDLINE,
	COLOR_STATUSLINE,
	COLOR_TITLELINE,
	COLOR_BROWSER_DIR,
	COLOR_BROWSER_FILE,
	COLOR_ERROR,
	COLOR_INFO,
	NR_COLORS
};

extern int ui_initialized;
extern enum ui_input_mode input_mode;
extern int cur_view;
extern struct searchable *searchable;

/* usually ~/.config/cmus/lib.pl and ~/.config/cmus/playlist.pl */
extern char *lib_autosave_filename;
extern char *pl_autosave_filename;

/* current filename given by user */
extern char *lib_filename;
extern char *pl_filename;

/* format string for track window (tree view) */
extern char *track_win_format;
extern char *track_win_alt_format;

/* format string for shuffle, sorted and play queue views */
extern char *list_win_format;
extern char *list_win_alt_format;

/* format string for currently playing track */
extern char *current_format;
extern char *current_alt_format;

/* format string for window title */
extern char *window_title_format;
extern char *window_title_alt_format;

/* program to run when status changes */
extern char *status_display_program;

/* add color_ prefix and _bg or _fg suffix */
extern const char * const color_names[NR_COLORS];
extern int bg_colors[NR_COLORS];
extern int fg_colors[NR_COLORS];

void update_titleline(void);
void update_statusline(void);
void update_color(int idx);
void info_msg(const char *format, ...) __FORMAT(1, 2);
void error_msg(const char *format, ...) __FORMAT(1, 2);
int yes_no_query(const char *format, ...) __FORMAT(1, 2);
void search_not_found(void);

/* bindable */
void toggle_remaining_time(void);
void enter_tree_view(void);
void enter_sorted_view(void);
void enter_playlist_view(void);
void enter_queue_view(void);
void enter_browser_view(void);
void enter_filters_view(void);
void enter_command_mode(void);
void enter_search_mode(void);
void enter_search_backward_mode(void);
void quit(void);

#endif
