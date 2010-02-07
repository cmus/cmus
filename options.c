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
#include "cmus.h"
#include "misc.h"
#include "lib.h"
#include "pl.h"
#include "browser.h"
#include "keys.h"
#include "filters.h"
#include "command_mode.h"
#include "file.h"
#include "prog.h"
#include "output.h"
#include "config/datadir.h"

#include <stdio.h>
#include <errno.h>

#if defined(__sun__)
#include <ncurses.h>
#else
#include <curses.h>
#endif

/* initialized option variables */

char *output_plugin = NULL;
char *status_display_program = NULL;
char *server_password;
int auto_reshuffle = 0;
int confirm_run = 1;
int show_hidden = 0;
int show_remaining_time = 0;
int set_term_title = 1;
int play_library = 1;
int repeat = 0;
int shuffle = 0;
int pretty_artist_name;
int fuzzy_artist_sort = 1;

int colors[NR_COLORS] = {
	-1,
	-1,
	COLOR_RED | BRIGHT,
	COLOR_YELLOW | BRIGHT,

	COLOR_BLUE,
	COLOR_WHITE,
	COLOR_BLACK,
	COLOR_BLUE,

	COLOR_WHITE | BRIGHT,
	-1,
	COLOR_YELLOW | BRIGHT,
	COLOR_BLUE,

	COLOR_YELLOW | BRIGHT,
	COLOR_BLUE | BRIGHT,
	-1,
	COLOR_WHITE,

	COLOR_YELLOW | BRIGHT,
	COLOR_WHITE,
	COLOR_BLACK,
	COLOR_BLUE,

	COLOR_WHITE | BRIGHT,
	COLOR_BLUE,
	COLOR_WHITE | BRIGHT
};

/* uninitialized option variables */
char *track_win_format = NULL;
char *track_win_format_va = NULL;
char *track_win_alt_format = NULL;
char *list_win_format = NULL;
char *list_win_alt_format = NULL;
char *current_format = NULL;
char *current_alt_format = NULL;
char *window_title_format = NULL;
char *window_title_alt_format = NULL;
char *id3_default_charset = NULL;

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

int parse_enum(const char *buf, int minval, int maxval, const char * const names[], int *val)
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
	FMT_TRACKWIN,
	FMT_TRACKWIN_VA,

	NR_FMTS
};

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

static void get_id3_default_charset(unsigned int id, char *buf)
{
	strcpy(buf, id3_default_charset);
}

static void set_id3_default_charset(unsigned int id, const char *buf)
{
	free(id3_default_charset);
	id3_default_charset = xstrdup(buf);
}

static const char * const valid_sort_keys[] = {
	"artist",
	"album",
	"title",
	"tracknumber",
	"discnumber",
	"date",
	"genre",
	"comment",
	"filename",
	"albumartist",
	"filemtime",
	NULL
};

static const char **parse_sort_keys(const char *value)
{
	const char **keys;
	const char *s, *e;
	int size = 4;
	int pos = 0;

	size = 4;
	keys = xnew(const char *, size);

	s = value;
	while (1) {
		char buf[32];
		int i, len;

		while (*s == ' ')
			s++;

		e = s;
		while (*e && *e != ' ')
			e++;

		len = e - s;
		if (len == 0)
			break;
		if (len > 31)
			len = 31;

		memcpy(buf, s, len);
		buf[len] = 0;
		s = e;

		for (i = 0; ; i++) {
			if (valid_sort_keys[i] == NULL) {
				error_msg("invalid sort key '%s'", buf);
				free(keys);
				return NULL;
			}

			if (strcmp(buf, valid_sort_keys[i]) == 0)
				break;
		}

		if (pos == size - 1) {
			size *= 2;
			keys = xrenew(const char *, keys, size);
		}
		keys[pos++] = valid_sort_keys[i];
	}
	keys[pos] = NULL;
	return keys;
}

static void get_lib_sort(unsigned int id, char *buf)
{
	strcpy(buf, lib_editable.sort_str);
}

static void set_lib_sort(unsigned int id, const char *buf)
{
	const char **keys = parse_sort_keys(buf);

	if (keys)
		editable_set_sort_keys(&lib_editable, keys);
}

static void get_pl_sort(unsigned int id, char *buf)
{
	strcpy(buf, pl_editable.sort_str);
}

static void set_pl_sort(unsigned int id, const char *buf)
{
	const char **keys = parse_sort_keys(buf);

	if (keys)
		editable_set_sort_keys(&pl_editable, keys);
}

static void get_output_plugin(unsigned int id, char *buf)
{
	const char *value = op_get_current();

	if (value)
		strcpy(buf, value);
}

static void set_output_plugin(unsigned int id, const char *buf)
{
	if (ui_initialized) {
		if (!soft_vol)
			mixer_close();
		player_set_op(buf);
		if (!soft_vol)
			mixer_open();
	} else {
		/* must set it later manually */
		output_plugin = xstrdup(buf);
	}
}

static void get_passwd(unsigned int id, char *buf)
{
	if (server_password)
		strcpy(buf, server_password);
}

static void set_passwd(unsigned int id, const char *buf)
{
	int len = strlen(buf);

	if (len == 0) {
		free(server_password);
		server_password = NULL;
	} else if (len < 6) {
		error_msg("unsafe password");
	} else {
		free(server_password);
		server_password = xstrdup(buf);
	}
}

static void get_replaygain_preamp(unsigned int id, char *buf)
{
	sprintf(buf, "%f", replaygain_preamp);
}

static void set_replaygain_preamp(unsigned int id, const char *buf)
{
	double val;
	char *end;

	val = strtod(buf, &end);
	if (end == buf) {
		error_msg("floating point number expected (dB)");
		return;
	}
	player_set_rg_preamp(val);
}

static void get_softvol_state(unsigned int id, char *buf)
{
	sprintf(buf, "%d %d", soft_vol_l, soft_vol_r);
}

static void set_softvol_state(unsigned int id, const char *buf)
{
	char buffer[OPTION_MAX_SIZE];
	char *ptr;
	long int l, r;

	strcpy(buffer, buf);
	ptr = strchr(buffer, ' ');
	if (!ptr)
		goto err;
	while (*ptr == ' ')
		*ptr++ = 0;

	if (str_to_int(buffer, &l) == -1 || l < 0 || l > 100)
		goto err;
	if (str_to_int(ptr, &r) == -1 || r < 0 || r > 100)
		goto err;

	player_set_soft_volume(l, r);
	return;
err:
	error_msg("two integers in range 0..100 expected");
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

static void get_auto_reshuffle(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[auto_reshuffle]);
}

static void set_auto_reshuffle(unsigned int id, const char *buf)
{
	parse_bool(buf, &auto_reshuffle);
}

static void toggle_auto_reshuffle(unsigned int id)
{
	auto_reshuffle ^= 1;
}

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

static void get_repeat_current(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[player_repeat_current]);
}

static void set_repeat_current(unsigned int id, const char *buf)
{
	if (!parse_bool(buf, &player_repeat_current))
		return;
	update_statusline();
}

static void toggle_repeat_current(unsigned int id)
{
	player_repeat_current ^= 1;
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

const char * const view_names[NR_VIEWS + 1] = {
	"tree", "sorted", "playlist", "queue", "browser", "filters", "settings", NULL
};

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
	strcpy(buf, bool_names[play_sorted]);
}

static void set_play_sorted(unsigned int id, const char *buf)
{
	int tmp;

	if (!parse_bool(buf, &tmp))
		return;

	play_sorted = tmp;
	update_statusline();
}

static void toggle_play_sorted(unsigned int id)
{
	editable_lock();
	play_sorted = play_sorted ^ 1;

	/* shuffle would override play_sorted... */
	if (play_sorted) {
		/* play_sorted makes no sense in playlist */
		play_library = 1;
		shuffle = 0;
	}

	editable_unlock();
	update_statusline();
}

static void get_fuzzy_artist_sort(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[fuzzy_artist_sort]);
}

static void set_fuzzy_artist_sort(unsigned int id, const char *buf)
{
	if (parse_bool(buf, &fuzzy_artist_sort))
		tree_sort_artists();
}

static void toggle_fuzzy_artist_sort(unsigned int id)
{
	fuzzy_artist_sort ^= 1;
	tree_sort_artists();
}

static void get_pretty_artist_name(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[pretty_artist_name]);
}

static void set_pretty_artist_name(unsigned int id, const char *buf)
{
	parse_bool(buf, &pretty_artist_name);
	lib_tree_win->changed = 1;
}

static void toggle_pretty_artist_name(unsigned int id)
{
	pretty_artist_name ^= 1;
	lib_tree_win->changed = 1;
}

const char * const aaa_mode_names[] = {
	"all", "artist", "album", NULL
};

static void get_aaa_mode(unsigned int id, char *buf)
{
	strcpy(buf, aaa_mode_names[aaa_mode]);
}

static void set_aaa_mode(unsigned int id, const char *buf)
{
	int tmp;

	if (!parse_enum(buf, 0, 2, aaa_mode_names, &tmp))
		return;

	aaa_mode = tmp;
	update_statusline();
}

static void toggle_aaa_mode(unsigned int id)
{
	editable_lock();

	/* aaa mode makes no sense in playlist */
	play_library = 1;

	aaa_mode++;
	aaa_mode %= 3;
	editable_unlock();
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

static const char * const replaygain_names[] = {
	"disabled", "track", "album", "track-preferred", "album-preferred", NULL
};

static void get_replaygain(unsigned int id, char *buf)
{
	strcpy(buf, replaygain_names[replaygain]);
}

static void set_replaygain(unsigned int id, const char *buf)
{
	int tmp;

	if (!parse_enum(buf, 0, 4, replaygain_names, &tmp))
		return;
	player_set_rg(tmp);
}

static void toggle_replaygain(unsigned int id)
{
	player_set_rg((replaygain + 1) % 5);
}

static void get_replaygain_limit(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[replaygain_limit]);
}

static void set_replaygain_limit(unsigned int id, const char *buf)
{
	int tmp;

	if (!parse_bool(buf, &tmp))
		return;
	player_set_rg_limit(tmp);
}

static void toggle_replaygain_limit(unsigned int id)
{
	player_set_rg_limit(replaygain_limit ^ 1);
}

static void get_show_hidden(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[show_hidden]);
}

static void set_show_hidden(unsigned int id, const char *buf)
{
	if (!parse_bool(buf, &show_hidden))
		return;
	browser_reload();
}

static void toggle_show_hidden(unsigned int id)
{
	show_hidden ^= 1;
	browser_reload();
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

static void get_set_term_title(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[set_term_title]);
}

static void set_set_term_title(unsigned int id, const char *buf)
{
	parse_bool(buf, &set_term_title);
}

static void toggle_set_term_title(unsigned int id)
{
	set_term_title ^= 1;
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

static void get_softvol(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[soft_vol]);
}

static void do_set_softvol(int soft)
{
	if (!soft_vol)
		mixer_close();
	player_set_soft_vol(soft);
	if (!soft_vol)
		mixer_open();
	update_statusline();
}

static void set_softvol(unsigned int id, const char *buf)
{
	int soft;

	if (!parse_bool(buf, &soft))
		return;
	do_set_softvol(soft);
}

static void toggle_softvol(unsigned int id)
{
	do_set_softvol(soft_vol ^ 1);
}

/* }}} */

/* special callbacks (id set) {{{ */

static const char * const color_enum_names[1 + 8 * 2 + 1] = {
	"default",
	"black", "red", "green", "yellow", "blue", "magenta", "cyan", "gray",
	"darkgray", "lightred", "lightgreen", "lightyellow", "lightblue", "lightmagenta", "lightcyan", "white",
	NULL
};

static void get_color(unsigned int id, char *buf)
{
	int val;

	val = colors[id];
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

	colors[id] = color;
	update_colors();
	update_full();
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
	case FMT_TRACKWIN_VA:
		return &track_win_format_va;
	default:
		die("unhandled format code: %d\n", id);
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

	update_full();
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
	DT(auto_reshuffle)
	DN(buffer_seconds)
	DT(confirm_run)
	DT(continue)
	DT(fuzzy_artist_sort)
	DN(id3_default_charset)
	DN(lib_sort)
	DN(output_plugin)
	DN(passwd)
	DN(pl_sort)
	DT(play_library)
	DT(play_sorted)
	DT(pretty_artist_name)
	DT(repeat)
	DT(repeat_current)
	DT(replaygain)
	DT(replaygain_limit)
	DN(replaygain_preamp)
	DT(show_hidden)
	DT(show_remaining_time)
	DT(set_term_title)
	DT(shuffle)
	DT(softvol)
	DN(softvol_state)
	DN(status_display_program)
	{ NULL, NULL, NULL, NULL }
};

static const char * const color_names[NR_COLORS] = {
	"color_cmdline_bg",
	"color_cmdline_fg",
	"color_error",
	"color_info",
	"color_separator",
	"color_statusline_bg",
	"color_statusline_fg",
	"color_titleline_bg",
	"color_titleline_fg",
	"color_win_bg",
	"color_win_cur",
	"color_win_cur_sel_bg",
	"color_win_cur_sel_fg",
	"color_win_dir",
	"color_win_fg",
	"color_win_inactive_cur_sel_bg",
	"color_win_inactive_cur_sel_fg",
	"color_win_inactive_sel_bg",
	"color_win_inactive_sel_fg",
	"color_win_sel_bg",
	"color_win_sel_fg",
	"color_win_title_bg",
	"color_win_title_fg"
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
	{ "format_playlist",	" %-20a %02n. %t%= %y %d " },
	{ "format_title",	"%a - %l - %t (%y)" },
	{ "format_trackwin",	" %02n. %t%= %y %d " },
	{ "format_trackwin_va",	" %02n. %t (%a)%= %y %d " },

	{ "lib_sort"	,	"artist album discnumber tracknumber title filename" },
	{ "pl_sort",		"" },
	{ "id3_default_charset","ISO-8859-1" },
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

void options_add(void)
{
	int i;

	for (i = 0; simple_options[i].name; i++)
		option_add(simple_options[i].name, 0, simple_options[i].get,
				simple_options[i].set, simple_options[i].toggle);

	for (i = 0; i < NR_FMTS; i++)
		option_add(str_defaults[i].name, i, get_format, set_format, NULL);

	for (i = 0; i < NR_COLORS; i++)
		option_add(color_names[i], i, get_color, set_color, NULL);

	op_add_options();
}

static int handle_line(void *data, const char *line)
{
	run_command(line);
	return 0;
}

int source_file(const char *filename)
{
	return file_for_each_line(filename, handle_line, NULL);
}

void options_load(void)
{
	char filename[512];
	int i;

	/* initialize those that can't be statically initialized */
	for (i = 0; str_defaults[i].name; i++)
		option_set(str_defaults[i].name, str_defaults[i].value);

	/* load autosave config */
	snprintf(filename, sizeof(filename), "%s/autosave", cmus_config_dir);
	if (source_file(filename) == -1) {
		const char *def = DATADIR "/cmus/rc";

		if (errno != ENOENT)
			error_msg("loading %s: %s", filename, strerror(errno));

		/* load defaults */
		if (source_file(def) == -1)
			die_errno("loading %s", def);
	}

	/* load optional static config */
	snprintf(filename, sizeof(filename), "%s/rc", cmus_config_dir);
	if (source_file(filename) == -1) {
		if (errno != ENOENT)
			error_msg("loading %s: %s", filename, strerror(errno));
	}
}

void options_exit(void)
{
	struct cmus_opt *opt;
	struct filter_entry *filt;
	char filename[512];
	FILE *f;
	int i;

	snprintf(filename, sizeof(filename), "%s/autosave", cmus_config_dir);
	f = fopen(filename, "w");
	if (f == NULL) {
		warn_errno("creating %s", filename);
		return;
	}

	/* save options */
	list_for_each_entry(opt, &option_head, node) {
		char buf[OPTION_MAX_SIZE];

		buf[0] = 0;
		opt->get(opt->id, buf);
		fprintf(f, "set %s=%s\n", opt->name, buf);
	}

	/* save key bindings */
	for (i = 0; i < NR_CTXS; i++) {
		struct binding *b = key_bindings[i];

		while (b) {
			fprintf(f, "bind %s %s %s\n", key_context_names[i], b->key->name, b->cmd);
			b = b->next;
		}
	}

	/* save filters */
	list_for_each_entry(filt, &filters_head, node)
		fprintf(f, "fset %s=%s\n", filt->name, filt->filter);
	fprintf(f, "factivate");
	list_for_each_entry(filt, &filters_head, node) {
		switch (filt->act_stat) {
		case FS_YES:
			fprintf(f, " %s", filt->name);
			break;
		case FS_NO:
			fprintf(f, " !%s", filt->name);
			break;
		}
	}
	fprintf(f, "\n");

	fclose(f);
}
