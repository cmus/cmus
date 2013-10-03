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

#ifndef OPTIONS_H
#define OPTIONS_H

#include "list.h"

#define OPTION_MAX_SIZE	256

typedef void (*opt_get_cb)(unsigned int id, char *buf);
typedef void (*opt_set_cb)(unsigned int id, const char *buf);
typedef void (*opt_toggle_cb)(unsigned int id);

enum {
	OPT_PROGRAM_PATH = 1 << 0,
};

struct cmus_opt {
	struct list_head node;

	const char *name;

	/* If there are many similar options you should write generic get(),
	 * set() and optionally toggle() and set id to index of option array or
	 * what ever.
	 *
	 * Useful for colors, format strings and plugin options.
	 */
	unsigned int id;

	opt_get_cb get;
	opt_set_cb set;

	/* NULL if not toggle-able */
	opt_toggle_cb toggle;

	unsigned int flags;
};

extern struct list_head option_head;
extern int nr_options;

enum {
	TREE_VIEW,
	SORTED_VIEW,
	PLAYLIST_VIEW,
	QUEUE_VIEW,
	BROWSER_VIEW,
	FILTERS_VIEW,
	HELP_VIEW,
	NR_VIEWS
};

enum {
	COLOR_CMDLINE_BG,
	COLOR_CMDLINE_FG,
	COLOR_ERROR,
	COLOR_INFO,

	COLOR_SEPARATOR,
	COLOR_STATUSLINE_BG,
	COLOR_STATUSLINE_FG,
	COLOR_TITLELINE_BG,

	COLOR_TITLELINE_FG,
	COLOR_WIN_BG,
	COLOR_WIN_CUR,
	COLOR_WIN_CUR_SEL_BG,

	COLOR_WIN_CUR_SEL_FG,
	COLOR_WIN_DIR,
	COLOR_WIN_FG,
	COLOR_WIN_INACTIVE_CUR_SEL_BG,

	COLOR_WIN_INACTIVE_CUR_SEL_FG,
	COLOR_WIN_INACTIVE_SEL_BG,
	COLOR_WIN_INACTIVE_SEL_FG,
	COLOR_WIN_SEL_BG,

	COLOR_WIN_SEL_FG,
	COLOR_WIN_TITLE_BG,
	COLOR_WIN_TITLE_FG,
	NR_COLORS
};

enum {
	COLOR_CMDLINE_ATTR,
	COLOR_STATUSLINE_ATTR,
	COLOR_TITLELINE_ATTR,
	COLOR_WIN_ATTR,
	COLOR_WIN_CUR_SEL_ATTR,
	COLOR_CUR_SEL_ATTR,
	COLOR_WIN_INACTIVE_CUR_SEL_ATTR,
	COLOR_WIN_INACTIVE_SEL_ATTR,
	COLOR_WIN_SEL_ATTR,
	COLOR_WIN_TITLE_ATTR,
	NR_ATTRS
};

#define BRIGHT (1 << 3)

extern char *cdda_device;
extern char *output_plugin;
extern char *status_display_program;
extern char *server_password;
extern int auto_reshuffle;
extern int confirm_run;
extern int resume_cmus;
extern int show_hidden;
extern int show_current_bitrate;
extern int show_playback_position;
extern int show_remaining_time;
extern int set_term_title;
extern int wrap_search;
extern int play_library;
extern int repeat;
extern int shuffle;
extern int follow;
extern int display_artist_sort_name;
extern int smart_artist_sort;
extern int scroll_offset;
extern int rewind_offset;
extern int skip_track_info;

extern const char * const aaa_mode_names[];
extern const char * const view_names[NR_VIEWS + 1];

extern int colors[NR_COLORS];
extern int attrs[NR_ATTRS];

/* format string for track window (tree view) */
extern char *track_win_format;
extern char *track_win_format_va;
extern char *track_win_alt_format;

/* format string for shuffle, sorted and play queue views */
extern char *list_win_format;
extern char *list_win_format_va;
extern char *list_win_alt_format;

/* format string for currently playing track */
extern char *current_format;
extern char *current_alt_format;

/* format string for window title */
extern char *window_title_format;
extern char *window_title_alt_format;

extern char *id3_default_charset;
extern char *icecast_default_charset;

/* build option list */
void options_add(void);

/* load options from the config file */
void options_load(void);

int source_file(const char *filename);

/* save options */
void options_exit(void);

/* load resume file */
void resume_load(void);
/* save resume file */
void resume_exit(void);

void option_add(const char *name, unsigned int id, opt_get_cb get,
		opt_set_cb set, opt_toggle_cb toggle, unsigned int flags);
struct cmus_opt *option_find(const char *name);
void option_set(const char *name, const char *value);
int parse_enum(const char *buf, int minval, int maxval, const char * const names[], int *val);

#endif
