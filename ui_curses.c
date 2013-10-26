/*
 * Copyright 2008-2013 Various Authors
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "convert.h"
#include "ui_curses.h"
#include "cmdline.h"
#include "search_mode.h"
#include "command_mode.h"
#include "options.h"
#include "play_queue.h"
#include "browser.h"
#include "filters.h"
#include "cmus.h"
#include "player.h"
#include "output.h"
#include "utils.h"
#include "lib.h"
#include "pl.h"
#include "xmalloc.h"
#include "xstrjoin.h"
#include "window.h"
#include "format_print.h"
#include "comment.h"
#include "misc.h"
#include "prog.h"
#include "uchar.h"
#include "spawn.h"
#include "server.h"
#include "keys.h"
#include "debug.h"
#include "help.h"
#include "worker.h"
#include "input.h"
#include "file.h"
#include "path.h"
#include "mixer.h"
#ifdef HAVE_CONFIG
#include "config/curses.h"
#include "config/iconv.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <ctype.h>
#include <dirent.h>
#include <locale.h>
#include <langinfo.h>
#ifdef HAVE_ICONV
#include <iconv.h>
#endif
#include <signal.h>
#include <stdarg.h>
#include <math.h>

#if defined(__sun__) || defined(__CYGWIN__)
/* TIOCGWINSZ */
#include <termios.h>
#include <ncurses.h>
#else
#include <curses.h>
#endif

/* defined in <term.h> but without const */
char *tgetstr(const char *id, char **area);
char *tgoto(const char *cap, int col, int row);

/* globals. documented in ui_curses.h */

int cmus_running = 1;
int ui_initialized = 0;
enum ui_input_mode input_mode = NORMAL_MODE;
int cur_view = TREE_VIEW;
struct searchable *searchable;
char *lib_filename = NULL;
char *lib_ext_filename = NULL;
char *pl_filename = NULL;
char *pl_ext_filename = NULL;
char *play_queue_filename = NULL;
char *play_queue_ext_filename = NULL;
char *charset = NULL;
int using_utf8 = 0;


/* ------------------------------------------------------------------------- */

static char *lib_autosave_filename;
static char *pl_autosave_filename;
static char *play_queue_autosave_filename;

/* shown error message and time stamp
 * error is cleared if it is older than 3s and key was pressed
 */
static char error_buf[512];
static time_t error_time = 0;
/* info messages are displayed in different color */
static int msg_is_error;
static int error_count = 0;

static char *server_address = NULL;

static char print_buffer[512];

/* destination buffer for utf8_encode_to_buf and utf8_decode */
static char conv_buffer[512];

/* one character can take up to 4 bytes in UTF-8 */
#define print_buffer_max_width (sizeof(print_buffer) / 4 - 1)

/* used for messages to the client */
static int client_fd = -1;

static char tcap_buffer[64];
static const char *t_ts;
static const char *t_fs;

static int tree_win_x = 0;
static int tree_win_y = 0;
static int tree_win_w = 0;

static int track_win_x = 0;
static int track_win_y = 0;
static int track_win_w = 0;

static int show_cursor;
static int cursor_x;
static int cursor_y;

static char *title_buf = NULL;

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

static unsigned char cursed_to_attr_idx[NR_CURSED] = {
	COLOR_WIN_ATTR,
	COLOR_WIN_ATTR,
	COLOR_WIN_INACTIVE_SEL_ATTR,
	COLOR_WIN_INACTIVE_CUR_SEL_ATTR,

	COLOR_WIN_ATTR,
	COLOR_WIN_ATTR,
	COLOR_WIN_SEL_ATTR,
	COLOR_WIN_CUR_SEL_ATTR,

	COLOR_WIN_ATTR,
	COLOR_WIN_TITLE_ATTR,
	COLOR_CMDLINE_ATTR,
	COLOR_STATUSLINE_ATTR,

	COLOR_TITLELINE_ATTR,
	COLOR_WIN_ATTR,
	COLOR_CMDLINE_ATTR,
	COLOR_CMDLINE_ATTR
};

/* index is CURSED_*, value is fucking color pair */
static int pairs[NR_CURSED];

enum {
	TF_ALBUMARTIST,
	TF_ARTIST,
	TF_ALBUM,
	TF_DISC,
	TF_TRACK,
	TF_TITLE,
	TF_YEAR,
	TF_ORIGINALYEAR,
	TF_GENRE,
	TF_COMMENT,
	TF_DURATION,
	TF_BITRATE,
	TF_CODEC,
	TF_CODEC_PROFILE,
	TF_PATHFILE,
	TF_FILE,
	TF_RG_TRACK_GAIN,
	TF_RG_TRACK_PEAK,
	TF_RG_ALBUM_GAIN,
	TF_RG_ALBUM_PEAK,
	TF_ARRANGER,
	TF_COMPOSER,
	TF_CONDUCTOR,
	TF_LYRICIST,
	TF_PERFORMER,
	TF_REMIXER,
	TF_LABEL,
	TF_PUBLISHER,
	TF_WORK,
	TF_OPUS,
	TF_PARTNUMBER,
	TF_PART,
	TF_SUBTITLE,
	TF_MEDIA,
	NR_TFS
};

static struct format_option track_fopts[NR_TFS + 1] = {
	DEF_FO_STR('A', "albumartist", 0),
	DEF_FO_STR('a', "artist", 0),
	DEF_FO_STR('l', "album", 0),
	DEF_FO_INT('D', "discnumber", 1),
	DEF_FO_INT('n', "tracknumber", 1),
	DEF_FO_STR('t', "title", 0),
	DEF_FO_STR('y', "date", 1),
	DEF_FO_STR('\0', "originaldate", 1),
	DEF_FO_STR('g', "genre", 0),
	DEF_FO_STR('c', "comment", 0),
	DEF_FO_TIME('d', "duration", 0),
	DEF_FO_INT('\0', "bitrate", 0),
	DEF_FO_STR('\0', "codec", 0),
	DEF_FO_STR('\0', "codec_profile", 0),
	DEF_FO_STR('f', "path", 0),
	DEF_FO_STR('F', "filename", 0),
	DEF_FO_DOUBLE('\0', "rg_track_gain", 0),
	DEF_FO_DOUBLE('\0', "rg_track_peak", 0),
	DEF_FO_DOUBLE('\0', "rg_album_gain", 0),
	DEF_FO_DOUBLE('\0', "rg_album_peak", 0),
	DEF_FO_STR('\0', "arranger", 0),
	DEF_FO_STR('\0', "composer", 0),
	DEF_FO_STR('\0', "conductor", 0),
	DEF_FO_STR('\0', "lyricist", 0),
	DEF_FO_STR('\0', "performer", 0),
	DEF_FO_STR('\0', "remixer", 0),
	DEF_FO_STR('\0', "label", 0),
	DEF_FO_STR('\0', "publisher", 0),
	DEF_FO_STR('\0', "work", 0),
	DEF_FO_STR('\0', "opus", 0),
	DEF_FO_STR('\0', "partnumber", 0),
	DEF_FO_STR('\0', "part", 0),
	DEF_FO_STR('\0', "subtitle", 0),
	DEF_FO_STR('\0', "media", 0),
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
	SF_FOLLOW,
	SF_SHUFFLE,
	SF_PLAYLISTMODE,
	SF_BITRATE,
	NR_SFS
};

static struct format_option status_fopts[NR_SFS + 1] = {
	DEF_FO_STR('s', NULL, 0),
	DEF_FO_TIME('p', NULL, 0),
	DEF_FO_TIME('d', NULL, 0),
	DEF_FO_TIME('t', NULL, 0),
	DEF_FO_INT('v', NULL, 0),
	DEF_FO_INT('l', NULL, 0),
	DEF_FO_INT('r', NULL, 0),
	DEF_FO_INT('b', NULL, 0),
	DEF_FO_STR('R', NULL, 0),
	DEF_FO_STR('C', NULL, 0),
	DEF_FO_STR('F', NULL, 0),
	DEF_FO_STR('S', NULL, 0),
	DEF_FO_STR('L', NULL, 0),
	DEF_FO_INT('B', NULL, 0),
	DEF_FO_END
};

int track_format_valid(const char *format)
{
	return format_valid(format, track_fopts);
}

static void utf8_encode_to_buf(const char *buffer)
{
	int n;
#ifdef HAVE_ICONV
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
			goto fallback;
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
		goto fallback;
	}
	return;
fallback:
#endif
	n = min(sizeof(conv_buffer) - 1, strlen(buffer));
	memmove(conv_buffer, buffer, n);
	conv_buffer[n] = '\0';
}

static void utf8_decode(const char *buffer)
{
	int n;
#ifdef HAVE_ICONV
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
			goto fallback;
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
		goto fallback;
	}
	return;
fallback:
#endif
	n = u_to_ascii(conv_buffer, buffer, sizeof(conv_buffer) - 1);
	conv_buffer[n] = '\0';
}

/* screen updates {{{ */

static void dump_print_buffer(int row, int col)
{
	if (using_utf8) {
		(void) mvaddstr(row, col, print_buffer);
	} else {
		utf8_decode(print_buffer);
		(void) mvaddstr(row, col, conv_buffer);
	}
}

/* print @str into @buf
 *
 * if @str is shorter than @width pad with spaces
 * if @str is wider than @width truncate and add "..."
 */
static int format_str(char *buf, const char *str, int width)
{
	int s = 0, d = 0, ellipsis_pos = 0, cut_double_width = 0;

	while (1) {
		uchar u;
		int w;

		u = u_get_char(str, &s);
		if (u == 0) {
			memset(buf + d, ' ', width);
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
				buf[d - 1] = ' ';
			}
			buf[d++] = '.';
			buf[d++] = '.';
			buf[d++] = '.';
			break;
		}
		u_set_char(buf, &d, u);
	}
	return d;
}

static void sprint(int row, int col, const char *str, int width)
{
	int pos = 0;

	print_buffer[pos++] = ' ';
	pos += format_str(print_buffer + pos, str, width - 2);
	print_buffer[pos++] = ' ';
	print_buffer[pos] = 0;
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
	(void) mvaddstr(row, col, print_buffer);
}

static void print_tree(struct window *win, int row, struct iter *iter)
{
	const char *str;
	struct artist *artist;
	struct album *album;
	struct iter sel;
	int current, selected, active, pos;

	artist = iter_to_artist(iter);
	album = iter_to_album(iter);
	current = 0;
	if (lib_cur_track) {
		if (album) {
			current = CUR_ALBUM == album;
		} else {
			current = CUR_ARTIST == artist;
		}
	}
	window_get_sel(win, &sel);
	selected = iters_equal(iter, &sel);
	active = lib_cur_win == lib_tree_win;
	bkgdset(pairs[(active << 2) | (selected << 1) | current]);

	if (active && selected) {
		cursor_x = 0;
		cursor_y = 1 + row;
	}

	pos = 0;
	print_buffer[pos++] = ' ';
	if (album) {
		print_buffer[pos++] = ' ';
		print_buffer[pos++] = ' ';
		str = album->name;
	} else {
		if (display_artist_sort_name)
			str = artist_sort_name(artist);
		else
			str = artist->name;
	}
	pos += format_str(print_buffer + pos, str, tree_win_w - pos - 1);
	print_buffer[pos++] = ' ';
	print_buffer[pos++] = 0;
	dump_print_buffer(tree_win_y + row + 1, tree_win_x);
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

static inline void fopt_set_double(struct format_option *fopt, double value, int empty)
{
	BUG_ON(fopt->type != FO_DOUBLE);
	fopt->fo_double = value;
	fopt->empty = empty;
}

static inline void fopt_set_time(struct format_option *fopt, int value, int empty)
{
	BUG_ON(fopt->type != FO_TIME);
	fopt->fo_time = value;
	fopt->empty = empty;
}

static void fill_track_fopts_track_info(struct track_info *info)
{
	char *filename;

	if (using_utf8) {
		filename = info->filename;
	} else {
		utf8_encode_to_buf(info->filename);
		filename = conv_buffer;
	}

	fopt_set_str(&track_fopts[TF_ALBUMARTIST], info->albumartist);
	fopt_set_str(&track_fopts[TF_ARTIST], info->artist);
	fopt_set_str(&track_fopts[TF_ALBUM], info->album);
	fopt_set_int(&track_fopts[TF_DISC], info->discnumber, info->discnumber == -1);
	fopt_set_int(&track_fopts[TF_TRACK], info->tracknumber, info->tracknumber == -1);
	fopt_set_str(&track_fopts[TF_TITLE], info->title);
	fopt_set_str(&track_fopts[TF_YEAR], keyvals_get_val(info->comments, "date"));
	fopt_set_str(&track_fopts[TF_GENRE], info->genre);
	fopt_set_str(&track_fopts[TF_COMMENT], info->comment);
	fopt_set_time(&track_fopts[TF_DURATION], info->duration, info->duration == -1);
	fopt_set_double(&track_fopts[TF_RG_TRACK_GAIN], info->rg_track_gain, isnan(info->rg_track_gain));
	fopt_set_double(&track_fopts[TF_RG_TRACK_PEAK], info->rg_track_peak, isnan(info->rg_track_peak));
	fopt_set_double(&track_fopts[TF_RG_ALBUM_GAIN], info->rg_album_gain, isnan(info->rg_album_gain));
	fopt_set_double(&track_fopts[TF_RG_ALBUM_PEAK], info->rg_album_peak, isnan(info->rg_album_peak));
	fopt_set_str(&track_fopts[TF_ORIGINALYEAR], keyvals_get_val(info->comments, "originaldate"));
	fopt_set_int(&track_fopts[TF_BITRATE], (int) (info->bitrate / 1000. + 0.5), info->bitrate == -1);
	fopt_set_str(&track_fopts[TF_CODEC], info->codec);
	fopt_set_str(&track_fopts[TF_CODEC_PROFILE], info->codec_profile);
	fopt_set_str(&track_fopts[TF_PATHFILE], filename);
	fopt_set_str(&track_fopts[TF_ARRANGER], keyvals_get_val(info->comments, "arranger"));
	fopt_set_str(&track_fopts[TF_COMPOSER], keyvals_get_val(info->comments, "composer"));
	fopt_set_str(&track_fopts[TF_CONDUCTOR], keyvals_get_val(info->comments, "conductor"));
	fopt_set_str(&track_fopts[TF_LYRICIST], keyvals_get_val(info->comments, "lyricist"));
	fopt_set_str(&track_fopts[TF_PERFORMER], keyvals_get_val(info->comments, "performer"));
	fopt_set_str(&track_fopts[TF_REMIXER], keyvals_get_val(info->comments, "remixer"));
	fopt_set_str(&track_fopts[TF_LABEL], keyvals_get_val(info->comments, "label"));
	fopt_set_str(&track_fopts[TF_PUBLISHER], keyvals_get_val(info->comments, "publisher"));
	fopt_set_str(&track_fopts[TF_WORK], keyvals_get_val(info->comments, "work"));
	fopt_set_str(&track_fopts[TF_OPUS], keyvals_get_val(info->comments, "opus"));
	fopt_set_str(&track_fopts[TF_PARTNUMBER], keyvals_get_val(info->comments, "partnumber"));
	fopt_set_str(&track_fopts[TF_PART], keyvals_get_val(info->comments, "part"));
	fopt_set_str(&track_fopts[TF_SUBTITLE], keyvals_get_val(info->comments, "subtitle"));
	fopt_set_str(&track_fopts[TF_MEDIA], info->media);
	if (is_http_url(info->filename)) {
		fopt_set_str(&track_fopts[TF_FILE], filename);
	} else {
		fopt_set_str(&track_fopts[TF_FILE], path_basename(filename));
	}
}

static void print_track(struct window *win, int row, struct iter *iter)
{
	struct tree_track *track;
	struct track_info *ti;
	struct iter sel;
	int current, selected, active;
	const char *format;

	track = iter_to_tree_track(iter);
	current = lib_cur_track == track;
	window_get_sel(win, &sel);
	selected = iters_equal(iter, &sel);
	active = lib_cur_win == lib_track_win;
	bkgdset(pairs[(active << 2) | (selected << 1) | current]);

	if (active && selected) {
		cursor_x = track_win_x;
		cursor_y = 1 + row;
	}

	ti = tree_track_info(track);
	fill_track_fopts_track_info(ti);

	if (track_info_has_tag(ti)) {
		if (track_is_compilation(ti->comments))
			format = track_win_format_va;
		else
			format = track_win_format;

		format_print(print_buffer, track_win_w, format, track_fopts);
	} else {
		format_print(print_buffer, track_win_w, track_win_alt_format, track_fopts);
	}
	dump_print_buffer(track_win_y + row + 1, track_win_x);
}

/* used by print_editable only */
static struct simple_track *current_track;

static void print_editable(struct window *win, int row, struct iter *iter)
{
	struct simple_track *track;
	struct iter sel;
	int current, selected, active;
	const char *format;

	track = iter_to_simple_track(iter);
	current = current_track == track;
	window_get_sel(win, &sel);
	selected = iters_equal(iter, &sel);

	if (selected) {
		cursor_x = 0;
		cursor_y = 1 + row;
	}

	active = 1;
	if (!selected && track->marked) {
		selected = 1;
		active = 0;
	}

	bkgdset(pairs[(active << 2) | (selected << 1) | current]);

	fill_track_fopts_track_info(track->info);

	if (track_info_has_tag(track->info)) {
		if (track_is_compilation(track->info->comments))
			format = list_win_format_va;
		else
			format = list_win_format;

		format_print(print_buffer, COLS, format, track_fopts);
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

	if (selected) {
		cursor_x = 0;
		cursor_y = 1 + row;
	}

	/* file name encoding == terminal encoding. no need to convert */
	if (using_utf8) {
		sprint(row + 1, 0, e->name, COLS);
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
	int current = !!e->act_stat;
	const char stat_chars[3] = " *!";
	int ch1, ch2, ch3, pos;
	const char *e_filter;

	window_get_sel(win, &sel);
	selected = iters_equal(iter, &sel);
	bkgdset(pairs[(active << 2) | (selected << 1) | current]);

	if (selected) {
		cursor_x = 0;
		cursor_y = 1 + row;
	}

	ch1 = ' ';
	ch3 = ' ';
	if (e->sel_stat != e->act_stat) {
		ch1 = '[';
		ch3 = ']';
	}
	ch2 = stat_chars[e->sel_stat];

	e_filter = e->filter;
	if (!using_utf8) {
		utf8_encode_to_buf(e_filter);
		e_filter = conv_buffer;
	}

	snprintf(buf, sizeof(buf), "%c%c%c%-15s  %s", ch1, ch2, ch3, e->name, e_filter);
	pos = format_str(print_buffer, buf, COLS - 1);
	print_buffer[pos++] = ' ';
	print_buffer[pos] = 0;
	dump_print_buffer(row + 1, 0);
}

static void print_help(struct window *win, int row, struct iter *iter)
{
	struct iter sel;
	int selected;
	int pos;
	int active = 1;
	char buf[512];
	const struct help_entry *e = iter_to_help_entry(iter);
	const struct cmus_opt *opt;

	window_get_sel(win, &sel);
	selected = iters_equal(iter, &sel);
	bkgdset(pairs[(active << 2) | (selected << 1)]);

	if (selected) {
		cursor_x = 0;
		cursor_y = 1 + row;
	}

	switch (e->type) {
	case HE_TEXT:
		snprintf(buf, sizeof(buf), " %s", e->text);
		break;
	case HE_BOUND:
		snprintf(buf, sizeof(buf), " %-8s %-14s %s",
			key_context_names[e->binding->ctx],
			e->binding->key->name,
			e->binding->cmd);
		break;
	case HE_UNBOUND:
		snprintf(buf, sizeof(buf), " %s", e->command->name);
		break;
	case HE_OPTION:
		opt = e->option;
		snprintf(buf, sizeof(buf), " %-29s ", opt->name);
		opt->get(opt->id, buf + strlen(buf));
		break;
	}
	pos = format_str(print_buffer, buf, COLS - 1);
	print_buffer[pos++] = ' ';
	print_buffer[pos] = 0;
	dump_print_buffer(row + 1, 0);
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
	update_window(lib_tree_win, tree_win_x, tree_win_y,
			tree_win_w, "Artist / Album", print_tree);
}

static void update_track_window(void)
{
	char title[512];

	/* it doesn't matter what format options we use because the format
	 * string does not contain any format charaters */
	format_print(title, track_win_w - 2, "Track%=Library", track_fopts);
	update_window(lib_track_win, track_win_x, track_win_y,
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

static void update_editable_window(struct editable *e, const char *title, const char *filename)
{
	char buf[512];
	int pos;

	if (filename) {
		if (using_utf8) {
			/* already UTF-8 */
		} else {
			utf8_encode_to_buf(filename);
			filename = conv_buffer;
		}
		snprintf(buf, sizeof(buf), "%s %s - %d tracks", title,
				pretty(filename), e->nr_tracks);
	} else {
		snprintf(buf, sizeof(buf), "%s - %d tracks", title, e->nr_tracks);
	}

	if (e->nr_marked) {
		pos = strlen(buf);
		snprintf(buf + pos, sizeof(buf) - pos, " (%d marked)", e->nr_marked);
	}
	pos = strlen(buf);
	snprintf(buf + pos, sizeof(buf) - pos, " %s%s",
			sorted_names[e->sort_str[0] != 0], e->sort_str);

	update_window(e->win, 0, 0, COLS, buf, &print_editable);
}

static void update_sorted_window(void)
{
	current_track = (struct simple_track *)lib_cur_track;
	update_editable_window(&lib_editable, "Library", lib_filename);
}

static void update_pl_window(void)
{
	current_track = pl_cur_track;
	update_editable_window(&pl_editable, "Playlist", pl_filename);
}

static void update_play_queue_window(void)
{
	current_track = NULL;
	update_editable_window(&pq_editable, "Play Queue", NULL);
}

static void update_browser_window(void)
{
	char title[512];
	char *dirname;

	if (using_utf8) {
		/* already UTF-8 */
		dirname = browser_dir;
	} else {
		utf8_encode_to_buf(browser_dir);
		dirname = conv_buffer;
	}
	snprintf(title, sizeof(title), "Browser - %s", dirname);
	update_window(browser_win, 0, 0, COLS, title, print_browser);
}

static void update_filters_window(void)
{
	update_window(filters_win, 0, 0, COLS, "Library Filters", print_filter);
}

static void update_help_window(void)
{
	update_window(help_win, 0, 0, COLS, "Settings", print_help);
}

static void draw_separator(void)
{
	int row;

	bkgdset(pairs[CURSED_WIN_TITLE]);
	(void) mvaddch(0, tree_win_w, ' ');
	bkgdset(pairs[CURSED_SEPARATOR]);
	for (row = 1; row < LINES - 3; row++)
		(void) mvaddch(row, tree_win_w, ACS_VLINE);
}

static void do_update_view(int full)
{
	cursor_x = -1;
	cursor_y = -1;

	switch (cur_view) {
	case TREE_VIEW:
		editable_lock();
		if (full || lib_tree_win->changed)
			update_tree_window();
		if (full || lib_track_win->changed)
			update_track_window();
		editable_unlock();
		draw_separator();
		update_filterline();
		break;
	case SORTED_VIEW:
		editable_lock();
		update_sorted_window();
		editable_unlock();
		update_filterline();
		break;
	case PLAYLIST_VIEW:
		editable_lock();
		update_pl_window();
		editable_unlock();
		break;
	case QUEUE_VIEW:
		editable_lock();
		update_play_queue_window();
		editable_unlock();
		break;
	case BROWSER_VIEW:
		update_browser_window();
		break;
	case FILTERS_VIEW:
		update_filters_window();
		break;
	case HELP_VIEW:
		update_help_window();
		break;
	}
}

static void do_update_statusline(void)
{
	static const char *status_strs[] = { ".", ">", "|" };
	static const char *cont_strs[] = { " ", "C" };
	static const char *follow_strs[] = { " ", "F" };
	static const char *repeat_strs[] = { " ", "R" };
	static const char *shuffle_strs[] = { " ", "S" };
	int buffer_fill, vol, vol_left, vol_right;
	int duration = -1;
	char *msg;
	char format[80];

	editable_lock();
	fopt_set_time(&status_fopts[SF_TOTAL], play_library ? lib_editable.total_time :
			pl_editable.total_time, 0);
	editable_unlock();

	fopt_set_str(&status_fopts[SF_FOLLOW], follow_strs[follow]);
	fopt_set_str(&status_fopts[SF_REPEAT], repeat_strs[repeat]);
	fopt_set_str(&status_fopts[SF_SHUFFLE], shuffle_strs[shuffle]);
	fopt_set_str(&status_fopts[SF_PLAYLISTMODE], aaa_mode_names[aaa_mode]);

	player_info_lock();

	if (player_info.ti)
		duration = player_info.ti->duration;

	vol_left = vol_right = vol = -1;
	if (soft_vol) {
		vol_left = soft_vol_l;
		vol_right = soft_vol_r;
		vol = (vol_left + vol_right + 1) / 2;
	} else if (volume_max && volume_l >= 0 && volume_r >= 0) {
		vol_left = scale_to_percentage(volume_l, volume_max);
		vol_right = scale_to_percentage(volume_r, volume_max);
		vol = (vol_left + vol_right + 1) / 2;
	}
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
	fopt_set_int(&status_fopts[SF_BITRATE], player_info.current_bitrate / 1000. + 0.5, 0);

	if (show_playback_position) {
		strcpy(format, " %s %p ");
		if (duration != -1)
			strcat(format, "/ %d ");
	} else {
		strcpy(format, " %s ");
		if (duration != -1)
			strcat(format, "%d ");
	}
	strcat(format, "- %t ");
	if (vol >= 0) {
		if (vol_left != vol_right) {
			strcat(format, "vol: %l,%r ");
		} else {
			strcat(format, "vol: %v ");
		}
	}
	if (player_info.ti) {
		if (is_http_url(player_info.ti->filename))
			strcat(format, "buf: %b ");
		if (show_current_bitrate && player_info.current_bitrate >= 0)
			strcat(format, " %B kbps ");
	}
	strcat(format, "%=");
	if (player_repeat_current) {
		strcat(format, "repeat current");
	} else if (play_library) {
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
	strcat(format, " | %1C%1F%1R%1S ");
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
	char *str;
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

	str = cmdline.line;
	if (!using_utf8) {
		/* cmdline.line actually pretends to be UTF-8 but all non-ASCII
		 * characters are invalid UTF-8 so it really is in locale's
		 * encoding.
		 *
		 * This code should be safe because cmdline.bpos ==
		 * cmdline.cpos as every non-ASCII character is counted as one
		 * invalid UTF-8 byte.
		 *
		 * NOTE: This has nothing to do with widths of printed
		 * characters.  I.e. even if there were control characters
		 * (displayed as <xx>) there would be no problem because bpos
		 * still equals to cpos, I think.
		 */
		utf8_encode_to_buf(cmdline.line);
		str = conv_buffer;
	}

	/* COMMAND_MODE or SEARCH_MODE */
	w = u_str_width(str);
	ch = ':';
	if (input_mode == SEARCH_MODE)
		ch = search_direction == SEARCH_FORWARD ? '/' : '?';

	if (w <= COLS - 2) {
		addch(ch);
		idx = u_copy_chars(print_buffer, str, &w);
		print_buffer[idx] = 0;
		dump_buffer(print_buffer);
		clrtoeol();
	} else {
		/* keep cursor as far right as possible */
		int skip, width, cw;

		/* cursor pos (width, not chars. doesn't count the ':') */
		cw = u_str_nwidth(str, cmdline.cpos);

		skip = cw + 2 - COLS;
		if (skip > 0) {
			/* skip the ':' */
			skip--;

			/* skip rest (if any) */
			idx = u_skip_chars(str, &skip);

			width = COLS;
			idx = u_copy_chars(print_buffer, str + idx, &width);
			while (width < COLS) {
				/* cursor is at end of the buffer
				 * print 1, 2 or 3 spaces
				 *
				 * To clarify:
				 *
				 * If the last _skipped_ character was double-width we may need
				 * to print 2 spaces.
				 *
				 * If the last _skipped_ character was invalid UTF-8 we may need
				 * to print 3 spaces.
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
			idx = u_copy_chars(print_buffer, str, &width);
			print_buffer[idx] = 0;
			dump_buffer(print_buffer);
		}
	}
}

static void set_title(const char *title)
{
	if (!set_term_title)
		return;

	if (t_ts) {
		printf("%s%s%s", tgoto(t_ts, 0, 0), title, t_fs);
		fflush(stdout);
	}
}

static void do_update_titleline(void)
{
	bkgdset(pairs[CURSED_TITLELINE]);
	player_info_lock();
	if (player_info.ti) {
		int i, use_alt_format = 0;
		char *wtitle;

		fill_track_fopts_track_info(player_info.ti);

		use_alt_format = !track_info_has_tag(player_info.ti);

		if (is_http_url(player_info.ti->filename)) {
			const char *title = get_stream_title();

			if (title != NULL) {
				free(title_buf);
				title_buf = to_utf8(title, icecast_default_charset);
				/*
				 * StreamTitle overrides radio station name
				 */
				use_alt_format = 0;
				fopt_set_str(&track_fopts[TF_TITLE], title_buf);
			}
		}

		if (use_alt_format) {
			format_print(print_buffer, COLS, current_alt_format, track_fopts);
		} else {
			format_print(print_buffer, COLS, current_format, track_fopts);
		}
		dump_print_buffer(LINES - 3, 0);

		/* set window title */
		if (use_alt_format) {
			format_print(print_buffer, print_buffer_max_width,
					window_title_alt_format, track_fopts);
		} else {
			format_print(print_buffer,  print_buffer_max_width,
					window_title_format, track_fopts);
		}

		/* remove whitespace */
		i = strlen(print_buffer) - 1;
		while (i > 0 && print_buffer[i] == ' ')
			i--;
		print_buffer[i + 1] = 0;

		if (using_utf8) {
			wtitle = print_buffer;
		} else {
			utf8_decode(print_buffer);
			wtitle = conv_buffer;
		}

		set_title(wtitle);
	} else {
		move(LINES - 3, 0);
		clrtoeol();

		set_title("cmus " VERSION);
	}
	player_info_unlock();
}

static int cmdline_cursor_column(void)
{
	char *str;
	int cw, skip, s;

	str = cmdline.line;
	if (!using_utf8) {
		/* see do_update_commandline */
		utf8_encode_to_buf(cmdline.line);
		str = conv_buffer;
	}

	/* width of the text in the buffer before cursor */
	cw = u_str_nwidth(str, cmdline.cpos);

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
	u_skip_chars(str, &s);
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
		if (cursor_x >= 0) {
			move(cursor_y, cursor_x);
		} else {
			move(LINES - 1, 0);
		}
		refresh();

		/* visible cursor is useful for screen readers */
		if (show_cursor) {
			curs_set(1);
		} else {
			curs_set(0);
		}
	}
}

/* lock player_info! */
const char *get_stream_title(void)
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

void update_titleline(void)
{
	curs_set(0);
	do_update_titleline();
	post_update();
}

void update_full(void)
{
	if (!ui_initialized)
		return;

	curs_set(0);

	do_update_view(1);
	do_update_titleline();
	do_update_statusline();
	do_update_commandline();

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

void update_filterline(void)
{
	if (cur_view != TREE_VIEW && cur_view != SORTED_VIEW)
		return;
	if (lib_live_filter) {
		char buf[512];
		int w;
		bkgdset(pairs[CURSED_STATUSLINE]);
		snprintf(buf, sizeof(buf), "filtered: %s", lib_live_filter);
		w = clamp(strlen(buf) + 2, COLS/4, COLS/2);
		sprint(LINES-4, COLS-w, buf, w);
	}
}

void info_msg(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vsnprintf(error_buf, sizeof(error_buf), format, ap);
	va_end(ap);

	if (client_fd != -1) {
		write_all(client_fd, error_buf, strlen(error_buf));
		write_all(client_fd, "\n", 1);
	}

	msg_is_error = 0;

	update_commandline();
}

void error_msg(const char *format, ...)
{
	va_list ap;

	strcpy(error_buf, "Error: ");
	va_start(ap, format);
	vsnprintf(error_buf + 7, sizeof(error_buf) - 7, format, ap);
	va_end(ap);

	d_print("%s\n", error_buf);
	if (client_fd != -1) {
		write_all(client_fd, error_buf, strlen(error_buf));
		write_all(client_fd, "\n", 1);
	}

	msg_is_error = 1;
	error_count++;

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
		case HELP_VIEW:
			what = "Binding/command/option";
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
		case HELP_VIEW:
			what = "Binding/command/option";
			break;
		}
	}
	info_msg("%s not found: %s", what, search_str ? search_str : "");
}

void set_client_fd(int fd)
{
	client_fd = fd;
}

int get_client_fd(void)
{
	return client_fd;
}

void set_view(int view)
{
	if (view == cur_view)
		return;

	cur_view = view;
	switch (cur_view) {
	case TREE_VIEW:
		searchable = tree_searchable;
		break;
	case SORTED_VIEW:
		searchable = lib_editable.searchable;
		break;
	case PLAYLIST_VIEW:
		searchable = pl_editable.searchable;
		break;
	case QUEUE_VIEW:
		searchable = pq_editable.searchable;
		break;
	case BROWSER_VIEW:
		searchable = browser_searchable;
		break;
	case FILTERS_VIEW:
		searchable = filters_searchable;
		break;
	case HELP_VIEW:
		searchable = help_searchable;
		update_help_window();
		break;
	}

	curs_set(0);
	do_update_view(1);
	post_update();
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

void update_colors(void)
{
	int i;

	if (!ui_initialized)
		return;

	for (i = 0; i < NR_CURSED; i++) {
		int bg = colors[cursed_to_bg_idx[i]];
		int fg = colors[cursed_to_fg_idx[i]];
		int attr = attrs[cursed_to_attr_idx[i]];
		int pair = i + 1;

		if (fg >= 8 && fg <= 15) {
			/* fg colors 8..15 are special (0..7 + bold) */
			init_pair(pair, fg & 7, bg);
			pairs[i] = COLOR_PAIR(pair) | (fg & BRIGHT ? A_BOLD : 0) | attr;
		} else {
			init_pair(pair, fg, bg);
			pairs[i] = COLOR_PAIR(pair) | attr;
		}
	}
}

static void clear_error(void)
{
	time_t t = time(NULL);

	/* prevent accidental clearing of error messages */
	if (t - error_time < 2)
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
	enum player_status status;
	const char *stream_title = NULL;
	char *argv[32];
	int i;

	if (status_display_program == NULL || status_display_program[0] == 0)
		return;

	player_info_lock();
	status = player_info.status;
	if (status == PLAYER_STATUS_PLAYING && player_info.ti && is_http_url(player_info.ti->filename))
		stream_title = get_stream_title();

	i = 0;
	argv[i++] = xstrdup(status_display_program);

	argv[i++] = xstrdup("status");
	argv[i++] = xstrdup(player_status_names[status]);
	if (player_info.ti) {
		static const char *keys[] = {
			"artist", "album", "discnumber", "tracknumber", "title", "date",
			"musicbrainz_trackid", NULL
		};
		int j;

		if (is_http_url(player_info.ti->filename)) {
			argv[i++] = xstrdup("url");
		} else {
			argv[i++] = xstrdup("file");
		}
		argv[i++] = xstrdup(player_info.ti->filename);

		if (track_info_has_tag(player_info.ti)) {
			for (j = 0; keys[j]; j++) {
				const char *key = keys[j];
				const char *val;

				if (strcmp(key, "title") == 0 && stream_title)
					/*
					 * StreamTitle overrides radio station name
					 */
					val = stream_title;
				else
					val = keyvals_get_val(player_info.ti->comments, key);

				if (val) {
					argv[i++] = xstrdup(key);
					argv[i++] = xstrdup(val);
				}
			}
			if (player_info.ti->duration > 0) {
				char buf[32];
				snprintf(buf, sizeof(buf), "%d", player_info.ti->duration);
				argv[i++] = xstrdup("duration");
				argv[i++] = xstrdup(buf);
			}
		} else if (stream_title) {
			argv[i++] = xstrdup("title");
			argv[i++] = xstrdup(stream_title);
		}
	}
	argv[i++] = NULL;
	player_info_unlock();

	if (spawn(argv, NULL, 0) == -1)
		error_msg("couldn't run `%s': %s", status_display_program, strerror(errno));
	for (i = 0; argv[i]; i++)
		free(argv[i]);
}

static int ctrl_c_pressed = 0;

static void sig_int(int sig)
{
	ctrl_c_pressed = 1;
}

static void sig_shutdown(int sig)
{
	cmus_running = 0;
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

static void resize_tree_view(int w, int h)
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
	window_set_nr_rows(lib_tree_win, h);
	window_set_nr_rows(lib_track_win, h);
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
#if HAVE_RESIZETERM
			resizeterm(lines, columns);
#endif
			w = COLS;
			h = LINES - 3;
			if (w < 16)
				w = 16;
			if (h < 8)
				h = 8;
			editable_lock();
			resize_tree_view(w, h);
			window_set_nr_rows(lib_editable.win, h - 1);
			window_set_nr_rows(pl_editable.win, h - 1);
			window_set_nr_rows(pq_editable.win, h - 1);
			window_set_nr_rows(filters_win, h - 1);
			window_set_nr_rows(help_win, h - 1);
			window_set_nr_rows(browser_win, h - 1);
			editable_unlock();
			needs_title_update = 1;
			needs_status_update = 1;
			needs_command_update = 1;
		}
		clearok(curscr, TRUE);
		refresh();
	}

	player_info_lock();
	editable_lock();

	needs_spawn = player_info.status_changed || player_info.file_changed ||
		player_info.metadata_changed;

	if (player_info.file_changed) {
		player_info.file_changed = 0;
		needs_title_update = 1;
		needs_status_update = 1;
	}
	if (player_info.metadata_changed) {
		player_info.metadata_changed = 0;
		needs_title_update = 1;
	}
	if (player_info.position_changed || player_info.status_changed) {
		player_info.position_changed = 0;
		player_info.status_changed = 0;

		needs_status_update = 1;
	}
	switch (cur_view) {
	case TREE_VIEW:
		needs_view_update += lib_tree_win->changed || lib_track_win->changed;
		break;
	case SORTED_VIEW:
		needs_view_update += lib_editable.win->changed;
		break;
	case PLAYLIST_VIEW:
		needs_view_update += pl_editable.win->changed;
		break;
	case QUEUE_VIEW:
		needs_view_update += pq_editable.win->changed;
		break;
	case BROWSER_VIEW:
		needs_view_update += browser_win->changed;
		break;
	case FILTERS_VIEW:
		needs_view_update += filters_win->changed;
		break;
	case HELP_VIEW:
		needs_view_update += help_win->changed;
		break;
	}

	/* total time changed? */
	if (play_library) {
		needs_status_update += lib_editable.win->changed;
		lib_editable.win->changed = 0;
	} else {
		needs_status_update += pl_editable.win->changed;
		pl_editable.win->changed = 0;
	}

	editable_unlock();
	player_info_unlock();

	if (needs_spawn)
		spawn_status_program();

	if (needs_view_update || needs_title_update || needs_status_update || needs_command_update) {
		curs_set(0);

		if (needs_view_update)
			do_update_view(0);
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

static void handle_escape(int c)
{
	clear_error();
	if (input_mode == NORMAL_MODE) {
		normal_mode_ch(c + 128);
	} else if (input_mode == COMMAND_MODE) {
		command_mode_escape(c);
		update_commandline();
	} else if (input_mode == SEARCH_MODE) {
		search_mode_escape(c);
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

	/* escape sequence */
	if (key == 0x1B) {
		int e_key = getch();
		if (e_key != ERR && e_key != 0) {
			handle_escape(e_key);
			return;
		}
	}

	ch = (unsigned char)key;
	while (bit > 0 && ch & mask) {
		mask >>= 1;
		bit--;
	}
	if (bit == 7) {
		/* ascii */
		u = ch;
	} else if (using_utf8) {
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
	} else
		u = ch | U_INVALID_MASK;
	handle_ch(u);
}

static void main_loop(void)
{
	int rc, fd_high;

	fd_high = server_socket;
	while (cmus_running) {
		fd_set set;
		struct timeval tv;
		int poll_mixer = 0;
		int i, nr_fds = 0;
		int fds[NR_MIXER_FDS];
		struct list_head *item;
		struct client *client;

		update();

		/* Timeout must be so small that screen updates seem instant.
		 * Only affects changes done in other threads (worker, player).
		 *
		 * Too small timeout makes window updates too fast (wastes CPU).
		 *
		 * Too large timeout makes status line (position) updates too slow.
		 * The timeout is accuracy of player position.
		 */
		tv.tv_sec = 0;
		tv.tv_usec = 0;

		player_info_lock();
		if (player_info.status == PLAYER_STATUS_PLAYING) {
			// player position updates need to be fast
			tv.tv_usec = 100e3;
		}
		player_info_unlock();

		if (!tv.tv_usec && worker_has_job(JOB_TYPE_ANY)) {
			// playlist is loading. screen needs to be updated
			tv.tv_usec = 250e3;
		}

		FD_ZERO(&set);
		FD_SET(0, &set);
		FD_SET(server_socket, &set);
		list_for_each_entry(client, &client_head, node) {
			FD_SET(client->fd, &set);
			if (client->fd > fd_high)
				fd_high = client->fd;
		}
		if (!soft_vol) {
			nr_fds = mixer_get_fds(fds);
			if (nr_fds <= 0) {
				poll_mixer = 1;
				if (!tv.tv_usec)
					tv.tv_usec = 500e3;
			}
			for (i = 0; i < nr_fds; i++) {
				BUG_ON(fds[i] <= 0);
				FD_SET(fds[i], &set);
				if (fds[i] > fd_high)
					fd_high = fds[i];
			}
		}

		if (tv.tv_usec) {
			rc = select(fd_high + 1, &set, NULL, NULL, &tv);
		} else {
			rc = select(fd_high + 1, &set, NULL, NULL, NULL);
		}
		if (poll_mixer) {
			int ol = volume_l;
			int or = volume_r;

			mixer_read_volume();
			if (ol != volume_l || or != volume_r)
				update_statusline();

		}
		if (rc <= 0) {
			if (ctrl_c_pressed) {
				handle_ch(0x03);
				ctrl_c_pressed = 0;
			}

			continue;
		}

		for (i = 0; i < nr_fds; i++) {
			if (FD_ISSET(fds[i], &set)) {
				d_print("vol changed\n");
				mixer_read_volume();
				update_statusline();
			}
		}
		if (FD_ISSET(server_socket, &set))
			server_accept();

		// server_serve() can remove client from the list
		item = client_head.next;
		while (item != &client_head) {
			struct list_head *next = item->next;
			client = container_of(item, struct client, node);
			if (FD_ISSET(client->fd, &set))
				server_serve(client);
			item = next;
		}

		if (FD_ISSET(0, &set))
			u_getch();
	}
}

static int get_next(struct track_info **ti)
{
	struct track_info *info;

	editable_lock();
	info = play_queue_remove();
	if (info == NULL) {
		if (play_library) {
			info = lib_set_next();
		} else {
			info = pl_set_next();
		}
	}
	editable_unlock();

	if (info == NULL)
		return -1;

	*ti = info;
	return 0;
}

static const struct player_callbacks player_callbacks = {
	.get_next = get_next
};

static void init_curses(void)
{
	struct sigaction act;
	char *ptr, *term;

	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = sig_int;
	sigaction(SIGINT, &act, NULL);

	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = sig_shutdown;
	sigaction(SIGHUP, &act, NULL);
	sigaction(SIGTERM, &act, NULL);

	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &act, NULL);

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
#if HAVE_USE_DEFAULT_COLORS
		start_color();
		use_default_colors();
#endif
	}
	d_print("Number of supported colors: %d\n", COLORS);
	ui_initialized = 1;

	/* this was disabled while initializing because it needs to be
	 * called only once after all colors have been set
	 */
	update_colors();

	ptr = tcap_buffer;
	t_ts = tgetstr("ts", &ptr);
	t_fs = tgetstr("fs", &ptr);
	d_print("ts: %d fs: %d\n", !!t_ts, !!t_fs);

	if (!t_fs)
		t_ts = NULL;

	term = getenv("TERM");
	if (!t_ts && term) {
		/*
		 * Eterm:            Eterm
		 * aterm:            rxvt
		 * mlterm:           xterm
		 * terminal (xfce):  xterm
		 * urxvt:            rxvt-unicode
		 * xterm:            xterm, xterm-{,16,88,256}color
		 */
		if (!strcmp(term, "screen")) {
			t_ts = "\033_";
			t_fs = "\033\\";
		} else if (!strncmp(term, "xterm", 5) ||
			   !strncmp(term, "rxvt", 4) ||
			   !strcmp(term, "Eterm")) {
			/* \033]1;  change icon
			 * \033]2;  change title
			 * \033]0;  change both
			 */
			t_ts = "\033]0;";
			t_fs = "\007";
		}
	}
}

static void init_all(void)
{
	server_init(server_address);

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
	help_init();
	cmdline_init();
	commands_init();
	search_mode_init();

	/* almost everything must be initialized now */
	options_load();

	/* finally we can set the output plugin */
	player_set_op(output_plugin);
	if (!soft_vol)
		mixer_open();

	lib_autosave_filename = xstrjoin(cmus_config_dir, "/lib.pl");
	pl_autosave_filename = xstrjoin(cmus_config_dir, "/playlist.pl");
	play_queue_autosave_filename = xstrjoin(cmus_config_dir, "/queue.pl");
	pl_filename = xstrdup(pl_autosave_filename);
	lib_filename = xstrdup(lib_autosave_filename);

	if (error_count) {
		char buf[16];
		char *ret;

		warn("Press <enter> to continue.");

		ret = fgets(buf, sizeof(buf), stdin);
		BUG_ON(ret == NULL);
	}
	help_add_all_unbound();

	init_curses();

	if (resume_cmus) {
		resume_load();
		cmus_add(play_queue_append, play_queue_autosave_filename, FILE_TYPE_PL, JOB_TYPE_QUEUE, 0);
	}

	cmus_add(pl_add_track, pl_autosave_filename, FILE_TYPE_PL, JOB_TYPE_PL, 0);
	cmus_add(lib_add_track, lib_autosave_filename, FILE_TYPE_PL, JOB_TYPE_LIB, 0);
}

static void exit_all(void)
{
	endwin();

	if (resume_cmus)
		resume_exit();
	options_exit();

	server_exit();
	cmus_exit();
	if (resume_cmus)
		cmus_save(play_queue_for_each, play_queue_autosave_filename);
	cmus_save(lib_for_each, lib_autosave_filename);
	cmus_save(pl_for_each, pl_autosave_filename);

	player_exit();
	op_exit_plugins();
	commands_exit();
	search_mode_exit();
	filters_exit();
	help_exit();
	browser_exit();
}

enum {
	FLAG_LISTEN,
	FLAG_PLUGINS,
	FLAG_SHOW_CURSOR,
	FLAG_HELP,
	FLAG_VERSION,
	NR_FLAGS
};

static struct option options[NR_FLAGS + 1] = {
	{ 0, "listen", 1 },
	{ 0, "plugins", 0 },
	{ 0, "show-cursor", 0 },
	{ 0, "help", 0 },
	{ 0, "version", 0 },
	{ 0, NULL, 0 }
};

static const char *usage =
"Usage: %s [OPTION]...\n"
"Curses based music player.\n"
"\n"
"      --listen ADDR   listen on ADDR instead of $XDG_RUNTIME_DIR/cmus-socket\n"
"                      ADDR is either a UNIX socket or host[:port]\n"
"                      WARNING: using TCP/IP is insecure!\n"
"      --plugins       list available plugins and exit\n"
"      --show-cursor   always visible cursor\n"
"      --help          display this help and exit\n"
"      --version       " VERSION "\n"
"\n"
"Use cmus-remote to control cmus from command line.\n"
"Report bugs to <cmus-devel@lists.sourceforge.net>.\n";

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
			printf("cmus " VERSION
			       "\nCopyright 2004-2006 Timo Hirvonen"
			       "\nCopyright 2008-2013 Various Authors\n");
			return 0;
		case FLAG_PLUGINS:
			list_plugins = 1;
			break;
		case FLAG_LISTEN:
			server_address = xstrdup(arg);
			break;
		case FLAG_SHOW_CURSOR:
			show_cursor = 1;
			break;
		}
	}

	setlocale(LC_CTYPE, "");
	setlocale(LC_COLLATE, "");
	charset = getenv("CMUS_CHARSET");
	if (!charset || !charset[0]) {
#ifdef CODESET
		charset = nl_langinfo(CODESET);
#else
		charset = "ISO-8859-1";
#endif
	}
	if (strcmp(charset, "UTF-8") == 0)
		using_utf8 = 1;

	misc_init();
	if (server_address == NULL)
		server_address = xstrdup(cmus_socket_path);
	debug_init();
	d_print("charset = '%s'\n", charset);

	ip_load_plugins();
	op_load_plugins();
	if (list_plugins) {
		ip_dump_plugins();
		op_dump_plugins();
		return 0;
	}
	init_all();
	main_loop();
	exit_all();
	return 0;
}
