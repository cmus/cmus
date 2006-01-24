/*
 * Copyright 2005 Timo Hirvonen
 */

#ifndef OPTIONS_H
#define OPTIONS_H

#include "list.h"

#define OPTION_MAX_SIZE	256

typedef void (*opt_get_cb)(unsigned int id, char *buf);
typedef void (*opt_set_cb)(unsigned int id, const char *buf);
typedef void (*opt_toggle_cb)(unsigned int id);

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
	NR_VIEWS
};

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

#define BRIGHT (1 << 3)

extern char *output_plugin;
extern char *status_display_program;
extern int confirm_run;
extern int show_hidden;
extern int show_remaining_time;
extern int play_library;
extern int repeat;
extern int shuffle;

extern const char * const aaa_mode_names[];
extern const char * const view_names[NR_VIEWS + 1];

extern int bg_colors[NR_COLORS];
extern int fg_colors[NR_COLORS];

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


/* build option list */
void options_add(void);

/* load options from the config file */
void options_load(void);

/* save options */
void options_exit(void);

void option_add(const char *name, unsigned int id, opt_get_cb get,
		opt_set_cb set, opt_toggle_cb toggle);
struct cmus_opt *option_find(const char *name);
void option_set(const char *name, const char *value);
int parse_enum(const char *buf, int minval, int maxval, const char * const names[], int *val);

#endif
