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
#include <misc.h>
#include <prog.h>
#include <uchar.h>
#include <spawn.h>
#include <server.h>
#include <keys.h>
#include <debug.h>
#include <config.h>

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

/* display parse errors? (command line) */
int display_errors = 0;

char *lib_autosave_filename;
char *pl_autosave_filename;
char *lib_filename = NULL;
char *pl_filename = NULL;

/* ------------------------------------------------------------------------- */

/* currently playing file */
static struct track_info *cur_track_info;

static int update_window_title = 0;

static int running = 1;

/* shown error message and time stamp
 * error is cleared if it is older than 3s and key was pressed
 */
static char error_buf[512];
static time_t error_time = 0;
/* info messages are displayed in different color */
static int msg_is_error;

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

enum {
	CURSED_WIN,
	CURSED_WIN_CUR,
	CURSED_WIN_SEL,
	CURSED_WIN_SEL_CUR,

	CURSED_WIN_ACTIVE,
	CURSED_WIN_ACTIVE_CUR,
	CURSED_WIN_ACTIVE_SEL,
	CURSED_WIN_ACTIVE_SEL_CUR,

	CURSED_SEPARATOR,
	CURSED_WIN_TITLE,
	CURSED_COMMANDLINE,
	CURSED_STATUSLINE,

	CURSED_TITLELINE,
	CURSED_DIR,
	CURSED_ERROR,
	CURSED_INFO,

	NR_CURSED
};

static unsigned char cursed_to_bg_idx[NR_CURSED] = {
	COLOR_WIN_BG,
	COLOR_WIN_BG,
	COLOR_WIN_INACTIVE_SEL_BG,
	COLOR_WIN_INACTIVE_CUR_SEL_BG,

	COLOR_WIN_BG,
	COLOR_WIN_BG,
	COLOR_WIN_SEL_BG,
	COLOR_WIN_CUR_SEL_BG,

	COLOR_WIN_BG,
	COLOR_WIN_TITLE_BG,
	COLOR_CMDLINE_BG,
	COLOR_STATUSLINE_BG,

	COLOR_TITLELINE_BG,
	COLOR_WIN_BG,
	COLOR_CMDLINE_BG,
	COLOR_CMDLINE_BG
};

static unsigned char cursed_to_fg_idx[NR_CURSED] = {
	COLOR_WIN_FG,
	COLOR_WIN_CUR,
	COLOR_WIN_INACTIVE_SEL_FG,
	COLOR_WIN_INACTIVE_CUR_SEL_FG,

	COLOR_WIN_FG,
	COLOR_WIN_CUR,
	COLOR_WIN_SEL_FG,
	COLOR_WIN_CUR_SEL_FG,

	COLOR_SEPARATOR,
	COLOR_WIN_TITLE_FG,
	COLOR_CMDLINE_FG,
	COLOR_STATUSLINE_FG,

	COLOR_TITLELINE_FG,
	COLOR_WIN_DIR,
	COLOR_ERROR,
	COLOR_INFO
};

/* index is CURSED_*, value is fucking color pair */
static int pairs[NR_CURSED];

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
	bkgdset(pairs[(active << 2) | (selected << 1) | current]);
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
	bkgdset(pairs[(active << 2) | (selected << 1) | current]);

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
	bkgdset(pairs[(active << 2) | (selected << 1) | current]);

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

	bkgdset(pairs[(active << 2) | (selected << 1) | current]);

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

	bkgdset(pairs[(active << 2) | (selected << 1) | current]);

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

		bkgdset(pairs[(active << 2) | (selected << 1) | current]);
	} else {
		if (e->type == BROWSER_ENTRY_DIR) {
			bkgdset(pairs[CURSED_DIR]);
		} else {
			bkgdset(pairs[CURSED_WIN]);
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
	bkgdset(pairs[(active << 2) | (selected << 1) | current]);

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

	bkgdset(pairs[CURSED_WIN_TITLE]);
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

	bkgdset(pairs[0]);
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

static const char *pretty(const char *path)
{
	static int home_len = -1;
	static char buf[256];

	if (home_len == -1)
		home_len = strlen(home_dir);

	if (strncmp(path, home_dir, home_len) || path[home_len] != '/')
		return path;

	buf[0] = '~';
	strcpy(buf + 1, path + home_len);
	return buf;
}

static const char * const sorted_names[2] = { "", "sorted by " };

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
	snprintf(title, sizeof(title), "Library %s - %d tracks %s%s", pretty(filename),
			lib.nr_tracks, sorted_names[lib_sort_str[0] != 0], lib_sort_str);
	update_window(lib.sorted_win, 0, 0, COLS, title, print_sorted);
}

static void update_pl_window(void)
{
	char title[512];
	char *filename;
	int pos;

	filename = pl_filename ? pl_filename : pl_autosave_filename;
	if (using_utf8) {
		/* already UTF-8 */
	} else {
		utf8_encode(filename);
		filename = conv_buffer;
	}

	snprintf(title, sizeof(title), "Playlist %s - %d tracks", pretty(filename), pl_nr_tracks);
	if (pl_nr_marked) {
		pos = strlen(title);
		snprintf(title + pos, sizeof(title) - pos, " (%d marked)", pl_nr_marked);
	}
	pos = strlen(title);
	snprintf(title + pos, sizeof(title) - pos, " %s%s",
			sorted_names[pl_sort_str[0] != 0], pl_sort_str);

	update_window(pl_win, 0, 0, COLS, title, print_pl);
}

static void update_play_queue_window(void)
{
	char title[128];

	snprintf(title, sizeof(title), "Play Queue - %d tracks", pq_nr_tracks);
	if (pq_nr_marked) {
		int pos = strlen(title);

		snprintf(title + pos, sizeof(title) - pos, " (%d marked)", pq_nr_marked);
	}
	update_window(play_queue_win, 0, 0, COLS, title, print_play_queue);
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

	bkgdset(pairs[CURSED_WIN_TITLE]);
	mvaddch(0, tree_win_w, ' ');
	bkgdset(pairs[CURSED_SEPARATOR]);
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
	static char *cont_strs[] = { " ", "C" };
	static char *repeat_strs[] = { " ", "R" };
	static char *shuffle_strs[] = { " ", "S" };
	int play_sorted, buffer_fill, vol, vol_left, vol_right;
	int duration = -1;
	char *msg;
	char format[80];

	lib_lock();
	fopt_set_time(&status_fopts[SF_TOTAL], play_library ? lib.total_time : pl_total_time, 0);
	fopt_set_str(&status_fopts[SF_REPEAT], repeat_strs[repeat]);
	fopt_set_str(&status_fopts[SF_SHUFFLE], shuffle_strs[shuffle]);
	fopt_set_str(&status_fopts[SF_PLAYLISTMODE], aaa_mode_names[lib.aaa_mode]);
	play_sorted = lib.play_sorted;
	lib_unlock();

	if (cur_track_info)
		duration = cur_track_info->duration;

	player_info_lock();

	vol_left = scale_to_percentage(player_info.vol_left, player_info.vol_max);
	vol_right = scale_to_percentage(player_info.vol_right, player_info.vol_max);
	vol = (vol_left + vol_right + 1) / 2;
	buffer_fill = scale_to_percentage(player_info.buffer_fill, player_info.buffer_size);

	fopt_set_str(&status_fopts[SF_STATUS], status_strs[player_info.status]);

	if (show_remaining_time && duration != -1) {
		fopt_set_time(&status_fopts[SF_POSITION], player_info.pos - duration, 0);
	} else {
		fopt_set_time(&status_fopts[SF_POSITION], player_info.pos, 0);
	}

	fopt_set_time(&status_fopts[SF_DURATION], duration, 0);
	fopt_set_int(&status_fopts[SF_VOLUME], vol, 0);
	fopt_set_int(&status_fopts[SF_LVOLUME], vol_left, 0);
	fopt_set_int(&status_fopts[SF_RVOLUME], vol_right, 0);
	fopt_set_int(&status_fopts[SF_BUFFER], buffer_fill, 0);
	fopt_set_str(&status_fopts[SF_CONTINUE], cont_strs[player_cont]);

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

	bkgdset(pairs[CURSED_STATUSLINE]);
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
		if (msg_is_error) {
			bkgdset(pairs[CURSED_ERROR]);
		} else {
			bkgdset(pairs[CURSED_INFO]);
		}
		addstr(error_buf);
		clrtoeol();
		return;
	}
	bkgdset(pairs[CURSED_COMMANDLINE]);
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
	bkgdset(pairs[CURSED_TITLELINE]);
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
	if (!ui_initialized)
		return;

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

	msg_is_error = 0;

	update_commandline();
}

void error_msg(const char *format, ...)
{
	va_list ap;

	if (!display_errors)
		return;

	strcpy(error_buf, "Error: ");
	va_start(ap, format);
	vsnprintf(error_buf + 7, sizeof(error_buf) - 7, format, ap);
	va_end(ap);

	msg_is_error = 1;

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
	bkgdset(pairs[CURSED_INFO]);

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

void set_view(int view)
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

void update_colors(void)
{
	int i;

	if (!ui_initialized)
		return;

	for (i = 0; i < NR_CURSED; i++) {
		int bg = colors[cursed_to_bg_idx[i]];
		int fg = colors[cursed_to_fg_idx[i]];
		int pair = i + 1;

		if (fg >= 8 && fg <= 15) {
			/* fg colors 8..15 are special (0..7 + bold) */
			init_pair(pair, fg & 7, bg);
			pairs[i] = COLOR_PAIR(pair) | (fg & BRIGHT ? A_BOLD : 0);
		} else {
			init_pair(pair, fg, bg);
			pairs[i] = COLOR_PAIR(pair);
		}
	}
	curs_set(0);

	update_view();
	do_update_titleline();
	do_update_statusline();
	do_update_commandline();

	post_update();
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
			char buf[32];

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
			snprintf(buf, sizeof(buf), "%d", cur_track_info->duration);
			argv[i++] = xstrdup("duration");
			argv[i++] = xstrdup(buf);
		}
	}
	argv[i++] = NULL;
	if (spawn(argv, &status) == -1)
		error_msg("couldn't run `%s': %s", status_display_program, strerror(errno));
	for (i = 0; argv[i]; i++)
		free(argv[i]);
}

static int ctrl_c_pressed = 0;

static void sig_int(int sig)
{
	ctrl_c_pressed = 1;
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
	if (player_info.position_changed || player_info.status_changed || player_info.vol_changed) {
		player_info.position_changed = 0;
		player_info.status_changed = 0;
		player_info.vol_changed = 0;

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

	/* total time changed? */
	if (play_library) {
		needs_status_update += lib.sorted_win->changed;
	} else {
		needs_status_update += pl_win->changed;
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

static void handle_ch(uchar ch)
{
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
}

static void handle_key(int key)
{
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

static void u_getch(void)
{
	int key;
	int bit = 7;
	int mask = (1 << 7);
	uchar u, ch;

	key = getch();
	if (key == ERR || key == 0)
		return;

	if (key > 255) {
		handle_key(key);
		return;
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
				return;

			ch = (unsigned char)key;
			u = (u << 6) | (ch & 63);
			count--;
		}
	}
	handle_ch(u);
}

static void main_loop(void)
{
	int rc, fd_high;

	fd_high = remote_socket;
	while (running) {
		fd_set set;
		struct timeval tv;

		update();

		FD_ZERO(&set);
		FD_SET(0, &set);
		FD_SET(remote_socket, &set);

		/* Timeout must be so small that screen updates seem instant.
		 * Only affects changes done in other threads (worker, player).
		 *
		 * Too small timeout makes window updates too fast (wastes CPU).
		 *
		 * Too large timeout makes status line (position) updates too slow.
		 * The timeout is accuracy of player position.
		 */
		tv.tv_sec = 0;
		tv.tv_usec = 100e3;
		rc = select(fd_high + 1, &set, NULL, NULL, &tv);
		if (rc <= 0) {
			if (ctrl_c_pressed) {
				handle_ch(0x03);
				ctrl_c_pressed = 0;
			}
			continue;
		}

		if (FD_ISSET(remote_socket, &set)) {
			/* no error msgs for cmus-remote */
			display_errors = 0;

			remote_server_serve();
		}
		if (FD_ISSET(0, &set)) {
			/* diplay errors for interactive commands */
			display_errors = 1;

			if (using_utf8) {
				u_getch();
			} else {
				int key = getch();

				if (key != ERR && key != 0) {
					if (key > 255) {
						handle_key(key);
					} else {
						handle_ch(key);
					}
				}
			}
		}
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

	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = sig_int;
	sigaction(SIGINT, &act, NULL);

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
		start_color();
		use_default_colors();
	}
	d_print("Number of supported colors: %d\n", COLORS);
	ui_initialized = 1;

	/* this was disabled while initializing because it needs to be
	 * called only once after all colors have been set
	 */
	update_colors();
}

static void init_all(void)
{
	char *term;

	term = getenv("TERM");
	if (term && (strncmp(term, "xterm", 5) == 0 ||
		     strncmp(term, "rxvt", 4) == 0 ||
		     strcmp(term, "screen") == 0))
		update_window_title = 1;

	remote_socket = remote_server_init(server_address);

	/* does not select output plugin */
	player_init(&player_callbacks);

	/* plugins have been loaded so we know what plugin options are available */
	options_add();

	lib_init();
	searchable = tree_searchable;
	pl_init();
	cmus_init();
	browser_init();
	filters_init();
	cmdline_init();
	commands_init();
	search_mode_init();

	/* almost everything must be initialized now */
	options_load();

	/* options have been loaded, init plugins (set their options) */
	player_init_plugins();

	/* finally we can set the output plugin */
	player_set_op(output_plugin);

	player_get_volume(&player_info.vol_left, &player_info.vol_right, &player_info.vol_max);

	lib_autosave_filename = xstrjoin(cmus_config_dir, "/lib.pl");
	pl_autosave_filename = xstrjoin(cmus_config_dir, "/playlist.pl");
	cmus_add(lib_add_track, lib_autosave_filename, FILE_TYPE_PL, JOB_TYPE_LIB);
	cmus_add(pl_add_track, pl_autosave_filename, FILE_TYPE_PL, JOB_TYPE_PL);

	init_curses();
}

static void exit_all(void)
{
	endwin();

	options_exit();

	remote_server_exit();
	cmus_exit();
	cmus_save(lib_for_each, lib_autosave_filename);
	cmus_save(pl_for_each, pl_autosave_filename);

	player_exit();
	lib_exit();
	commands_exit();
	search_mode_exit();
	filters_exit();
	browser_exit();
}

enum {
	FLAG_LISTEN,
	FLAG_PLUGINS,
	FLAG_HELP,
	FLAG_VERSION,
	NR_FLAGS
};

static struct option options[NR_FLAGS + 1] = {
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
	d_print("charset = '%s'\n", charset);

	player_load_plugins();
	if (list_plugins) {
		player_dump_plugins();
		return 0;
	}
	init_all();
	main_loop();
	exit_all();
	return 0;
}
