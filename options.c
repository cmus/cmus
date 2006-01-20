/*
 * command mode options (:set var=val)
 *
 * Copyright 2005 Timo Hirvonen
 */

#include <options.h>
#include <command_mode.h>
#include <ui_curses.h>
#include <format_print.h>
#include <lib.h>
#include <pl.h>
#include <play_queue.h>
#include <player.h>
#include <buffer.h>
#include <sconf.h>
#include <misc.h>
#include <xmalloc.h>
#include <utils.h>
#include <debug.h>

const char *valid_sort_keys[] = {
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

/*
 * opt->data is color index
 *         0..NR_COLORS-1    is bg
 * NR_COLORS..2*NR_COLORS-1  is fg
 */
static void get_color(const struct command_mode_option *opt, char **value)
{
	int i = (int)(long)opt->data;
	int color;
	char buf[32];

	BUG_ON(i < 0);
	BUG_ON(i >= 2 * NR_COLORS);

	if (i < NR_COLORS) {
		color = bg_colors[i];
	} else {
		color = fg_colors[i - NR_COLORS];
	}
	snprintf(buf, sizeof(buf), "%d", color);
	*value = xstrdup(buf);
}

static void set_color(const struct command_mode_option *opt, const char *value)
{
	int i = (int)(long)opt->data;
	int color_max = 255;
	long int color;

	BUG_ON(i < 0);
	BUG_ON(i >= 2 * NR_COLORS);

	if (str_to_int(value, &color) || color < -1 || color > color_max) {
		error_msg("color value must be -1..%d", color_max);
		return;
	}

	if (i < NR_COLORS) {
		bg_colors[i] = color;
	} else {
		i -= NR_COLORS;
		fg_colors[i] = color;
	}
	update_color(i);
}

static void get_format(const struct command_mode_option *opt, char **value)
{
	char **var = opt->data;

	*value = xstrdup(*var);
}

static void set_format(const struct command_mode_option *opt, const char *value)
{
	char **var = opt->data;

	d_print("%s=%s (old=%s)\n", opt->name, value, *var);
	if (!format_valid(value)) {
		error_msg("invalid format string");
		return;
	}
	free(*var);
	*var = xstrdup(value);

	switch (cur_view) {
	case TREE_VIEW:
	case SORTED_VIEW:
		lib_lock();
		lib.track_win->changed = 1;
		lib.sorted_win->changed = 1;
		lib_unlock();
		break;
	case PLAYLIST_VIEW:
		pl_lock();
		pl_win->changed = 1;
		pl_unlock();
		break;
	case QUEUE_VIEW:
		play_queue_lock();
		play_queue_win->changed = 1;
		play_queue_unlock();
		break;
	}

	update_titleline();
}

static void get_output_plugin(const struct command_mode_option *opt, char **value)
{
	*value = player_get_op();
	if (*value == NULL)
		*value = xstrdup("");
}

static void set_output_plugin(const struct command_mode_option *opt, const char *value)
{
	player_set_op(value);
}

#define SECOND_SIZE (44100 * 16 / 8 * 2)

static void get_buffer_seconds(const struct command_mode_option *opt, char **value)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%d", (player_get_buffer_chunks() * CHUNK_SIZE +
				SECOND_SIZE / 2) / SECOND_SIZE);
	*value = xstrdup(buf);
}

static void set_buffer_seconds(const struct command_mode_option *opt, const char *value)
{
	long int seconds;

	if (str_to_int(value, &seconds) == -1 || seconds < 1) {
		error_msg("buffer_seconds must be positive integer");
		return;
	}
	player_set_buffer_chunks((seconds * SECOND_SIZE + CHUNK_SIZE / 2) / CHUNK_SIZE);
}

static void get_status_display_program(const struct command_mode_option *opt, char **value)
{
	if (status_display_program == NULL) {
		*value = xstrdup("");
	} else {
		*value = xstrdup(status_display_program);
	}
}

static void set_status_display_program(const struct command_mode_option *opt, const char *value)
{
	free(status_display_program);
	if (strcmp(value, "") == 0) {
		status_display_program = NULL;
		return;
	}
	status_display_program = xstrdup(value);
}

char **parse_sort_keys(const char *value)
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

char *keys_to_str(char **keys)
{
	char buf[256];
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
	return xstrdup(buf);
}

static void get_lib_sort(const struct command_mode_option *opt, char **value)
{
	*value = keys_to_str(lib.sort_keys);
}

static void get_pl_sort(const struct command_mode_option *opt, char **value)
{
	*value = keys_to_str(pl_sort_keys);
}

static void set_lib_sort(const struct command_mode_option *opt, const char *value)
{
	char **keys = parse_sort_keys(value);

	if (keys)
		lib_set_sort_keys(keys);
}

static void set_pl_sort(const struct command_mode_option *opt, const char *value)
{
	char **keys = parse_sort_keys(value);

	if (keys)
		pl_set_sort_keys(keys);
}

static void get_confirm_run(const struct command_mode_option *opt, char **value)
{
	if (confirm_run) {
		*value = xstrdup("true");
	} else {
		*value = xstrdup("false");
	}
}

static void set_confirm_run(const struct command_mode_option *opt, const char *value)
{
	if (strcmp(value, "true") == 0) {
		confirm_run = 1;
	} else if (strcmp(value, "false") == 0) {
		confirm_run = 0;
	} else {
		error_msg("confirm_run must be 'true' or 'false'");
		return;
	}
}

static void get_op_option(const struct command_mode_option *opt, char **value)
{
	int rc;

	rc = player_get_op_option(opt->name, value);
	if (value == NULL)
		*value = xstrdup("");
}

static void set_op_option(const struct command_mode_option *opt, const char *value)
{
	d_print("%s=%s\n", opt->name, value);
	BUG_ON(opt->data != NULL);
	player_set_op_option(opt->name, value);
}

static void player_option_callback(void *data, const char *name)
{
	d_print("adding player option %s\n", name);
	option_add(name, get_op_option, set_op_option, data);
}

#define NR_FMTS 8
static const char *fmt_names[NR_FMTS] = {
	"altformat_current",
	"altformat_playlist",
	"altformat_title",
	"altformat_trackwin",
	"format_current",
	"format_playlist",
	"format_title",
	"format_trackwin"
};

static const char *fmt_defaults[NR_FMTS] = {
	" %F%= %d ",
	" %f%= %d ",
	"%f",
	" %f%= %d ",
	" %a - %l - %02n. %t%= %y %d ",
	" %a - %l - %02n. %t%= %y %d ",
	"%a - %l - %t (%y)",
	" %02n. %t%= %y %d "
};

static char **fmt_vars[NR_FMTS] = {
	&current_alt_format,
	&list_win_alt_format,
	&window_title_alt_format,
	&track_win_alt_format,
	&current_format,
	&list_win_format,
	&window_title_format,
	&track_win_format
};

void options_init(void)
{
	int i;

	for (i = 0; i < NR_FMTS; i++) {
		char *var;

		if (!sconf_get_str_option(fmt_names[i], &var))
			var = xstrdup(fmt_defaults[i]);
		*fmt_vars[i] = var;
		option_add(fmt_names[i], get_format, set_format, fmt_vars[i]);
	}

	for (i = 0; i < NR_COLORS; i++) {
		char buf[64];

		snprintf(buf, sizeof(buf), "color_%s_bg", color_names[i]);
		option_add(buf, get_color, set_color, (void *)(long)i);
		snprintf(buf, sizeof(buf), "color_%s_fg", color_names[i]);
		option_add(buf, get_color, set_color, (void *)(long)(i + NR_COLORS));
	}

	sconf_get_bool_option("confirm_run", &confirm_run);

	option_add("output_plugin", get_output_plugin, set_output_plugin, NULL);
	option_add("buffer_seconds", get_buffer_seconds, set_buffer_seconds, NULL);
	option_add("status_display_program", get_status_display_program,
			set_status_display_program, NULL);
	option_add("lib_sort", get_lib_sort, set_lib_sort, NULL);
	option_add("pl_sort", get_pl_sort, set_pl_sort, NULL);
	option_add("confirm_run", get_confirm_run, set_confirm_run, NULL);

	player_for_each_op_option(player_option_callback, NULL);
}

void options_exit(void)
{
	int i;

	for (i = 0; i < NR_FMTS; i++)
		sconf_set_str_option(fmt_names[i], *fmt_vars[i]);
	sconf_set_bool_option("confirm_run", confirm_run);
}
