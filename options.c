/*
 * Copyright 2006 Timo Hirvonen
 */

#include "options.h"
#include "list.h"
#include "utils.h"
#include "xmalloc.h"
#include "player.h"
#include "buffer.h"
#include "ui_curses.h"
#include "format_print.h"
#include "sconf.h"
#include "cmus.h"
#include "misc.h"
#include "lib.h"
#include "pl.h"

#include <stdio.h>
#include <curses.h>

/* initialized option variables {{{ */

static int default_view = TREE_VIEW + 1;

char *op_name = NULL;
char *status_display_program = NULL;
int confirm_run = 1;
int show_remaining_time = 0;
int play_library = 1;
int repeat = 0;
int shuffle = 0;

int bg_colors[NR_COLORS] = {
	-1,
	-1,
	COLOR_WHITE,
	COLOR_WHITE,
	-1,
	-1,
	COLOR_BLUE,
	COLOR_BLUE,

	-1,
	COLOR_BLUE,
	-1,
	COLOR_WHITE,
	COLOR_BLUE,
	-1,
	-1,
	-1,
	-1
};

int fg_colors[NR_COLORS] = {
	-1,
	COLOR_YELLOW | BRIGHT,
	COLOR_BLACK,
	COLOR_YELLOW | BRIGHT,
	-1,
	COLOR_YELLOW | BRIGHT,
	COLOR_WHITE | BRIGHT,
	COLOR_YELLOW | BRIGHT,

	COLOR_BLUE,
	COLOR_WHITE | BRIGHT,
	-1,
	COLOR_BLACK,
	COLOR_WHITE | BRIGHT,
	COLOR_BLUE | BRIGHT,
	-1,
	COLOR_RED | BRIGHT,
	COLOR_YELLOW | BRIGHT
};

/* }}} */

/* uninitialized option variables {{{ */

char *track_win_format = NULL;
char *track_win_alt_format = NULL;
char *list_win_format = NULL;
char *list_win_alt_format = NULL;
char *current_format = NULL;
char *current_alt_format = NULL;
char *window_title_format = NULL;
char *window_title_alt_format = NULL;

/* }}} */

static void buf_int(char *buf, int val)
{
	snprintf(buf, OPTION_MAX_SIZE, "%d", val);
}

static int parse_int(const char *buf, int minval, int maxval, int *val)
{
	long int tmp;

	if (str_to_int(buf, &tmp) == -1 || tmp < minval || tmp > maxval) {
		error_msg("integer in range %d..%d expected", minval, maxval);
		return 0;
	}
	*val = tmp;
	return 1;
}

static int parse_enum(const char *buf, int minval, int maxval, const char * const names[], int *val)
{
	long int tmp;
	int i;

	if (str_to_int(buf, &tmp) == 0) {
		if (tmp < minval || tmp > maxval)
			goto err;
		*val = tmp;
		return 1;
	}

	for (i = 0; names[i]; i++) {
		if (strcasecmp(buf, names[i]) == 0) {
			*val = i + minval;
			return 1;
		}
	}
err:
	error_msg("name or integer in range %d..%d expected", minval, maxval);
	return 0;
}

static const char * const bool_names[] = {
	"false", "true", NULL
};

static int parse_bool(const char *buf, int *val)
{
	return parse_enum(buf, 0, 1, bool_names, val);
}

/* this is used as id in struct cmus_opt */
enum format_id {
	FMT_CURRENT_ALT,
	FMT_PLAYLIST_ALT,
	FMT_TITLE_ALT,
	FMT_TRACKWIN_ALT,
	FMT_CURRENT,
	FMT_PLAYLIST,
	FMT_TITLE,
	FMT_TRACKWIN
};
#define NR_FMTS 8

/* callbacks for normal options {{{ */

#define SECOND_SIZE (44100 * 16 / 8 * 2)
static void get_buffer_seconds(unsigned int id, char *buf)
{
	buf_int(buf, (player_get_buffer_chunks() * CHUNK_SIZE + SECOND_SIZE / 2) / SECOND_SIZE);
}

static void set_buffer_seconds(unsigned int id, const char *buf)
{
	int sec;

	if (parse_int(buf, 1, 20, &sec))
		player_set_buffer_chunks((sec * SECOND_SIZE + CHUNK_SIZE / 2) / CHUNK_SIZE);
}

static const char *valid_sort_keys[] = {
	"artist",
	"album",
	"title",
	"tracknumber",
	"discnumber",
	"date",
	"genre",
	"filename",
	NULL
};

static char **parse_sort_keys(const char *value)
{
	char **keys;
	int i, j;

	keys = get_words(value);

	for (i = 0; keys[i]; i++) {
		for (j = 0; valid_sort_keys[j]; j++) {
			if (strcmp(keys[i], valid_sort_keys[j]) == 0)
				break;
		}
		if (valid_sort_keys[j] == NULL) {
			error_msg("invalid sort key '%s'", keys[i]);
			free_str_array(keys);
			return NULL;
		}
	}
	return keys;
}

static void keys_to_str(char **keys, char *buf)
{
	int i, pos = 0;

	for (i = 0; keys[i]; i++) {
		const char *key = keys[i];
		int len = strlen(key);

		if (sizeof(buf) - pos - len - 2 < 0)
			break;

		memcpy(buf + pos, key, len);
		pos += len;
		buf[pos++] = ' ';
	}
	if (pos > 0)
		pos--;
	buf[pos] = 0;
}

static void get_lib_sort(unsigned int id, char *buf)
{
	keys_to_str(lib.sort_keys, buf);
}

static void set_lib_sort(unsigned int id, const char *buf)
{
	char **keys = parse_sort_keys(buf);

	if (keys)
		lib_set_sort_keys(keys);
}

static void get_pl_sort(unsigned int id, char *buf)
{
	keys_to_str(pl_sort_keys, buf);
}

static void set_pl_sort(unsigned int id, const char *buf)
{
	char **keys = parse_sort_keys(buf);

	if (keys)
		pl_set_sort_keys(keys);
}

static void get_output_plugin(unsigned int id, char *buf)
{
/* 	if (ui_initialized) { */
		char *value = player_get_op();

		if (value)
			strcpy(buf, value);
		free(value);
/*
 * 		return;
 * 	}
 * 	if (op_name)
 * 		strcpy(buf, op_name);
 */
}

static void set_output_plugin(unsigned int id, const char *buf)
{
/* 	if (ui_initialized) { */
		player_set_op(buf);
/*
 * 	} else {
 * 		op_name = xstrdup(buf);
 * 	}
 */
}

static void get_status_display_program(unsigned int id, char *buf)
{
	if (status_display_program)
		strcpy(buf, status_display_program);
}

static void set_status_display_program(unsigned int id, const char *buf)
{
	free(status_display_program);
	status_display_program = NULL;
	if (buf[0])
		status_display_program = xstrdup(buf);
}

/* }}} */

/* callbacks for toggle options {{{ */

static void get_continue(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[player_cont]);
}

static void set_continue(unsigned int id, const char *buf)
{
	if (!parse_bool(buf, &player_cont))
		return;
	update_statusline();
}

static void toggle_continue(unsigned int id)
{
	player_cont ^= 1;
	update_statusline();
}

static void get_confirm_run(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[confirm_run]);
}

static void set_confirm_run(unsigned int id, const char *buf)
{
	parse_bool(buf, &confirm_run);
}

static void toggle_confirm_run(unsigned int id)
{
	confirm_run ^= 1;
}

static const char * const view_names[NR_VIEWS + 1] = {
	"tree", "sorted", "playlist", "queue", "browser", "filters", NULL
};

static void get_default_view(unsigned int id, char *buf)
{
	strcpy(buf, view_names[default_view - 1]);
}

static void set_default_view(unsigned int id, const char *buf)
{
	parse_enum(buf, 1, NR_VIEWS, view_names, &default_view);
}

static void toggle_default_view(unsigned int id)
{
	default_view++;
	if (default_view > NR_VIEWS)
		default_view = 1;
}

static void get_play_library(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[play_library]);
}

static void set_play_library(unsigned int id, const char *buf)
{
	if (!parse_bool(buf, &play_library))
		return;
	update_statusline();
}

static void toggle_play_library(unsigned int id)
{
	play_library ^= 1;
	update_statusline();
}

static void get_play_sorted(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[lib.play_sorted]);
}

static void set_play_sorted(unsigned int id, const char *buf)
{
	int tmp;

	if (!parse_bool(buf, &tmp))
		return;

	lib.play_sorted = tmp;
	update_statusline();
}

static void toggle_play_sorted(unsigned int id)
{
	lib_lock();
	lib.play_sorted = lib.play_sorted ^ 1;

	/* shuffle would override play_sorted... */
	if (lib.play_sorted) {
		/* play_sorted makes no sense in playlist */
		play_library = 1;
		shuffle = 0;
	}

	lib_unlock();
	update_statusline();
}

const char * const aaa_mode_names[] = {
	"all", "artist", "album", NULL
};

static void get_aaa_mode(unsigned int id, char *buf)
{
	strcpy(buf, aaa_mode_names[lib.aaa_mode]);
}

static void set_aaa_mode(unsigned int id, const char *buf)
{
	int tmp;

	if (!parse_enum(buf, 0, 2, aaa_mode_names, &tmp))
		return;

	lib.aaa_mode = tmp;
	update_statusline();
}

static void toggle_aaa_mode(unsigned int id)
{
	lib_lock();

	/* aaa mode makes no sense in playlist */
	play_library = 1;

	lib.aaa_mode++;
	lib.aaa_mode %= 3;
	lib_unlock();
	update_statusline();
}

static void get_repeat(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[repeat]);
}

static void set_repeat(unsigned int id, const char *buf)
{
	if (!parse_bool(buf, &repeat))
		return;
	update_statusline();
}

static void toggle_repeat(unsigned int id)
{
	repeat ^= 1;
	update_statusline();
}

static void get_show_remaining_time(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[show_remaining_time]);
}

static void set_show_remaining_time(unsigned int id, const char *buf)
{
	if (!parse_bool(buf, &show_remaining_time))
		return;
	update_statusline();
}

static void toggle_show_remaining_time(unsigned int id)
{
	show_remaining_time ^= 1;
	update_statusline();
}

static void get_shuffle(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[shuffle]);
}

static void set_shuffle(unsigned int id, const char *buf)
{
	if (!parse_bool(buf, &shuffle))
		return;
	update_statusline();
}

static void toggle_shuffle(unsigned int id)
{
	shuffle ^= 1;
	update_statusline();
}

/* }}} */

/* special callbacks (id set) {{{ */

static const char * const color_enum_names[1 + 8 * 2 + 1] = {
	"default",
	"black", "red", "green", "yellow", "blue", "magenta", "cyan", "gray",
	/* lightblack?  consistency is good */
	"lightblack", "lightred", "lightgreen", "lightyellow", "lightblue", "lightmagenta", "lightcyan", "lightgray",
	NULL
};

static void get_color(unsigned int id, char *buf)
{
	int val;

	if (id < NR_COLORS) {
		val = bg_colors[id];
	} else {
		val = fg_colors[id - NR_COLORS];
	}
	if (val < 16) {
		strcpy(buf, color_enum_names[val + 1]);
	} else {
		buf_int(buf, val);
	}
}

static void set_color(unsigned int id, const char *buf)
{
	int color;

	if (!parse_enum(buf, -1, 255, color_enum_names, &color))
		return;

	if (id < NR_COLORS) {
		bg_colors[id] = color;
	} else {
		id -= NR_COLORS;
		fg_colors[id] = color;
	}
	update_color(id);
}

/* toggle -1..7 for bg and -1..15 for fg */
static void toggle_color(unsigned int id)
{
	int color;

	if (id < NR_COLORS) {
		color = bg_colors[id] + 1;
		if (color >= 8)
			color = -1;
		bg_colors[id] = color;
	} else {
		id -= NR_COLORS;
		color = fg_colors[id] + 1;
		if (color >= 16)
			color = -1;
		fg_colors[id] = color;
	}
	update_color(id);
}

static char **id_to_fmt(enum format_id id)
{
	switch (id) {
	case FMT_CURRENT_ALT:
		return &current_alt_format;
	case FMT_PLAYLIST_ALT:
		return &list_win_alt_format;
	case FMT_TITLE_ALT:
		return &window_title_alt_format;
	case FMT_TRACKWIN_ALT:
		return &track_win_alt_format;
	case FMT_CURRENT:
		return &current_format;
	case FMT_PLAYLIST:
		return &list_win_format;
	case FMT_TITLE:
		return &window_title_format;
	case FMT_TRACKWIN:
		return &track_win_format;
	}
	return NULL;
}

static void get_format(unsigned int id, char *buf)
{
	char **fmtp = id_to_fmt(id);

	strcpy(buf, *fmtp);
}

static void set_format(unsigned int id, const char *buf)
{
	char **fmtp = id_to_fmt(id);

	if (!format_valid(buf)) {
		error_msg("invalid format string");
		return;
	}
	free(*fmtp);
	*fmtp = xstrdup(buf);
}

/* }}} */

#define DN(name) { #name, get_ ## name, set_ ## name, NULL },
#define DT(name) { #name, get_ ## name, set_ ## name, toggle_ ## name },

static const struct {
	const char *name;
	opt_get_cb get;
	opt_set_cb set;
	opt_toggle_cb toggle;
} simple_options[] = {
	DT(aaa_mode)
	DN(buffer_seconds)
	DT(confirm_run)
	DT(continue)
	DT(default_view)
	DN(lib_sort)
	DN(output_plugin)
	DN(pl_sort)
	DT(play_library)
	DT(play_sorted)
	DT(repeat)
	DT(show_remaining_time)
	DT(shuffle)
	DN(status_display_program)
	{ NULL, NULL, NULL, NULL }
};

static const char * const color_names[NR_COLORS * 2] = {
	"color_row_bg",
	"color_row_cur_bg",
	"color_row_sel_bg",
	"color_row_sel_cur_bg",
	"color_row_active_bg",
	"color_row_active_cur_bg",
	"color_row_active_sel_bg",
	"color_row_active_sel_cur_bg",
	"color_separator_bg",
	"color_title_bg",
	"color_commandline_bg",
	"color_statusline_bg",
	"color_titleline_bg",
	"color_browser_dir_bg",
	"color_browser_file_bg",
	"color_error_bg",
	"color_info_bg",

	"color_row_fg",
	"color_row_cur_fg",
	"color_row_sel_fg",
	"color_row_sel_cur_fg",
	"color_row_active_fg",
	"color_row_active_cur_fg",
	"color_row_active_sel_fg",
	"color_row_active_sel_cur_fg",
	"color_separator_fg",
	"color_title_fg",
	"color_commandline_fg",
	"color_statusline_fg",
	"color_titleline_fg",
	"color_browser_dir_fg",
	"color_browser_file_fg",
	"color_error_fg",
	"color_info_fg"
};

/* default values for the variables which we must initialize but
 * can't do it statically */
static const struct {
	const char *name;
	const char *value;
} str_defaults[] = {
	{ "altformat_current",	" %F " },
	{ "altformat_playlist",	" %f%= %d " },
	{ "altformat_title",	"%f" },
	{ "altformat_trackwin",	" %f%= %d " },
	{ "format_current",	" %a - %l - %02n. %t%= %y " },
	{ "format_playlist",	" %a - %l - %02n. %t%= %y %d " },
	{ "format_title",	"%a - %l - %t (%y)" },
	{ "format_trackwin",	" %02n. %t%= %y %d " },

	{ "lib_sort"	,	"artist album discnumber tracknumber title filename" },
	{ "pl_sort",		"" },
	{ NULL, NULL }
};

LIST_HEAD(option_head);
int nr_options = 0;

void option_add(const char *name, unsigned int id, opt_get_cb get,
		opt_set_cb set, opt_toggle_cb toggle)
{
	struct cmus_opt *opt = xnew(struct cmus_opt, 1);
	struct list_head *item;

	opt->name = name;
	opt->id = id;
	opt->get = get;
	opt->set = set;
	opt->toggle = toggle;

	item = option_head.next;
	while (item != &option_head) {
		struct cmus_opt *o = container_of(item, struct cmus_opt, node);

		if (strcmp(name, o->name) < 0)
			break;
		item = item->next;
	}
	/* add before item */
	list_add_tail(&opt->node, item);
	nr_options++;
}

struct cmus_opt *option_find(const char *name)
{
	struct cmus_opt *opt;

	list_for_each_entry(opt, &option_head, node) {
		if (strcmp(name, opt->name) == 0)
			return opt;
	}
	error_msg("no such option %s", name);
	return NULL;
}

void option_set(const char *name, const char *value)
{
	struct cmus_opt *opt = option_find(name);

	if (opt)
		opt->set(opt->id, value);
}

static void get_op_option(unsigned int id, char *buf)
{
	char *val = NULL;

	player_get_op_option(id, &val);
	if (val)
		strcpy(buf, val);
}

static void set_op_option(unsigned int id, const char *buf)
{
	player_set_op_option(id, buf);
}

/* id is ((plugin_index << 16) | option_index) */
static void handle_op_option(unsigned int id, const char *name)
{
	option_add(xstrdup(name), id, get_op_option, set_op_option, NULL);
}

void options_init(void)
{
	struct cmus_opt *opt;
	int i;

	/* add options */

	for (i = 0; simple_options[i].name; i++)
		option_add(simple_options[i].name, 0, simple_options[i].get,
				simple_options[i].set, simple_options[i].toggle);

	for (i = 0; i < NR_FMTS; i++)
		option_add(str_defaults[i].name, i, get_format, set_format, NULL);

	for (i = 0; i < NR_COLORS * 2; i++)
		option_add(color_names[i], i, get_color, set_color, toggle_color);

	player_for_each_op_option(handle_op_option);

	/* initialize those that can't be statically initialized */
	for (i = 0; str_defaults[i].name; i++)
		option_set(str_defaults[i].name, str_defaults[i].value);

	/* load options from the config file */

/* 	sconf_load(); */

	list_for_each_entry(opt, &option_head, node) {
		char *val;

		if (sconf_get_str_option(opt->name, &val)) {
			opt->set(opt->id, val);
			free(val);
		}
	}

	cur_view = default_view;
}

void options_exit(void)
{
	char buf[OPTION_MAX_SIZE];
	struct cmus_opt *opt;

	list_for_each_entry(opt, &option_head, node) {
		opt->get(opt->id, buf);
		sconf_set_str_option(opt->name, buf);
	}
/* 	sconf_save(); */
}
