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

#include <ui_curses.h>
#include <cmdline.h>
#include <search_mode.h>
#include <command_mode.h>
#include <options.h>
#include <play_queue.h>
#include <browser.h>
#include <filters.h>
#include <cmus.h>
#include <player.h>
#include <utils.h>
#include <lib.h>
#include <pl.h>
#include <xmalloc.h>
#include <xstrjoin.h>
#include <window.h>
#include <format_print.h>
#include <sconf.h>
#include <misc.h>
#include <prog.h>
#include <uchar.h>
#include <spawn.h>
#include <file.h>
#include <server.h>
#include <keys.h>
#include <debug.h>
#include <config.h>

#if defined(CONFIG_IRMAN)
#include <irman.h>
#include <irman_config.h>
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <curses.h>
#include <ctype.h>
#include <dirent.h>
#include <locale.h>
#include <langinfo.h>
#include <iconv.h>
#include <signal.h>
#include <stdarg.h>

/* globals. documented in ui_curses.h */

int ui_initialized = 0;
enum ui_input_mode input_mode = NORMAL_MODE;
int cur_view = TREE_VIEW;
struct searchable *searchable;

char *lib_autosave_filename;
char *pl_autosave_filename;
char *lib_filename = NULL;
char *pl_filename = NULL;

char *track_win_format = NULL;
char *track_win_alt_format = NULL;
char *list_win_format = NULL;
char *list_win_alt_format = NULL;
char *current_format = NULL;
char *current_alt_format = NULL;
char *window_title_format = NULL;
char *window_title_alt_format = NULL;
char *status_display_program = NULL;

#define BRIGHT (1 << 3)

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
	COLOR_RED,
	-1,
	COLOR_WHITE,
	COLOR_RED,
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

	COLOR_RED,
	COLOR_WHITE | BRIGHT,
	-1,
	COLOR_BLACK,
	COLOR_WHITE | BRIGHT,
	COLOR_BLUE | BRIGHT,
	-1,
	COLOR_RED | BRIGHT,
	COLOR_YELLOW | BRIGHT
};

/* prefixes actually. "_bg" or "_fg" added at end */
const char * const color_names[NR_COLORS] = {
	"row",
	"row_cur",
	"row_sel",
	"row_sel_cur",
	"row_active",
	"row_active_cur",
	"row_active_sel",
	"row_active_sel_cur",
	"separator",
	"title",
	"commandline",
	"statusline",
	"titleline",
	"browser_dir",
	"browser_file",
	"error",
	"info",
};

/* ------------------------------------------------------------------------- */

/* currently playing file */
static struct track_info *cur_track_info;

static int show_remaining_time = 0;
static int update_window_title = 0;

static int running = 1;

/* shown error message and time stamp
 * error is cleared if it is older than 3s and key was pressed
 */
static char error_buf[512];
static time_t error_time = 0;

static char *server_address = NULL;
static int remote_socket = -1;

static char *charset = NULL;
static char print_buffer[512];

/* destination buffer for utf8_encode and utf8_decode */
static char conv_buffer[512];

#define print_buffer_size (sizeof(print_buffer) - 1)
static int using_utf8;

static int tree_win_x = 0;
static int tree_win_y = 0;
static int tree_win_w = 0;

static int track_win_x = 0;
static int track_win_y = 0;
static int track_win_w = 0;

/* colors in curses format */
static int cursed_colors[NR_COLORS];

enum {
	TF_ARTIST,
	TF_ALBUM,
	TF_DISC,
	TF_TRACK,
	TF_TITLE,
	TF_YEAR,
	TF_GENRE,
	TF_DURATION,
	TF_PATHFILE,
	TF_FILE,
	NR_TFS
};

static struct format_option track_fopts[NR_TFS + 1] = {
	DEF_FO_STR('a'),
	DEF_FO_STR('l'),
	DEF_FO_INT('D'),
	DEF_FO_INT('n'),
	DEF_FO_STR('t'),
	DEF_FO_STR('y'),
	DEF_FO_STR('g'),
	DEF_FO_TIME('d'),
	DEF_FO_STR('f'),
	DEF_FO_STR('F'),
	DEF_FO_END
};

enum {
	SF_STATUS,
	SF_POSITION,
	SF_DURATION,
	SF_TOTAL,
	SF_VOLUME,
	SF_LVOLUME,
	SF_RVOLUME,
	SF_BUFFER,
	SF_REPEAT,
	SF_CONTINUE,
	SF_SHUFFLE,
	SF_PLAYLISTMODE,
	NR_SFS
};

static struct format_option status_fopts[NR_SFS + 1] = {
	DEF_FO_STR('s'),
	DEF_FO_TIME('p'),
	DEF_FO_TIME('d'),
	DEF_FO_TIME('t'),
	DEF_FO_INT('v'),
	DEF_FO_INT('l'),
	DEF_FO_INT('r'),
	DEF_FO_INT('b'),
	DEF_FO_STR('R'),
	DEF_FO_STR('C'),
	DEF_FO_STR('S'),
	DEF_FO_STR('L'),
	DEF_FO_END
};

static void utf8_encode(const char *buffer)
{
	static iconv_t cd = (iconv_t)-1;
	size_t is, os;
	const char *i;
	char *o;
	int rc;

	if (cd == (iconv_t)-1) {
		d_print("iconv_open(UTF-8, %s)\n", charset);
		cd = iconv_open("UTF-8", charset);
		if (cd == (iconv_t)-1) {
			d_print("iconv_open failed: %s\n", strerror(errno));
			return;
		}
	}
	i = buffer;
	o = conv_buffer;
	is = strlen(i);
	os = sizeof(conv_buffer) - 1;
	rc = iconv(cd, (void *)&i, &is, &o, &os);
	*o = 0;
	if (rc == -1) {
		d_print("iconv failed: %s\n", strerror(errno));
		return;
	}
}

static void utf8_decode(const char *buffer)
{
	static iconv_t cd = (iconv_t)-1;
	size_t is, os;
	const char *i;
	char *o;
	int rc;

	if (cd == (iconv_t)-1) {
		d_print("iconv_open(%s, UTF-8)\n", charset);
		cd = iconv_open(charset, "UTF-8");
		if (cd == (iconv_t)-1) {
			d_print("iconv_open failed: %s\n", strerror(errno));
			return;
		}
	}
	i = buffer;
	o = conv_buffer;
	is = strlen(i);
	os = sizeof(conv_buffer) - 1;
	rc = iconv(cd, (void *)&i, &is, &o, &os);
	*o = 0;
	if (rc == -1) {
		d_print("iconv failed: %s\n", strerror(errno));
		return;
	}
}

/* screen updates {{{ */

static void dump_print_buffer(int row, int col)
{
	if (using_utf8) {
		mvaddstr(row, col, print_buffer);
	} else {
		utf8_decode(print_buffer);
		mvaddstr(row, col, conv_buffer);
	}
}

static void sprint(int row, int col, const char *str, int width, int indent)
{
	int s, d, ellipsis_pos = 0, cut_double_width = 0;

	width -= 2 + indent;
	d = indent + 1;
	memset(print_buffer, ' ', d);
	s = 0;
	while (1) {
		uchar u;
		int w;

		if (width == 3)
			ellipsis_pos = d;

		u_get_char(str, &s, &u);
		if (u == 0) {
			memset(print_buffer + d, ' ', width);
			d += width;
			break;
		}

		w = u_char_width(u);
		if (width == 3)
			ellipsis_pos = d;
		if (width == 4 && w == 2) {
			/* can't cut double-width char */
			ellipsis_pos = d + 1;
			cut_double_width = 1;
		}

		width -= w;
		if (width < 0) {
			/* does not fit */
			d = ellipsis_pos;
			if (cut_double_width) {
				/* first half of the double-width char */
				print_buffer[d - 1] = ' ';
			}
			print_buffer[d++] = '.';
			print_buffer[d++] = '.';
			print_buffer[d++] = '.';
			break;
		}
		u_set_char(print_buffer, &d, u);
	}
	print_buffer[d++] = ' ';
	print_buffer[d++] = 0;
	dump_print_buffer(row, col);
}

static void sprint_ascii(int row, int col, const char *str, int len)
{
	int l;

	l = strlen(str);
	len -= 2;

	print_buffer[0] = ' ';
	if (l > len) {
		memcpy(print_buffer + 1, str, len - 3);
		print_buffer[len - 2] = '.';
		print_buffer[len - 1] = '.';
		print_buffer[len - 0] = '.';
	} else {
		memcpy(print_buffer + 1, str, l);
		memset(print_buffer + 1 + l, ' ', len - l);
	}
	print_buffer[len + 1] = ' ';
	print_buffer[len + 2] = 0;
	mvaddstr(row, col, print_buffer);
}

static void print_tree(struct window *win, int row, struct iter *iter)
{
	const char *noname = "<no name>";
	struct artist *artist;
	struct album *album;
	struct iter sel;
	int current, selected, active;

	artist = iter_to_artist(iter);
	album = iter_to_album(iter);
	if (album) {
		current = lib.cur_album == album;
	} else {
		current = lib.cur_artist == artist;
	}
	window_get_sel(win, &sel);
	selected = iters_equal(iter, &sel);
	active = lib.cur_win == lib.tree_win;
	bkgdset(cursed_colors[(active << 2) | (selected << 1) | current]);
	if (album) {
		sprint(tree_win_y + row + 1, tree_win_x, album->name ? : noname, tree_win_w, 2);
	} else {
		sprint(tree_win_y + row + 1, tree_win_x, artist->name ? : noname, tree_win_w, 0);
	}
}

static inline void fopt_set_str(struct format_option *fopt, const char *str)
{
	BUG_ON(fopt->type != FO_STR);
	if (str) {
		fopt->fo_str = str;
		fopt->empty = 0;
	} else {
		fopt->empty = 1;
	}
}

static inline void fopt_set_int(struct format_option *fopt, int value, int empty)
{
	BUG_ON(fopt->type != FO_INT);
	fopt->fo_int = value;
	fopt->empty = empty;
}

static inline void fopt_set_time(struct format_option *fopt, int value, int empty)
{
	BUG_ON(fopt->type != FO_TIME);
	fopt->fo_time = value;
	fopt->empty = empty;
}

static void fill_track_fopts(struct tree_track *track)
{
	const char *filename;
	const struct track_info *ti = tree_track_info(track);
	int num, disc;

	if (using_utf8) {
		filename = ti->filename;
	} else {
		utf8_encode(ti->filename);
		filename = conv_buffer;
	}
	disc = track->shuffle_track.simple_track.disc;
	num = track->shuffle_track.simple_track.num;

	fopt_set_str(&track_fopts[TF_ARTIST], track->album->artist->name);
	fopt_set_str(&track_fopts[TF_ALBUM], track->album->name);
	fopt_set_int(&track_fopts[TF_DISC], disc, disc == -1);
	fopt_set_int(&track_fopts[TF_TRACK], num, num == -1);
	fopt_set_str(&track_fopts[TF_TITLE], comments_get_val(ti->comments, "title"));
	fopt_set_str(&track_fopts[TF_YEAR], comments_get_val(ti->comments, "date"));
	fopt_set_str(&track_fopts[TF_GENRE], comments_get_val(ti->comments, "genre"));
	fopt_set_time(&track_fopts[TF_DURATION], ti->duration, ti->duration == -1);
	fopt_set_str(&track_fopts[TF_PATHFILE], filename);
	if (is_url(ti->filename)) {
		fopt_set_str(&track_fopts[TF_FILE], filename);
	} else {
		const char *f;

		f = strrchr(filename, '/');
		if (f) {
			fopt_set_str(&track_fopts[TF_FILE], f + 1);
		} else {
			fopt_set_str(&track_fopts[TF_FILE], filename);
		}
	}
}

static void fill_track_fopts_track_info(struct track_info *info)
{
	char *filename;
	int num, disc;

	if (using_utf8) {
		filename = info->filename;
	} else {
		utf8_encode(info->filename);
		filename = conv_buffer;
	}
	disc = comments_get_int(info->comments, "discnumber");
	num = comments_get_int(info->comments, "tracknumber");

	fopt_set_str(&track_fopts[TF_ARTIST], comments_get_val(info->comments, "artist"));
	fopt_set_str(&track_fopts[TF_ALBUM], comments_get_val(info->comments, "album"));
	fopt_set_int(&track_fopts[TF_DISC], disc, disc == -1);
	fopt_set_int(&track_fopts[TF_TRACK], num, num == -1);
	fopt_set_str(&track_fopts[TF_TITLE], comments_get_val(info->comments, "title"));
	fopt_set_str(&track_fopts[TF_YEAR], comments_get_val(info->comments, "date"));
	fopt_set_str(&track_fopts[TF_GENRE], comments_get_val(info->comments, "genre"));
	fopt_set_time(&track_fopts[TF_DURATION], info->duration, info->duration == -1);
	fopt_set_str(&track_fopts[TF_PATHFILE], filename);
	if (is_url(info->filename)) {
		fopt_set_str(&track_fopts[TF_FILE], filename);
	} else {
		const char *f;

		f = strrchr(filename, '/');
		if (f) {
			fopt_set_str(&track_fopts[TF_FILE], f + 1);
		} else {
			fopt_set_str(&track_fopts[TF_FILE], filename);
		}
	}
}

static void print_track(struct window *win, int row, struct iter *iter)
{
	struct tree_track *track;
	struct iter sel;
	int current, selected, active;

	track = iter_to_tree_track(iter);
	current = lib.cur_track == track;
	window_get_sel(win, &sel);
	selected = iters_equal(iter, &sel);
	active = lib.cur_win == lib.track_win;
	bkgdset(cursed_colors[(active << 2) | (selected << 1) | current]);

	fill_track_fopts(track);

	if (track_info_has_tag(tree_track_info(track))) {
		format_print(print_buffer, track_win_w, track_win_format, track_fopts);
	} else {
		format_print(print_buffer, track_win_w, track_win_alt_format, track_fopts);
	}
	dump_print_buffer(track_win_y + row + 1, track_win_x);
}

static void print_sorted(struct window *win, int row, struct iter *iter)
{
	struct tree_track *track;
	struct iter sel;
	int current, selected, active = 1;

	track = iter_to_sorted_track(iter);
	current = lib.cur_track == track;
	window_get_sel(win, &sel);
	selected = iters_equal(iter, &sel);
	bkgdset(cursed_colors[(active << 2) | (selected << 1) | current]);

	fill_track_fopts(track);

	if (track_info_has_tag(tree_track_info(track))) {
		format_print(print_buffer, COLS, list_win_format, track_fopts);
	} else {
		format_print(print_buffer, COLS, list_win_alt_format, track_fopts);
	}
	dump_print_buffer(row + 1, 0);
}

static void print_pl(struct window *win, int row, struct iter *iter)
{
	struct simple_track *track;
	struct iter sel;
	int current, selected, active;

	track = iter_to_simple_track(iter);
	current = pl_cur_track == track;
	window_get_sel(win, &sel);
	selected = iters_equal(iter, &sel);

	active = 1;
	if (!selected && track->marked) {
		selected = 1;
		active = 0;
	}

	bkgdset(cursed_colors[(active << 2) | (selected << 1) | current]);

	fill_track_fopts_track_info(track->info);

	if (track_info_has_tag(track->info)) {
		format_print(print_buffer, COLS, list_win_format, track_fopts);
	} else {
		format_print(print_buffer, COLS, list_win_alt_format, track_fopts);
	}
	dump_print_buffer(row + 1, 0);
}

static void print_play_queue(struct window *win, int row, struct iter *iter)
{
	struct simple_track *track;
	struct iter sel;
	int current, selected, active;

	track = iter_to_simple_track(iter);
	current = 0;
	window_get_sel(win, &sel);
	selected = iters_equal(iter, &sel);

	active = 1;
	if (!selected && track->marked) {
		selected = 1;
		active = 0;
	}

	bkgdset(cursed_colors[(active << 2) | (selected << 1) | current]);

	fill_track_fopts_track_info(track->info);

	if (track_info_has_tag(track->info)) {
		format_print(print_buffer, COLS, list_win_format, track_fopts);
	} else {
		format_print(print_buffer, COLS, list_win_alt_format, track_fopts);
	}
	dump_print_buffer(row + 1, 0);
}

static void print_browser(struct window *win, int row, struct iter *iter)
{
	struct browser_entry *e;
	struct iter sel;
	int selected;

	e = iter_to_browser_entry(iter);
	window_get_sel(win, &sel);
	selected = iters_equal(iter, &sel);
	if (selected) {
		int active = 1;
		int current = 0;

		bkgdset(cursed_colors[(active << 2) | (selected << 1) | current]);
	} else {
		if (e->type == BROWSER_ENTRY_DIR) {
			bkgdset(cursed_colors[COLOR_BROWSER_DIR]);
		} else {
			bkgdset(cursed_colors[COLOR_BROWSER_FILE]);
		}
	}

	/* file name encoding == terminal encoding. no need to convert */
	if (using_utf8) {
		sprint(row + 1, 0, e->name, COLS, 0);
	} else {
		sprint_ascii(row + 1, 0, e->name, COLS);
	}
}

static void print_filter(struct window *win, int row, struct iter *iter)
{
	char buf[256];
	struct filter_entry *e = iter_to_filter_entry(iter);
	struct iter sel;
	/* window active? */
	int active = 1;
	/* row selected? */
	int selected;
	/* is the filter currently active? */
	int current = e->active;

	window_get_sel(win, &sel);
	selected = iters_equal(iter, &sel);
	bkgdset(cursed_colors[(active << 2) | (selected << 1) | current]);

	snprintf(buf, sizeof(buf), "%c %-15s  %s", e->selected ? '*' : ' ', e->name, e->filter);
	sprint(row + 1, 0, buf, COLS, 0);
}

static void update_window(struct window *win, int x, int y, int w, const char *title,
		void (*print)(struct window *, int, struct iter *))
{
	struct iter iter;
	int nr_rows;
	int c, i;

	win->changed = 0;

	bkgdset(cursed_colors[COLOR_TITLE]);
	c = snprintf(print_buffer, w + 1, " %s", title);
	if (c > w)
		c = w;
	memset(print_buffer + c, ' ', w - c + 1);
	print_buffer[w] = 0;
	dump_print_buffer(y, x);
	nr_rows = window_get_nr_rows(win);
	i = 0;
	if (window_get_top(win, &iter)) {
		while (i < nr_rows) {
			print(win, i, &iter);
			i++;
			if (!window_get_next(win, &iter))
				break;
		}
	}

	bkgdset(cursed_colors[0]);
	memset(print_buffer, ' ', w);
	print_buffer[w] = 0;
	while (i < nr_rows) {
		dump_print_buffer(y + i + 1, x);
		i++;
	}
}

static void update_tree_window(void)
{
	update_window(lib.tree_win, tree_win_x, tree_win_y,
			tree_win_w, "Artist / Album", print_tree);
}

static void update_track_window(void)
{
	char title[512];

	format_print(title, track_win_w - 2, "Track%=Library", track_fopts);
	update_window(lib.track_win, track_win_x, track_win_y,
			track_win_w, title, print_track);
}

static const char * const sorted_names[2] = { "not sorted", "sorted" };

static void update_sorted_window(void)
{
	char title[512];
	char *filename;

	filename = lib_filename ? lib_filename : lib_autosave_filename;
	if (using_utf8) {
		/* already UTF-8 */
	} else {
		utf8_encode(filename);
		filename = conv_buffer;
	}
	snprintf(title, sizeof(title), "Library (%s) - %s",
			sorted_names[lib.sort_keys[0] != NULL], filename);
	update_window(lib.sorted_win, 0, 0, COLS, title, print_sorted);
}

static void update_pl_window(void)
{
	char title[512];
	char *filename;

	filename = pl_filename ? pl_filename : pl_autosave_filename;
	if (using_utf8) {
		/* already UTF-8 */
	} else {
		utf8_encode(filename);
		filename = conv_buffer;
	}
	snprintf(title, sizeof(title), "Playlist (%s) - %s",
			sorted_names[pl_sort_keys[0] != NULL], filename);
	update_window(pl_win, 0, 0, COLS, title, print_pl);
}

static void update_play_queue_window(void)
{
	update_window(play_queue_win, 0, 0, COLS, "Play Queue", print_play_queue);
}

static void update_browser_window(void)
{
	char title[512];
	char *dirname;

	if (using_utf8) {
		/* already UTF-8 */
		dirname = browser_dir;
	} else {
		utf8_encode(browser_dir);
		dirname = conv_buffer;
	}
	snprintf(title, sizeof(title), "Browser - %s", dirname);
	update_window(browser_win, 0, 0, COLS, title, print_browser);
}

static void update_filters_window(void)
{
	update_window(filters_win, 0, 0, COLS, "Library Filters", print_filter);
}

static void draw_separator(void)
{
	int row;

	bkgdset(cursed_colors[COLOR_TITLE]);
	mvaddch(0, tree_win_w, ' ');
	bkgdset(cursed_colors[COLOR_SEPARATOR]);
	for (row = 1; row < LINES - 3; row++)
		mvaddch(row, tree_win_w, ACS_VLINE);
}

static void update_view(void)
{
	switch (cur_view) {
	case TREE_VIEW:
		lib_lock();
		if (lib.tree_win->changed)
			update_tree_window();
		if (lib.track_win->changed)
			update_track_window();
		lib_unlock();
		draw_separator();
		break;
	case SORTED_VIEW:
		lib_lock();
		update_sorted_window();
		lib_unlock();
		break;
	case PLAYLIST_VIEW:
		pl_lock();
		update_pl_window();
		pl_unlock();
		break;
	case QUEUE_VIEW:
		play_queue_lock();
		update_play_queue_window();
		play_queue_unlock();
		break;
	case BROWSER_VIEW:
		update_browser_window();
		break;
	case FILTERS_VIEW:
		update_filters_window();
		break;
	}
}

static void do_update_statusline(void)
{
	static char *status_strs[] = { ".", ">", "|" };
	static char *playlist_mode_strs[] = {
		"all", "artist", "album"
	};
	static char *cont_strs[] = { " ", "C" };
	static char *repeat_strs[] = { " ", "R" };
	static char *shuffle_strs[] = { " ", "S" };
	int play_sorted, volume, buffer_fill;
	int duration = -1;
	char *msg;
	char format[80];

	lib_lock();
	lib.status_changed = 0;
	fopt_set_time(&status_fopts[SF_TOTAL], play_library ? lib.total_time : pl_total_time, 0);
	fopt_set_str(&status_fopts[SF_REPEAT], repeat_strs[repeat]);
	fopt_set_str(&status_fopts[SF_SHUFFLE], shuffle_strs[shuffle]);
	fopt_set_str(&status_fopts[SF_PLAYLISTMODE], playlist_mode_strs[lib.playlist_mode]);
	play_sorted = lib.play_sorted;
	lib_unlock();

	if (cur_track_info)
		duration = cur_track_info->duration;

	player_info_lock();

	volume = (player_info.vol_left + player_info.vol_right) / 2.0 + 0.5;
	buffer_fill = (double)player_info.buffer_fill / (double)player_info.buffer_size * 100.0 + 0.5;

	fopt_set_str(&status_fopts[SF_STATUS], status_strs[player_info.status]);

	if (show_remaining_time && duration != -1) {
		fopt_set_time(&status_fopts[SF_POSITION], player_info.pos - duration, 0);
	} else {
		fopt_set_time(&status_fopts[SF_POSITION], player_info.pos, 0);
	}

	fopt_set_time(&status_fopts[SF_DURATION], duration, 0);
	fopt_set_int(&status_fopts[SF_VOLUME], volume, 0);
	fopt_set_int(&status_fopts[SF_LVOLUME], player_info.vol_left, 0);
	fopt_set_int(&status_fopts[SF_RVOLUME], player_info.vol_right, 0);
	fopt_set_int(&status_fopts[SF_BUFFER], buffer_fill, 0);
	fopt_set_str(&status_fopts[SF_CONTINUE], cont_strs[player_info.cont]);

	strcpy(format, " %s %p ");
	if (duration != -1)
		strcat(format, "/ %d ");
	if (player_info.vol_left != player_info.vol_right) {
		strcat(format, "- %t vol: %l,%r ");
	} else {
		strcat(format, "- %t vol: %v ");
	}
	if (cur_track_info && is_url(cur_track_info->filename))
		strcat(format, "buf: %b ");
	strcat(format, "%=");
	if (play_library) {
		/* artist/album modes work only in lib */
		if (shuffle) {
			/* shuffle overrides sorted mode */
			strcat(format, "%L from library");
		} else if (play_sorted) {
			strcat(format, "%L from sorted library");
		} else {
			strcat(format, "%L from library");
		}
	} else {
		strcat(format, "playlist");
	}
	strcat(format, " | %1C%1R%1S ");
	format_print(print_buffer, COLS, format, status_fopts);

	msg = player_info.error_msg;
	player_info.error_msg = NULL;

	player_info_unlock();

	bkgdset(cursed_colors[COLOR_STATUSLINE]);
	dump_print_buffer(LINES - 2, 0);

	if (msg) {
		error_msg("%s", msg);
		free(msg);
	}
}

static void dump_buffer(const char *buffer)
{
	if (using_utf8) {
		addstr(buffer);
	} else {
		utf8_decode(buffer);
		addstr(conv_buffer);
	}
}

static void do_update_commandline(void)
{
	int w, idx;
	char ch;

	move(LINES - 1, 0);
	if (error_buf[0]) {
		bkgdset(cursed_colors[COLOR_ERROR]);
		addstr(error_buf);
		clrtoeol();
		return;
	}
	bkgdset(cursed_colors[COLOR_COMMANDLINE]);
	if (input_mode == NORMAL_MODE) {
		clrtoeol();
		return;
	}

	/* COMMAND_MODE or SEARCH_MODE */
	w = u_str_width(cmdline.line);
	ch = ':';
	if (input_mode == SEARCH_MODE)
		ch = search_direction == SEARCH_FORWARD ? '/' : '?';

	if (w <= COLS - 2) {
		addch(ch);
		idx = u_copy_chars(print_buffer, cmdline.line, &w);
		print_buffer[idx] = 0;
		dump_buffer(print_buffer);
		clrtoeol();
	} else {
		/* keep cursor as far right as possible */
		int skip, width, cw;

		/* cursor pos (width, not chars. doesn't count the ':') */
		cw = u_str_nwidth(cmdline.line, cmdline.cpos);

		skip = cw + 2 - COLS;
		if (skip > 0) {
			/* skip the ':' */
			skip--;

			/* skip rest (if any) */
			idx = u_skip_chars(cmdline.line, &skip);

			width = COLS;
			idx = u_copy_chars(print_buffer, cmdline.line + idx, &width);
			while (width < COLS) {
				/* cursor is at end of the buffer
				 * print a space (or 2 if the last skipped character
				 * was double-width)
				 */
				print_buffer[idx++] = ' ';
				width++;
			}
			print_buffer[idx] = 0;
			dump_buffer(print_buffer);
		} else {
			/* print ':' + COLS - 1 chars */
			addch(ch);
			width = COLS - 1;
			idx = u_copy_chars(print_buffer, cmdline.line, &width);
			print_buffer[idx] = 0;
			dump_buffer(print_buffer);
		}
	}
}

/* lock player_info! */
static const char *get_stream_title(const char *metadata)
{
	static char stream_title[255 * 16 + 1];
	char *ptr, *title;

	ptr = strstr(player_info.metadata, "StreamTitle='");
	if (ptr == NULL)
		return NULL;
	ptr += 13;
	title = ptr;
	while (*ptr) {
		if (*ptr == '\'' && *(ptr + 1) == ';') {
			memcpy(stream_title, title, ptr - title);
			stream_title[ptr - title] = 0;
			return stream_title;
		}
		ptr++;
	}
	return NULL;
}

static void do_update_titleline(void)
{
	bkgdset(cursed_colors[COLOR_TITLELINE]);
	player_info_lock();
	if (cur_track_info) {
		const char *filename;
		int use_alt_format = 0;
		struct keyval *cur_comments = cur_track_info->comments;

		if (cur_comments[0].key == NULL) {
			const char *title = get_stream_title(player_info.metadata);

			if (title == NULL)
				use_alt_format = 1;
			fopt_set_str(&track_fopts[TF_ARTIST], NULL);
			fopt_set_str(&track_fopts[TF_ALBUM], NULL);
			fopt_set_int(&track_fopts[TF_DISC], -1, 1);
			fopt_set_int(&track_fopts[TF_TRACK], -1, 1);
			fopt_set_str(&track_fopts[TF_TITLE], title);
			fopt_set_str(&track_fopts[TF_YEAR], NULL);
		} else {
			int disc_num, track_num;

			disc_num = comments_get_int(cur_comments, "discnumber");
			track_num = comments_get_int(cur_comments, "tracknumber");
			fopt_set_str(&track_fopts[TF_ARTIST], comments_get_val(cur_comments, "artist"));
			fopt_set_str(&track_fopts[TF_ALBUM], comments_get_val(cur_comments, "album"));
			fopt_set_int(&track_fopts[TF_DISC], disc_num, disc_num == -1);
			fopt_set_int(&track_fopts[TF_TRACK], track_num, track_num == -1);
			fopt_set_str(&track_fopts[TF_TITLE], comments_get_val(cur_comments, "title"));
			fopt_set_str(&track_fopts[TF_YEAR], comments_get_val(cur_comments, "date"));
		}
		fopt_set_time(&track_fopts[TF_DURATION],
				cur_track_info->duration,
				cur_track_info->duration == -1);
		fopt_set_str(&track_fopts[TF_PATHFILE], cur_track_info->filename);
		if (is_url(cur_track_info->filename)) {
			fopt_set_str(&track_fopts[TF_FILE], cur_track_info->filename);
		} else {
			filename = strrchr(cur_track_info->filename, '/');
			if (filename) {
				fopt_set_str(&track_fopts[TF_FILE], filename + 1);
			} else {
				fopt_set_str(&track_fopts[TF_FILE], cur_track_info->filename);
			}
		}
		if (use_alt_format) {
			format_print(print_buffer, COLS, current_alt_format, track_fopts);
		} else {
			format_print(print_buffer, COLS, current_format, track_fopts);
		}
		dump_print_buffer(LINES - 3, 0);

		if (update_window_title) {
			char *wtitle;
			int i;

			if (use_alt_format) {
				format_print(print_buffer, sizeof(print_buffer) - 1,
						window_title_alt_format, track_fopts);
			} else {
				format_print(print_buffer, sizeof(print_buffer) - 1,
						window_title_format, track_fopts);
			}

			/* remove whitespace */
			i = sizeof(print_buffer) - 2;
			while (i > 0 && print_buffer[i] == ' ')
				i--;
			print_buffer[i + 1] = 0;

			if (using_utf8) {
				wtitle = print_buffer;
			} else {
				utf8_decode(print_buffer);
				wtitle = conv_buffer;
			}

			printf("\033]0;%s\007", wtitle);
			fflush(stdout);
		}
	} else {
		move(LINES - 3, 0);
		clrtoeol();

		if (update_window_title) {
			printf("\033]0;CMus " VERSION "\007");
			fflush(stdout);
		}
	}
	player_info_unlock();
}

static int cmdline_cursor_column(void)
{
	int cw, skip, s;

	/* width of the text in the buffer before cursor */
	cw = u_str_nwidth(cmdline.line, cmdline.cpos);

	if (1 + cw < COLS) {
		/* whole line is visible */
		return 1 + cw;
	}

	/* beginning of cmdline is not visible */

	/* check if the first visible char in cmdline would be halved
	 * double-width character (or invalid byte <xx>) which is not possible.
	 * we need to skip the whole character and move cursor to COLS - 2
	 * column. */
	skip = cw + 2 - COLS;

	/* skip the ':' */
	skip--;

	/* skip rest */
	s = skip;
	u_skip_chars(cmdline.line, &s);
	if (s > skip) {
		/* the last skipped char was double-width or <xx> */
		return COLS - 1 - (s - skip);
	}
	return COLS - 1;
}

static void post_update(void)
{
	/* refresh makes cursor visible at least for urxvt */
	if (input_mode == COMMAND_MODE || input_mode == SEARCH_MODE) {
		move(LINES - 1, cmdline_cursor_column());
		refresh();
		curs_set(1);
	} else {
		move(LINES - 1, 0);
		refresh();
		curs_set(0);
	}
}

void update_titleline(void)
{
	curs_set(0);
	do_update_titleline();
	post_update();
}

static void update_commandline(void)
{
	curs_set(0);
	do_update_commandline();
	post_update();
}

void update_statusline(void)
{
	curs_set(0);
	do_update_statusline();
	post_update();
}

void info_msg(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vsnprintf(error_buf, sizeof(error_buf), format, ap);
	va_end(ap);

	update_commandline();
}

void error_msg(const char *format, ...)
{
	va_list ap;

	strcpy(error_buf, "Error: ");
	va_start(ap, format);
	vsnprintf(error_buf + 7, sizeof(error_buf) - 7, format, ap);
	va_end(ap);

	if (ui_initialized) {
		error_time = time(NULL);
		update_commandline();
	} else {
		warn("%s\n", error_buf);
		error_buf[0] = 0;
	}
}

int yes_no_query(const char *format, ...)
{
	char buffer[512];
	va_list ap;
	int ret = 0;

	va_start(ap, format);
	vsnprintf(buffer, sizeof(buffer), format, ap);
	va_end(ap);

	move(LINES - 1, 0);
	bkgdset(cursed_colors[COLOR_INFO]);

	/* no need to convert buffer.
	 * it is always encoded in the right charset (assuming filenames are
	 * encoded in same charset as LC_CTYPE).
	 */

	addstr(buffer);
	clrtoeol();
	refresh();

	while (1) {
		int ch = getch();

		if (ch == ERR || ch == 0)
			continue;
		if (ch == 'y')
			ret = 1;
		break;
	}
	update_commandline();
	return ret;
}

void search_not_found(void)
{
	const char *what = "Track";

	if (search_restricted) {
		switch (cur_view) {
		case TREE_VIEW:
			what = "Artist/album";
			break;
		case SORTED_VIEW:
		case PLAYLIST_VIEW:
		case QUEUE_VIEW:
			what = "Title";
			break;
		case BROWSER_VIEW:
			what = "File/Directory";
			break;
		case FILTERS_VIEW:
			what = "Filter";
			break;
		}
	} else {
		switch (cur_view) {
		case TREE_VIEW:
		case SORTED_VIEW:
		case PLAYLIST_VIEW:
		case QUEUE_VIEW:
			what = "Track";
			break;
		case BROWSER_VIEW:
			what = "File/Directory";
			break;
		case FILTERS_VIEW:
			what = "Filter";
			break;
		}
	}
	info_msg("%s not found: %s", what, search_str ? : "");
}

static void set_view(int view)
{
	if (view == cur_view)
		return;
	cur_view = view;

	lib_lock();
	if (view < 2)
		__lib_set_view(view);
	switch (cur_view) {
	case TREE_VIEW:
		searchable = tree_searchable;
		update_tree_window();
		update_track_window();
		draw_separator();
		break;
	case SORTED_VIEW:
		searchable = sorted_searchable;
		update_sorted_window();
		break;
	case PLAYLIST_VIEW:
		searchable = pl_searchable;
		update_pl_window();
		break;
	case QUEUE_VIEW:
		searchable = play_queue_searchable;
		update_play_queue_window();
		break;
	case BROWSER_VIEW:
		searchable = browser_searchable;
		update_browser_window();
		break;
	case FILTERS_VIEW:
		searchable = filters_searchable;
		update_filters_window();
		break;
	}
	lib_unlock();

	refresh();
}

void toggle_remaining_time(void)
{
	show_remaining_time ^= 1;
	update_statusline();
}

void enter_tree_view(void)
{
	set_view(TREE_VIEW);
}

void enter_sorted_view(void)
{
	set_view(SORTED_VIEW);
}

void enter_playlist_view(void)
{
	set_view(PLAYLIST_VIEW);
}

void enter_queue_view(void)
{
	set_view(QUEUE_VIEW);
}

void enter_browser_view(void)
{
	set_view(BROWSER_VIEW);
}

void enter_filters_view(void)
{
	set_view(FILTERS_VIEW);
}

void enter_command_mode(void)
{
	error_buf[0] = 0;
	error_time = 0;
	input_mode = COMMAND_MODE;
	update_commandline();
}

void enter_search_mode(void)
{
	error_buf[0] = 0;
	error_time = 0;
	input_mode = SEARCH_MODE;
	search_direction = SEARCH_FORWARD;
	update_commandline();
}

void enter_search_backward_mode(void)
{
	error_buf[0] = 0;
	error_time = 0;
	input_mode = SEARCH_MODE;
	search_direction = SEARCH_BACKWARD;
	update_commandline();
}

void quit(void)
{
	running = 0;
}

void update_color(int idx)
{
	/* first color pair is 1 */
	int pair = idx + 1;
	int fg, bg, cursed;

	fg = clamp(fg_colors[idx], -1, 255);
	bg = clamp(bg_colors[idx], -1, 255);
	if (fg == -1) {
		init_pair(pair, fg, bg);
		cursed = COLOR_PAIR(pair);
	} else {
		if (fg >= 8 && fg <= 15) {
			/* fg colors 8..15 are special (0..7 + bold) */
			init_pair(pair, fg & 7, bg);
			cursed = COLOR_PAIR(pair) | (fg & BRIGHT ? A_BOLD : 0);
		} else {
			init_pair(pair, fg, bg);
			cursed = COLOR_PAIR(pair);
		}
	}
	cursed_colors[idx] = cursed;
}

static void full_update(void)
{
	curs_set(0);
	lib.tree_win->changed = 1;
	lib.track_win->changed = 1;
	update_view();
	do_update_titleline();
	do_update_statusline();
	do_update_commandline();
	post_update();
}

#define HELP_WIDTH 80
#define HELP_HEIGHT 24
#define HELP_W (HELP_WIDTH - 2)
#define HELP_H (HELP_HEIGHT - 2)

static void display_global_help(WINDOW *w)
{
	int row, col;

	row = 1;
	col = 2;
	mvwaddstr(w, row++, (HELP_W - 11) / 2, "Global Keys");
	mvwaddstr(w, row++, (HELP_W - 11) / 2, "~~~~~~~~~~~");
	row++;
	mvwaddstr(w, row++, col, "z - skip back in playlist");
	mvwaddstr(w, row++, col, "x - play");
	mvwaddstr(w, row++, col, "c - pause");
	mvwaddstr(w, row++, col, "v - stop");
	mvwaddstr(w, row++, col, "b - skip forward in playlist");
	mvwaddstr(w, row++, col, "C - toggle continue");
	mvwaddstr(w, row++, col, "r - toggle repeat");
	mvwaddstr(w, row++, col, "m - toggle playlist mode");
	mvwaddstr(w, row++, col, "p - toggle play mode");
	mvwaddstr(w, row++, col, "t - toggle time elapsed/remaining");
	mvwaddstr(w, row++, col, "Q - quit");
	mvwaddstr(w, row++, col, ": - command mode");
	mvwaddstr(w, row++, col, "left,  h - seek 5 seconds back");
	mvwaddstr(w, row++, col, "right, l - seek 5 seconds forward");

	row = 4;
	col = 40;
	mvwaddstr(w, row++, col, "1 - show artist/album/track view");
	mvwaddstr(w, row++, col, "2 - show shuffle view");
	mvwaddstr(w, row++, col, "3 - show sorted view");
	mvwaddstr(w, row++, col, "4 - show play queue view");
	mvwaddstr(w, row++, col, "5 - show directory browser");
	mvwaddstr(w, row++, col, "6 - show filter view");
	mvwaddstr(w, row++, col, "up,   k           - move up");
	mvwaddstr(w, row++, col, "down, j           - move down");
	mvwaddstr(w, row++, col, "page up,   ctrl-b - move page up");
	mvwaddstr(w, row++, col, "page down, ctrl-f - move page down");
	mvwaddstr(w, row++, col, "home, g           - goto top");
	mvwaddstr(w, row++, col, "end,  G           - goto bottom");
	mvwaddstr(w, row++, col, "- or +, = - volume down or up");
	mvwaddstr(w, row++, col, "{ or }    - left or right channel down");
	mvwaddstr(w, row++, col, "[ or ]    - left or right channel up");
}

static void display_misc_help(WINDOW *w)
{
	int row, col;

	row = 1;
	col = 2;
	mvwaddstr(w, row++, col, "Tree / Shuffle / Sorted View Keys");
	mvwaddstr(w, row++, col, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
	row++;
	mvwaddstr(w, row++, col, "D, del - remove selection");
	mvwaddstr(w, row++, col, "e      - append to play queue");
	mvwaddstr(w, row++, col, "E      - prepend to play queue");
	mvwaddstr(w, row++, col, "i      - jump to current track");
	mvwaddstr(w, row++, col, "u      - update tags");
	mvwaddstr(w, row++, col, "enter  - play selected track");
	mvwaddstr(w, row++, col, "space  - show/hide albums");
	mvwaddstr(w, row++, col, "tab    - switch tree/track windows");
	row++;
	mvwaddstr(w, row++, col, "Play Queue Keys");
	mvwaddstr(w, row++, col, "~~~~~~~~~~~~~~~");
	row++;
	mvwaddstr(w, row++, col, "D, del - remove selected track");

	row = 1;
	col = 40;
	mvwaddstr(w, row++, col, "Directory Browser Keys");
	mvwaddstr(w, row++, col, "~~~~~~~~~~~~~~~~~~~~~~");
	row++;
	mvwaddstr(w, row++, col, "D, del    - remove selected file");
	mvwaddstr(w, row++, col, "a         - add to playlist");
	mvwaddstr(w, row++, col, "e         - append to play queue");
	mvwaddstr(w, row++, col, "E         - prepend to play queue");
	mvwaddstr(w, row++, col, "i         - show/hide hidden files");
	mvwaddstr(w, row++, col, "u         - update dir/playlist");
	mvwaddstr(w, row++, col, "enter     - cd to dir/playlist");
	mvwaddstr(w, row++, col, "            or play file");
	mvwaddstr(w, row++, col, "backspace - cd to parent directory");
	row++;
	mvwaddstr(w, row++, col, "Filter View Keys");
	mvwaddstr(w, row++, col, "~~~~~~~~~~~~~~~~");
	row++;
	mvwaddstr(w, row++, col, "D, del - remove selected filter");
	mvwaddstr(w, row++, col, "space  - select/unselect filter");
	mvwaddstr(w, row++, col, "enter  - apply selected filters");
}

static void display_search_mode_help(WINDOW *w)
{
	int row, col;

	row = 1;
	col = 2;
	mvwaddstr(w, row++, (HELP_W - 9) / 2, "Searching");
	mvwaddstr(w, row++, (HELP_W - 9) / 2, "~~~~~~~~~");
	row++;
	mvwaddstr(w, row++, col, "/WORDS  - search forward");
	mvwaddstr(w, row++, col, "?WORDS  - search backward");
	mvwaddstr(w, row++, col, "//WORDS - search forward (see below)");
	mvwaddstr(w, row++, col, "??WORDS - search backward (see below)");
	mvwaddstr(w, row++, col, "/       - search forward for the latest used pattern");
	mvwaddstr(w, row++, col, "?       - search backward for the latest used pattern");
	mvwaddstr(w, row++, col, "n       - search next");
	mvwaddstr(w, row++, col, "N       - search previous");
	row++;
	mvwaddstr(w, row++, col, "WORDS is list of words separated by spaces.  Search is case insensitive");
	mvwaddstr(w, row++, col, "and works in every view.");
	row++;
	mvwaddstr(w, row++, col, "In views 1-4 words are compared to artist, album and title tags.  Use");
	mvwaddstr(w, row++, col, "//WORDS and ??WORDS to search only artists/albums in view 1 or titles in");
	mvwaddstr(w, row++, col, "views 2-4.  If the file doesn't have tags words are compared to filename");
	mvwaddstr(w, row++, col, "without path.  In view 5 words are compared to filename without path.");
}

static void display_command_mode_help(WINDOW *w)
{
	int row, col;

	row = 1;
	col = 2;
	mvwaddstr(w, row++, (HELP_W - 12) / 2, "Command Mode");
	mvwaddstr(w, row++, (HELP_W - 12) / 2, "~~~~~~~~~~~~");
	row++;
	mvwaddstr(w, row++, col, ":add file/dir/playlist     - add to playlist");
	mvwaddstr(w, row++, col, ":bind context key command  - bind key");
	mvwaddstr(w, row++, col, ":cd [dir]                  - change directory");
	mvwaddstr(w, row++, col, ":clear                     - clear playlist");
	mvwaddstr(w, row++, col, ":enqueue file/dir/playlist - add to play queue");
	mvwaddstr(w, row++, col, ":filter [value]            - set temporary playlist filter");
	mvwaddstr(w, row++, col, ":fset name=value           - add or replace filter");
	mvwaddstr(w, row++, col, ":load filename             - load playlist");
	mvwaddstr(w, row++, col, ":run command               - run `command' for the selected files");
	mvwaddstr(w, row++, col, ":save [filename]           - save playlist");
	mvwaddstr(w, row++, col, ":seek [+-]POS[mh]          - seek to absolute or relative (+/-) position");
	mvwaddstr(w, row++, col, "                             POS is seconds, minutes (m) or hours (h)");
	mvwaddstr(w, row++, col, ":set option=value          - see next page");
	mvwaddstr(w, row++, col, ":shuffle                   - reshuffle playlist");
	mvwaddstr(w, row++, col, ":unbind context key        - unbind key");
	row++;
	mvwaddstr(w, row++, col, "Use <tab> to expand commands and their arguments.");
	mvwaddstr(w, row++, col, "Unambiguous short commands work too (f.e: ':a file.ogg').");
}

static void display_options_help(WINDOW *w)
{
	int row, col;

	row = 1;
	col = 2;
	mvwaddstr(w, row++, (HELP_W - 7) / 2, "Options");
	mvwaddstr(w, row++, (HELP_W - 7) / 2, "~~~~~~~");
	row++;
	mvwaddstr(w, row++, col, "output_plugin           - output plugin (alsa, arts, oss)");
	mvwaddstr(w, row++, col, "buffer_seconds          - size of player buffer in seconds (1-10)");
	mvwaddstr(w, row++, col, "confirm_run             - confirm :run with >1 files (true/false)");
	mvwaddstr(w, row++, col, "dsp.*, mixer.*          - output plugin options");
	mvwaddstr(w, row++, col, "color_*                 - user interface colors");
	mvwaddstr(w, row++, col, "format_current          - format of the line showing currently played track");
	mvwaddstr(w, row++, col, "format_playlist         - format of text in shuffle and sorted windows");
	mvwaddstr(w, row++, col, "format_title            - format of window title");
	mvwaddstr(w, row++, col, "format_track_win        - format of text in track window");
	mvwaddstr(w, row++, col, "altformat_*             - format strings used when file has no tags");
	mvwaddstr(w, row++, col, "sort                    - comma separated list of sort keys for the sorted");
	mvwaddstr(w, row++, col, "                          view (3). Valid keys: artist, album, title,");
	mvwaddstr(w, row++, col, "                          tracknumber, discnumber, date, genre, filename)");
	mvwaddstr(w, row++, col, "status_display_program  - script to run when player status changes");
	row++;
	mvwaddstr(w, row++, col, "Example:  :set sort=genre,date");
	mvwaddstr(w, row++, col, "Use <tab> to cycle through all options.");
}

static void display_last_help(WINDOW *w)
{
	const char *title = PACKAGE " " VERSION;
	char underline[64];
	int title_len = strlen(title);
	int row, col, i;

	for (i = 0; i < title_len; i++)
		underline[i] = '~';
	underline[i] = 0;
	row = 1;
	col = 2;
	mvwaddstr(w, row++, (HELP_W - title_len) / 2, title);
	mvwaddstr(w, row++, (HELP_W - title_len) / 2, underline);
	row++;
	mvwaddstr(w, row++, col, "Run `cmus --help' to display command line options.");
	mvwaddstr(w, row++, col, "Full documentation: " DATADIR "/doc/" PACKAGE "/cmus.html");
	row++;
	mvwaddstr(w, row++, col, "Copyright 2004-2005 Timo Hirvonen");
	mvwaddstr(w, row++, col, "Send bug reports, patches etc. to " PACKAGE_BUGREPORT);
}

void display_help(void)
{
	int x, y, page;
	WINDOW *w;

	if (COLS < HELP_WIDTH || LINES < HELP_HEIGHT) {
		error_msg("window is too small to display help");
		return;
	}

	y = (COLS - HELP_WIDTH) / 2;
	if (y < 0)
		y = 0;
	x = (LINES - HELP_HEIGHT) / 2;
	if (x < 0)
		x = 0;
	w = newwin(HELP_HEIGHT, HELP_WIDTH, x, y);

	page = 0;
	while (1) {
		box(w, 0, 0);
		switch (page) {
		case 0:
			display_global_help(w);
			break;
		case 1:
			display_misc_help(w);
			break;
		case 2:
			display_search_mode_help(w);
			break;
		case 3:
			display_command_mode_help(w);
			break;
		case 4:
			display_options_help(w);
			break;
#define LAST_HELP_PAGE 5
		case LAST_HELP_PAGE:
			display_last_help(w);
			mvwaddstr(w, HELP_H, (HELP_W - 57) / 2,
					"Press <space> for first page or <enter> to return to cmus");
			break;
		}
		if (page < LAST_HELP_PAGE)
			mvwaddstr(w, HELP_H, (HELP_W - 56) / 2,
					"Press <space> for next page or <enter> to return to cmus");
		wrefresh(w);
		while (1) {
			int ch = getch();

			if (ch == ' ') {
				if (page == LAST_HELP_PAGE) {
					page = 0;
				} else {
					page++;
				}
				break;
			}
			if (ch == 127 || ch == KEY_BACKSPACE) {
				if (page == 0) {
					page = LAST_HELP_PAGE;
				} else {
					page--;
				}
				break;
			}
			if (ch == '\n') {
				page = LAST_HELP_PAGE + 1;
				break;
			}
		}
		if (page > LAST_HELP_PAGE)
			break;
		wclear(w);
	}

	delwin(w);
	full_update();
}

static void clear_error(void)
{
	time_t t = time(NULL);

	/* error msg is visible at least 3s */
	if (t - error_time < 3)
		return;

	if (error_buf[0]) {
		error_time = 0;
		error_buf[0] = 0;
		update_commandline();
	}
}

/* screen updates }}} */

static void spawn_status_program(void)
{
	static const char *status_strs[] = { "stopped", "playing", "paused" };
	const char *stream_title = NULL;
	char *argv[32];
	int i, status;

	if (status_display_program == NULL || status_display_program[0] == 0)
		return;

	player_info_lock();
	status = player_info.status;
	if (status == 1)
		stream_title = get_stream_title(player_info.metadata);
	player_info_unlock();

	i = 0;
	argv[i++] = xstrdup(status_display_program);

	argv[i++] = xstrdup("status");
	argv[i++] = xstrdup(status_strs[status]);
	if (cur_track_info) {
		static const char *keys[] = {
			"artist", "album", "discnumber", "tracknumber", "title", "date", NULL
		};
		int j;

		if (is_url(cur_track_info->filename)) {
			argv[i++] = xstrdup("url");
			argv[i++] = xstrdup(cur_track_info->filename);
			if (stream_title) {
				argv[i++] = xstrdup("title");
				argv[i++] = xstrdup(stream_title);
			}
		} else {
			argv[i++] = xstrdup("file");
			argv[i++] = xstrdup(cur_track_info->filename);
			for (j = 0; keys[j]; j++) {
				const char *key = keys[j];
				const char *val;

				val = comments_get_val(cur_track_info->comments, key);
				if (val) {
					argv[i++] = xstrdup(key);
					argv[i++] = xstrdup(val);
				}
			}
		}
	}
	argv[i++] = NULL;
	if (spawn(argv, &status) == -1)
		error_msg("couldn't run `%s': %s", status_display_program, strerror(errno));
	for (i = 0; argv[i]; i++)
		free(argv[i]);
}

static void finish(int sig)
{
	running = 0;
}

static int needs_to_resize = 1;

static void sig_winch(int sig)
{
	needs_to_resize = 1;
}

static int get_window_size(int *lines, int *columns)
{
	struct winsize ws;

	if (ioctl(0, TIOCGWINSZ, &ws) == -1)
		return -1;
	*columns = ws.ws_col;
	*lines = ws.ws_row;
	return 0;
}

static void resize_playlist(int w, int h)
{
	tree_win_w = w / 3;
	track_win_w = w - tree_win_w - 1;
	if (tree_win_w < 8)
		tree_win_w = 8;
	if (track_win_w < 8)
		track_win_w = 8;
	tree_win_x = 0;
	tree_win_y = 0;
	track_win_x = tree_win_w + 1;
	track_win_y = 0;

	h--;
	lib_lock();
	window_set_nr_rows(lib.tree_win, h);
	window_set_nr_rows(lib.track_win, h);
	window_set_nr_rows(lib.sorted_win, h);
	lib_unlock();
}

static void get_colors(void)
{
	char buf[64];
	int i;

	for (i = 0; i < NR_COLORS; i++) {
		snprintf(buf, sizeof(buf), "%s_bg", color_names[i]);
		sconf_get_int_option(buf, &bg_colors[i]);
		snprintf(buf, sizeof(buf), "%s_fg", color_names[i]);
		sconf_get_int_option(buf, &fg_colors[i]);
	}
}

static void set_colors(void)
{
	char buf[64];
	int i;

	for (i = 0; i < NR_COLORS; i++) {
		snprintf(buf, sizeof(buf), "%s_bg", color_names[i]);
		sconf_set_int_option(buf, bg_colors[i]);
		snprintf(buf, sizeof(buf), "%s_fg", color_names[i]);
		sconf_set_int_option(buf, fg_colors[i]);
	}
}

/* irman {{{ */
#if defined(CONFIG_IRMAN)

static struct irman *irman = NULL;
static char *irman_device = NULL;
static int irman_fd = -1;

static struct {
	void (*function)(void);
	const char *option;
	char *text;
} ir_commands[] = {
	{ player_play, "btn_play", NULL },
	{ player_stop, "btn_stop", NULL },
	{ player_pause, "btn_pause", NULL },
	{ cmus_prev, "btn_prev", NULL },
	{ cmus_next, "btn_next", NULL },
	{ cmus_seek_bwd, "btn_seek_bwd", NULL },
	{ cmus_seek_fwd, "btn_seek_fwd", NULL },
	{ cmus_vol_up, "btn_vol_up", NULL },
	{ cmus_vol_down, "btn_vol_down", NULL },
	{ lib_toggle_play_mode, "btn_play_mode", NULL },
	{ lib_toggle_repeat, "btn_repeat", NULL },
	{ player_toggle_cont, "btn_continue", NULL },
	{ NULL, NULL, NULL }
};

static void ir_read(void)
{
	unsigned char code[IRMAN_CODE_LEN];
	char text[IRMAN_TEXT_SIZE];
	int i, rc;

	rc = irman_get_code(irman, code);
	if (rc) {
		d_print("irman_get_code: error: %s\n", strerror(errno));
		return;
	}
	irman_code_to_text(text, code);
	for (i = 0; ir_commands[i].function; i++) {
		if (ir_commands[i].text == NULL)
			continue;
		if (strcmp(ir_commands[i].text, text) == 0) {
			ir_commands[i].function();
			break;
		}
	}
}

static int ir_init(void)
{
	int i;

	sconf_get_str_option("irman_device", &irman_device);
	for (i = 0; ir_commands[i].function; i++)
		sconf_get_str_option(ir_commands[i].option, &ir_commands[i].text);
	if (irman_device == NULL) {
		warn("irman device not set (run `" PACKAGE " --irman-config')\n");
		return 1;
	}
	irman = irman_open(irman_device);
	if (irman == NULL) {
		warn_errno("error opening irman device `%s'", irman_device);
		return 1;
	}
	irman_fd = irman_get_fd(irman);
	return 0;
}

static void ir_exit(void)
{
	irman_close(irman);
}
#endif

/* irman }}} */

static void update(void)
{
	int needs_view_update = 0;
	int needs_title_update = 0;
	int needs_status_update = 0;
	int needs_command_update = 0;
	int needs_spawn = 0;

	if (needs_to_resize) {
		int w, h;
		int columns, lines;

		if (get_window_size(&lines, &columns) == 0) {
			needs_to_resize = 0;
			resizeterm(lines, columns);
			w = COLS;
			h = LINES - 3;
			if (w < 16)
				w = 16;
			if (h < 8)
				h = 8;
			resize_playlist(w, h);
			window_set_nr_rows(filters_win, h - 1);
			window_set_nr_rows(browser_win, h - 1);
			window_set_nr_rows(play_queue_win, h - 1);
			window_set_nr_rows(pl_win, h - 1);
			needs_title_update = 1;
			needs_status_update = 1;
			needs_command_update = 1;
		}
	}

	player_info_lock();
	lib_lock();

	needs_spawn = player_info.status_changed || player_info.file_changed ||
		player_info.metadata_changed;

	if (player_info.file_changed) {
		if (cur_track_info)
			track_info_unref(cur_track_info);
		if (player_info.filename[0] == 0) {
			cur_track_info = NULL;
		} else {
			cur_track_info = cmus_get_track_info(player_info.filename);
		}
		player_info.file_changed = 0;
		needs_title_update = 1;
		needs_status_update = 1;
	}
	if (player_info.metadata_changed) {
		player_info.metadata_changed = 0;
		needs_title_update = 1;
	}
	if (lib.status_changed || player_info.position_changed ||
			player_info.status_changed || player_info.volume_changed) {
		player_info.position_changed = 0;
		player_info.status_changed = 0;
		player_info.volume_changed = 0;

		needs_status_update = 1;
	}
	switch (cur_view) {
	case TREE_VIEW:
		needs_view_update += lib.tree_win->changed || lib.track_win->changed;
		break;
	case SORTED_VIEW:
		needs_view_update += lib.sorted_win->changed;
		break;
	case PLAYLIST_VIEW:
		needs_view_update += pl_win->changed;
		break;
	case QUEUE_VIEW:
		needs_view_update += play_queue_win->changed;
		break;
	case BROWSER_VIEW:
		needs_view_update += browser_win->changed;
		break;
	case FILTERS_VIEW:
		needs_view_update += filters_win->changed;
		break;
	}
	lib_unlock();
	player_info_unlock();

	if (needs_spawn)
		spawn_status_program();

	if (needs_view_update || needs_title_update || needs_status_update || needs_command_update) {
		curs_set(0);

		if (needs_view_update)
			update_view();
		if (needs_title_update)
			do_update_titleline();
		if (needs_status_update)
			do_update_statusline();
		if (needs_command_update)
			do_update_commandline();
		post_update();
	}
}

static int u_getch(uchar *uch, int *keyp)
{
	int key;
	int bit = 7;
	uchar u, ch;
	int mask = (1 << 7);

	key = getch();
	if (key == ERR || key == 0)
		return -1;
	if (key > 255) {
		*keyp = key;
		return 1;
	}
	ch = (unsigned char)key;
	while (bit > 0 && ch & mask) {
		mask >>= 1;
		bit--;
	}
	if (bit == 7) {
		/* ascii */
		u = ch;
	} else {
		int count;

		u = ch & ((1 << bit) - 1);
		count = 6 - bit;
		while (count) {
			key = getch();
			if (key == ERR || key == 0)
				return -1;
			ch = (unsigned char)key;
			u = (u << 6) | (ch & 63);
			count--;
		}
	}
	*uch = u;
	return 0;
}

static void main_loop(void)
{
	int rc, fd_high;

	fd_high = remote_socket;
#if defined(CONFIG_IRMAN)
	if (irman_fd > fd_high)
		fd_high = irman_fd;
#endif

	while (running) {
		fd_set set;
		struct timeval tv;

		update();

		FD_ZERO(&set);
		FD_SET(0, &set);
		FD_SET(remote_socket, &set);
#if defined(CONFIG_IRMAN)
		FD_SET(irman_fd, &set);
#endif
		tv.tv_sec = 0;
		tv.tv_usec = 50e3;
		rc = select(fd_high + 1, &set, NULL, NULL, &tv);
		if (rc <= 0)
			continue;

		if (FD_ISSET(remote_socket, &set)) {
			remote_server_serve();
		}
		if (FD_ISSET(0, &set)) {
			int key = 0;
			uchar ch;

			if (using_utf8) {
				rc = u_getch(&ch, &key);
			} else {
				ch = key = getch();
				if (key == ERR || key == 0) {
					rc = -1;
				} else {
					if (key > 255) {
						rc = 1;
					} else {
						rc = 0;
					}
				}
			}
			if (rc == 0) {
				clear_error();
				if (input_mode == NORMAL_MODE) {
					normal_mode_ch(ch);
				} else if (input_mode == COMMAND_MODE) {
					command_mode_ch(ch);
					update_commandline();
				} else if (input_mode == SEARCH_MODE) {
					search_mode_ch(ch);
					update_commandline();
				}
			} else if (rc == 1) {
				clear_error();
				if (input_mode == NORMAL_MODE) {
					normal_mode_key(key);
				} else if (input_mode == COMMAND_MODE) {
					command_mode_key(key);
					update_commandline();
				} else if (input_mode == SEARCH_MODE) {
					search_mode_key(key);
					update_commandline();
				}
			}
		}
#if defined(CONFIG_IRMAN)
		if (FD_ISSET(irman_fd, &set)) {
			ir_read();
		}
#endif
	}
}

static int get_next(char **filename)
{
	struct track_info *info;

	info = play_queue_remove();
	if (info == NULL) {
		if (play_library) {
			info = lib_set_next();
		} else {
			info = pl_set_next();
		}
		if (info == NULL)
			return -1;
	}

	*filename = xstrdup(info->filename);
	track_info_unref(info);
	return 0;
}

static const struct player_callbacks player_callbacks = {
	.get_next = get_next
};

static void init_curses(void)
{
	struct sigaction act;

	signal(SIGINT, finish);

	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = sig_winch;
	sigaction(SIGWINCH, &act, NULL);

	initscr();

	/* turn off kb buffering */
	cbreak();

	keypad(stdscr, TRUE);

	/* wait max 5 * 0.1 s if there are no keys available
	 * doesn't really matter because we use select()
	 */
	halfdelay(5);

	noecho();
	if (has_colors()) {
		int i;

		start_color();
		use_default_colors();
		for (i = 0; i < NR_COLORS; i++)
			update_color(i);
	}
	d_print("Number of supported colors: %d\n", COLORS);
	ui_initialized = 1;
}

static void init_all(void)
{
	int rc, btmp;
	char *term, *sort;
	char **keys;

	term = getenv("TERM");
	if (term && (strncmp(term, "xterm", 5) == 0 ||
		     strncmp(term, "rxvt", 4) == 0 ||
		     strcmp(term, "screen") == 0))
		update_window_title = 1;

	remote_socket = remote_server_init(server_address);

	rc = player_init(&player_callbacks);
	if (rc) {
		remote_server_exit();
		die("could not initialize player\n");
	}

	lib_init();
	searchable = tree_searchable;

	pl_init();

	cmus_init();

#if defined(CONFIG_IRMAN)
	rc = ir_init();
	if (rc) {
		lib_exit();
		player_exit();
		remote_server_exit();
		exit(1);
	}
#endif

	if (sconf_get_bool_option("continue", &btmp))
		player_set_cont(btmp);
	if (sconf_get_bool_option("repeat", &btmp))
		repeat = btmp;
	if (sconf_get_bool_option("shuffle", &btmp))
		shuffle = btmp;
	if (sconf_get_bool_option("play_library", &btmp))
		play_library = btmp;
	if (sconf_get_bool_option("play_sorted", &btmp))
		lib.play_sorted = btmp;
	if (sconf_get_int_option("playlist_mode", &btmp)) {
		if (btmp < 0 || btmp > 2)
			btmp = 0;
		lib.playlist_mode = btmp;
	}
	sconf_get_bool_option("show_remaining_time", &show_remaining_time);
	sconf_get_str_option("status_display_program", &status_display_program);

	if (!sconf_get_str_option("lib_sort", &sort))
		sort = xstrdup("artist album discnumber tracknumber title filename");
	keys = parse_sort_keys(sort);
	if (keys)
		lib_set_sort_keys(keys);
	free(sort);

	if (!sconf_get_str_option("pl_sort", &sort)) {
		keys = parse_sort_keys(sort);
		if (keys)
			pl_set_sort_keys(keys);
		free(sort);
	}

	if (sconf_get_int_option("buffer_chunks", &btmp)) {
		if (btmp < 3)
			btmp = 3;
		if (btmp > 30)
			btmp = 30;
		player_set_buffer_chunks(btmp);
	}
	get_colors();

	browser_init();
	filters_init();
	cmdline_init();
	/* commands_init must be after player_init */
	commands_init();
	options_init();
	search_mode_init();
	keys_init();

	player_get_volume(&player_info.vol_left, &player_info.vol_right);

	lib_autosave_filename = xstrjoin(cmus_config_dir, "/lib.pl");
	pl_autosave_filename = xstrjoin(cmus_config_dir, "/playlist.pl");
	cmus_add(lib_add_track, lib_autosave_filename, FILE_TYPE_PL, JOB_TYPE_LIB);
	cmus_add(pl_add_track, pl_autosave_filename, FILE_TYPE_PL, JOB_TYPE_PL);

	init_curses();
}

static void exit_all(void)
{
	endwin();

#if defined(CONFIG_IRMAN)
	ir_exit();
#endif
	remote_server_exit();

	cmus_exit();
	cmus_save(lib_for_each, lib_autosave_filename);
	cmus_save(pl_for_each, pl_autosave_filename);

	if (cur_track_info)
		track_info_unref(cur_track_info);

	sconf_set_bool_option("continue", player_info.cont);
	sconf_set_bool_option("repeat", repeat);
	sconf_set_bool_option("shuffle", shuffle);
	sconf_set_bool_option("play_library", play_library);
	sconf_set_bool_option("play_sorted", lib.play_sorted);
	sconf_set_int_option("playlist_mode", lib.playlist_mode);
	sconf_set_bool_option("show_remaining_time", show_remaining_time);
	sconf_set_str_option("status_display_program",
			status_display_program ? status_display_program : "");
	sconf_set_str_option("lib_sort", keys_to_str(lib.sort_keys));
	sconf_set_str_option("pl_sort", keys_to_str(pl_sort_keys));
	sconf_set_int_option("buffer_chunks", player_get_buffer_chunks());
	set_colors();

	free(lib_autosave_filename);
	free(status_display_program);

	player_exit();
	lib_exit();

	keys_exit();
	options_exit();
	commands_exit();
	search_mode_exit();
	filters_exit();
	browser_exit();
}

enum {
#if defined(CONFIG_IRMAN)
	FLAG_IRMAN_CONFIG,
#endif
	FLAG_LISTEN,
	FLAG_PLUGINS,
	FLAG_HELP,
	FLAG_VERSION,
	NR_FLAGS
};

static struct option options[NR_FLAGS + 1] = {
#if defined(CONFIG_IRMAN)
	{ 0, "irman-config", 0 },
#endif
	{ 0, "listen", 1 },
	{ 0, "plugins", 0 },
	{ 0, "help", 0 },
	{ 0, "version", 0 },
	{ 0, NULL, 0 }
};

static const char *usage =
"Usage: %s [OPTION]...\n"
"Curses based music player.\n"
"\n"
#if defined(CONFIG_IRMAN)
"      --irman-config  configure irman settings\n"
#endif
"      --listen ADDR   listen ADDR (unix socket) instead of /tmp/cmus-$USER\n"
"      --plugins       list available plugins and exit\n"
"      --help          display this help and exit\n"
"      --version       " VERSION "\n"
"\n"
"Use cmus-remote to control cmus from command line.\n"
"Documentation: " DATADIR "/doc/" PACKAGE "/cmus.html\n"
"Report bugs to <" PACKAGE_BUGREPORT ">.\n";

int main(int argc, char *argv[])
{
	int configure_irman = 0;
	int list_plugins = 0;

	program_name = argv[0];
	argv++;
	while (1) {
		int idx;
		char *arg;

		idx = get_option(&argv, options, &arg);
		if (idx < 0)
			break;

		switch (idx) {
#if defined(CONFIG_IRMAN)
		case FLAG_IRMAN_CONFIG:
			configure_irman = 1;
			break;
#endif
		case FLAG_HELP:
			printf(usage, program_name);
			return 0;
		case FLAG_VERSION:
			printf(PACKAGE " " VERSION "\nCopyright 2004-2005 Timo Hirvonen\n");
			return 0;
		case FLAG_PLUGINS:
			list_plugins = 1;
			break;
		case FLAG_LISTEN:
			server_address = xstrdup(arg);
			break;
		}
	}

	setlocale(LC_CTYPE, "");
#ifdef CODESET
	charset = nl_langinfo(CODESET);
#else
	charset = "ISO-8859-1";
#endif
	if (strcmp(charset, "UTF-8") == 0) {
		using_utf8 = 1;
	} else {
		using_utf8 = 0;
	}
	misc_init();
	if (server_address == NULL) {
		server_address = xnew(char, 256);
		snprintf(server_address, 256, "/tmp/cmus-%s", user_name);
	}
	debug_init();
	sconf_load();

	d_print("charset = '%s'\n", charset);

	if (configure_irman) {
#if defined(CONFIG_IRMAN)
		if (irman_config())
			return 1;
#endif
	} else {
		player_init_plugins();
		if (list_plugins) {
			player_dump_plugins();
			return 0;
		}
		init_all();
		main_loop();
		exit_all();
	}

	sconf_save();
	return 0;
}
