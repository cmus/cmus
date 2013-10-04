/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2006 Timo Hirvonen
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

#include "options.h"
#include "list.h"
#include "utils.h"
#include "xmalloc.h"
#include "player.h"
#include "buffer.h"
#include "ui_curses.h"
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
#include "input.h"
#ifdef HAVE_CONFIG
#include "config/datadir.h"
#endif
#include "track_info.h"
#include "cache.h"
#include "debug.h"
#include "discid.h"

#include <stdio.h>
#include <errno.h>
#include <strings.h>

#if defined(__sun__)
#include <ncurses.h>
#else
#include <curses.h>
#endif

/* initialized option variables */

char *cdda_device = NULL;
char *output_plugin = NULL;
char *status_display_program = NULL;
char *server_password;
int auto_reshuffle = 1;
int confirm_run = 1;
int resume_cmus = 0;
int show_hidden = 0;
int show_current_bitrate = 0;
int show_playback_position = 1;
int show_remaining_time = 0;
int set_term_title = 1;
int wrap_search = 1;
int play_library = 1;
int repeat = 0;
int shuffle = 0;
int follow = 0;
int display_artist_sort_name;
int smart_artist_sort = 1;
int scroll_offset = 2;
int rewind_offset = 5;
int skip_track_info = 0;

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

int attrs[NR_ATTRS] = {
	A_NORMAL,
	A_NORMAL,
	A_NORMAL,
	A_NORMAL,
	A_NORMAL,
	A_NORMAL,
	A_NORMAL,
	A_NORMAL,
	A_NORMAL
};

/* uninitialized option variables */
char *track_win_format = NULL;
char *track_win_format_va = NULL;
char *track_win_alt_format = NULL;
char *list_win_format = NULL;
char *list_win_format_va = NULL;
char *list_win_alt_format = NULL;
char *current_format = NULL;
char *current_alt_format = NULL;
char *window_title_format = NULL;
char *window_title_alt_format = NULL;
char *id3_default_charset = NULL;
char *icecast_default_charset = NULL;

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
	FMT_CURRENT,
	FMT_CURRENT_ALT,
	FMT_PLAYLIST,
	FMT_PLAYLIST_ALT,
	FMT_PLAYLIST_VA,
	FMT_TITLE,
	FMT_TITLE_ALT,
	FMT_TRACKWIN,
	FMT_TRACKWIN_ALT,
	FMT_TRACKWIN_VA,

	NR_FMTS
};

/* default values for the variables which we must initialize but
 * can't do it statically */
static const struct {
	const char *name;
	const char *value;
} str_defaults[] = {
	[FMT_CURRENT_ALT]	= { "altformat_current"	, " %F "				},
	[FMT_CURRENT]		= { "format_current"	, " %a - %l -%3n. %t%= %y "		},
	[FMT_PLAYLIST_ALT]	= { "altformat_playlist", " %f%= %d "				},
	[FMT_PLAYLIST]		= { "format_playlist"	, " %-25%a %3n. %t%= %y %d "		},
	[FMT_PLAYLIST_VA]	= { "format_playlist_va", " %-25%A %3n. %t (%a)%= %y %d "	},
	[FMT_TITLE_ALT]		= { "altformat_title"	, "%f"					},
	[FMT_TITLE]		= { "format_title"	, "%a - %l - %t (%y)"			},
	[FMT_TRACKWIN_ALT]	= { "altformat_trackwin", " %f%= %d "				},
	[FMT_TRACKWIN]		= { "format_trackwin"	, "%3n. %t%= %y %d "			},
	[FMT_TRACKWIN_VA]	= { "format_trackwin_va", "%3n. %t (%a)%= %y %d "		},

	[NR_FMTS] =

	{ "lib_sort", "albumartist date album discnumber tracknumber title filename" },
	{ "pl_sort", "" },
	{ "id3_default_charset", "ISO-8859-1" },
	{ "icecast_default_charset", "ISO-8859-1" },
	{ NULL, NULL }
};

/* callbacks for normal options {{{ */

static void get_device(unsigned int id, char *buf)
{
	strcpy(buf, cdda_device);
}

static void set_device(unsigned int id, const char *buf)
{
	free(cdda_device);
	cdda_device = expand_filename(buf);
}

#define SECOND_SIZE (44100 * 16 / 8 * 2)
static void get_buffer_seconds(unsigned int id, char *buf)
{
	buf_int(buf, (player_get_buffer_chunks() * CHUNK_SIZE + SECOND_SIZE / 2) / SECOND_SIZE);
}

static void set_buffer_seconds(unsigned int id, const char *buf)
{
	int sec;

	if (parse_int(buf, 1, 300, &sec))
		player_set_buffer_chunks((sec * SECOND_SIZE + CHUNK_SIZE / 2) / CHUNK_SIZE);
}

static void get_scroll_offset(unsigned int id, char *buf)
{
	buf_int(buf, scroll_offset);
}

static void set_scroll_offset(unsigned int id, const char *buf)
{
	int offset;

	if (parse_int(buf, 0, 9999, &offset))
		scroll_offset = offset;
}

static void get_rewind_offset(unsigned int id, char *buf)
{
	buf_int(buf, rewind_offset);
}

static void set_rewind_offset(unsigned int id, const char *buf)
{
	int offset;

	if (parse_int(buf, -1, 9999, &offset))
		rewind_offset = offset;
}

static void get_id3_default_charset(unsigned int id, char *buf)
{
	strcpy(buf, id3_default_charset);
}

static void get_icecast_default_charset(unsigned int id, char *buf)
{
	strcpy(buf, icecast_default_charset);
}

static void set_id3_default_charset(unsigned int id, const char *buf)
{
	free(id3_default_charset);
	id3_default_charset = xstrdup(buf);
}

static void set_icecast_default_charset(unsigned int id, const char *buf)
{
	free(icecast_default_charset);
	icecast_default_charset = xstrdup(buf);
}

static const struct {
	const char *str;
	sort_key_t key;
} sort_key_map[] = {
	{ "artist",		SORT_ARTIST		},
	{ "album",		SORT_ALBUM		},
	{ "title",		SORT_TITLE		},
	{ "tracknumber",	SORT_TRACKNUMBER	},
	{ "discnumber",		SORT_DISCNUMBER		},
	{ "date",		SORT_DATE		},
	{ "originaldate",	SORT_ORIGINALDATE	},
	{ "genre",		SORT_GENRE		},
	{ "comment",		SORT_COMMENT		},
	{ "albumartist",	SORT_ALBUMARTIST	},
	{ "filename",		SORT_FILENAME		},
	{ "filemtime",		SORT_FILEMTIME		},
	{ "rg_track_gain",	SORT_RG_TRACK_GAIN	},
	{ "rg_track_peak",	SORT_RG_TRACK_PEAK	},
	{ "rg_album_gain",	SORT_RG_ALBUM_GAIN	},
	{ "rg_album_peak",	SORT_RG_ALBUM_PEAK	},
	{ "bitrate",		SORT_BITRATE		},
	{ "codec",		SORT_CODEC		},
	{ "codec_profile",	SORT_CODEC_PROFILE	},
	{ "media",		SORT_MEDIA		},
	{ "-artist",		REV_SORT_ARTIST		},
	{ "-album",		REV_SORT_ALBUM		},
	{ "-title",		REV_SORT_TITLE		},
	{ "-tracknumber",	REV_SORT_TRACKNUMBER	},
	{ "-discnumber",	REV_SORT_DISCNUMBER	},
	{ "-date",		REV_SORT_DATE		},
	{ "-originaldate",	REV_SORT_ORIGINALDATE	},
	{ "-genre",		REV_SORT_GENRE		},
	{ "-comment",		REV_SORT_COMMENT	},
	{ "-albumartist",	REV_SORT_ALBUMARTIST	},
	{ "-filename",		REV_SORT_FILENAME	},
	{ "-filemtime",		REV_SORT_FILEMTIME	},
	{ "-rg_track_gain",	REV_SORT_RG_TRACK_GAIN	},
	{ "-rg_track_peak",	REV_SORT_RG_TRACK_PEAK	},
	{ "-rg_album_gain",	REV_SORT_RG_ALBUM_GAIN	},
	{ "-rg_album_peak",	REV_SORT_RG_ALBUM_PEAK	},
	{ "-bitrate",		REV_SORT_BITRATE	},
	{ "-codec",		REV_SORT_CODEC		},
	{ "-codec_profile",	REV_SORT_CODEC_PROFILE	},
	{ "-media",		REV_SORT_MEDIA		},
	{ NULL,                 SORT_INVALID            }
};

static sort_key_t *parse_sort_keys(const char *value)
{
	sort_key_t *keys;
	const char *s, *e;
	int size = 4;
	int pos = 0;

	size = 4;
	keys = xnew(sort_key_t, size);

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
			if (sort_key_map[i].str == NULL) {
				error_msg("invalid sort key '%s'", buf);
				free(keys);
				return NULL;
			}

			if (strcmp(buf, sort_key_map[i].str) == 0)
				break;
		}
		if (pos == size - 1) {
			size *= 2;
			keys = xrenew(sort_key_t, keys, size);
		}
		keys[pos++] = sort_key_map[i].key;
	}
	keys[pos] = SORT_INVALID;
	return keys;
}

static const char *sort_key_to_str(sort_key_t key)
{
	int i;
	for (i = 0; sort_key_map[i].str; i++) {
		if (sort_key_map[i].key == key)
			return sort_key_map[i].str;
	}
	return NULL;
}

static void sort_keys_to_str(const sort_key_t *keys, char *buf, size_t bufsize)
{
	int i, pos = 0;

	for (i = 0; keys[i] != SORT_INVALID; i++) {
		const char *key = sort_key_to_str(keys[i]);
		int len = strlen(key);

		if ((int)bufsize - pos - len - 2 < 0)
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
	strcpy(buf, lib_editable.sort_str);
}

static void set_lib_sort(unsigned int id, const char *buf)
{
	sort_key_t *keys = parse_sort_keys(buf);

	if (keys) {
		editable_lock();
		editable_set_sort_keys(&lib_editable, keys);
		editable_unlock();
		sort_keys_to_str(keys, lib_editable.sort_str, sizeof(lib_editable.sort_str));
	}
}

static void get_pl_sort(unsigned int id, char *buf)
{
	strcpy(buf, pl_editable.sort_str);
}

static void set_pl_sort(unsigned int id, const char *buf)
{
	sort_key_t *keys = parse_sort_keys(buf);

	if (keys) {
		editable_lock();
		editable_set_sort_keys(&pl_editable, keys);
		editable_unlock();
		sort_keys_to_str(keys, pl_editable.sort_str, sizeof(pl_editable.sort_str));
	}
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
		status_display_program = expand_filename(buf);
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

static void get_follow(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[follow]);
}

static void set_follow(unsigned int id, const char *buf)
{
	if (!parse_bool(buf, &follow))
		return;
	update_statusline();
}

static void toggle_follow(unsigned int id)
{
	follow ^= 1;
	update_statusline();
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

	editable_lock();
	play_sorted = tmp;
	editable_unlock();

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

static void get_smart_artist_sort(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[smart_artist_sort]);
}

static void set_smart_artist_sort(unsigned int id, const char *buf)
{
	if (parse_bool(buf, &smart_artist_sort))
		tree_sort_artists();
}

static void toggle_smart_artist_sort(unsigned int id)
{
	smart_artist_sort ^= 1;
	tree_sort_artists();
}

static void get_display_artist_sort_name(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[display_artist_sort_name]);
}

static void set_display_artist_sort_name(unsigned int id, const char *buf)
{
	parse_bool(buf, &display_artist_sort_name);
	lib_tree_win->changed = 1;
}

static void toggle_display_artist_sort_name(unsigned int id)
{
	display_artist_sort_name ^= 1;
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

static void get_resume(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[resume_cmus]);
}

static void set_resume(unsigned int id, const char *buf)
{
	parse_bool(buf, &resume_cmus);
}

static void toggle_resume(unsigned int id)
{
	resume_cmus ^= 1;
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

static void get_show_current_bitrate(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[show_current_bitrate]);
}

static void set_show_current_bitrate(unsigned int id, const char *buf)
{
	if (parse_bool(buf, &show_current_bitrate))
		update_statusline();
}

static void toggle_show_current_bitrate(unsigned int id)
{
	show_current_bitrate ^= 1;
	update_statusline();
}

static void get_show_playback_position(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[show_playback_position]);
}

static void set_show_playback_position(unsigned int id, const char *buf)
{
	if (!parse_bool(buf, &show_playback_position))
		return;
	update_statusline();
}

static void toggle_show_playback_position(unsigned int id)
{
	show_playback_position ^= 1;
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

static void get_wrap_search(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[wrap_search]);
}

static void set_wrap_search(unsigned int id, const char *buf)
{
	parse_bool(buf, &wrap_search);
}

static void toggle_wrap_search(unsigned int id)
{
	wrap_search ^= 1;
}

static void get_skip_track_info(unsigned int id, char *buf)
{
	strcpy(buf, bool_names[skip_track_info]);
}

static void set_skip_track_info(unsigned int id, const char *buf)
{
	parse_bool(buf, &skip_track_info);
}

static void toggle_skip_track_info(unsigned int id)
{
	skip_track_info ^= 1;
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

static const char * const attr_enum_names[6 + 1] = {
	"default",
	"standout", "underline", "reverse", "blink", "bold",
	NULL
};

static void get_attr(unsigned int id, char *buf)
{
	int attr = attrs[id];

	if (attr == 0) {
		strcpy(buf, "default");
		return;
	}

	if (attr & A_STANDOUT)
		strcat(buf, "standout|");

	if (attr & A_UNDERLINE)
		strcat(buf, "underline|");

	if (attr & A_REVERSE)
		strcat(buf, "reverse|");

	if (attr & A_BLINK)
		strcat(buf, "blink|");

	if (attr & A_BOLD)
		strcat(buf, "bold|");

	buf[strlen(buf) - 1] = '\0';
}

static void set_attr(unsigned int id, const char *buf)
{
	int attr = 0;
	size_t i = 0;
	size_t offset = 0;
	size_t length = 0;
	char*  current;

	do {
		if (buf[i] == '|' || buf[i] == '\0') {
			current = xstrndup(&buf[offset], length);

			if (strcmp(current, "default") == 0)
				attr |= A_NORMAL;
			else if (strcmp(current, "standout") == 0)
				attr |= A_STANDOUT;
			else if (strcmp(current, "underline") == 0)
				attr |= A_UNDERLINE;
			else if (strcmp(current, "reverse") == 0)
				attr |= A_REVERSE;
			else if (strcmp(current, "blink") == 0)
				attr |= A_BLINK;
			else if (strcmp(current, "bold") == 0)
				attr |= A_BOLD;

			free(current);

			offset = i;
			length = -1;
		}

		i++;
		length++;
	} while (buf[i - 1] != '\0');

	attrs[id] = attr;
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
	case FMT_PLAYLIST_VA:
		return &list_win_format_va;
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

	if (!track_format_valid(buf)) {
		error_msg("invalid format string");
		return;
	}
	free(*fmtp);
	*fmtp = xstrdup(buf);

	update_full();
}

/* }}} */

#define DN(name) { #name, get_ ## name, set_ ## name, NULL, 0 },
#define DN_FLAGS(name, flags) { #name, get_ ## name, set_ ## name, NULL, flags },
#define DT(name) { #name, get_ ## name, set_ ## name, toggle_ ## name, 0 },

static const struct {
	const char *name;
	opt_get_cb get;
	opt_set_cb set;
	opt_toggle_cb toggle;
	unsigned int flags;
} simple_options[] = {
	DT(aaa_mode)
	DT(auto_reshuffle)
	DN_FLAGS(device, OPT_PROGRAM_PATH)
	DN(buffer_seconds)
	DN(scroll_offset)
	DN(rewind_offset)
	DT(confirm_run)
	DT(continue)
	DT(smart_artist_sort)
	DN(id3_default_charset)
	DN(icecast_default_charset)
	DN(lib_sort)
	DN(output_plugin)
	DN(passwd)
	DN(pl_sort)
	DT(play_library)
	DT(play_sorted)
	DT(display_artist_sort_name)
	DT(repeat)
	DT(repeat_current)
	DT(replaygain)
	DT(replaygain_limit)
	DN(replaygain_preamp)
	DT(resume)
	DT(show_hidden)
	DT(show_current_bitrate)
	DT(show_playback_position)
	DT(show_remaining_time)
	DT(set_term_title)
	DT(shuffle)
	DT(follow)
	DT(softvol)
	DN(softvol_state)
	DN_FLAGS(status_display_program, OPT_PROGRAM_PATH)
	DT(wrap_search)
	DT(skip_track_info)
	{ NULL, NULL, NULL, NULL, 0 }
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

static const char * const attr_names[NR_ATTRS] = {
	"color_cmdline_attr",
	"color_statusline_attr",
	"color_titleline_attr",
	"color_win_attr",
	"color_win_cur_sel_attr",
	"color_cur_sel_attr",
	"color_win_inactive_cur_sel_attr",
	"color_win_inactive_sel_attr",
	"color_win_sel_attr",
	"color_win_title_attr"
};

LIST_HEAD(option_head);
int nr_options = 0;

void option_add(const char *name, unsigned int id, opt_get_cb get,
		opt_set_cb set, opt_toggle_cb toggle, unsigned int flags)
{
	struct cmus_opt *opt = xnew(struct cmus_opt, 1);
	struct list_head *item;

	opt->name = name;
	opt->id = id;
	opt->get = get;
	opt->set = set;
	opt->toggle = toggle;
	opt->flags = flags;

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
				simple_options[i].set, simple_options[i].toggle,
				simple_options[i].flags);

	for (i = 0; i < NR_FMTS; i++)
		option_add(str_defaults[i].name, i, get_format, set_format, NULL, 0);

	for (i = 0; i < NR_COLORS; i++)
		option_add(color_names[i], i, get_color, set_color, NULL, 0);

	for (i = 0; i < NR_ATTRS; i++)
		option_add(attr_names[i], i, get_attr, set_attr, NULL, 0);

	ip_add_options();
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
	cdda_device = get_default_cdda_device();
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
	char filename_tmp[512];
	char filename[512];
	FILE *f;
	int i;

	snprintf(filename_tmp, sizeof(filename_tmp), "%s/autosave.tmp", cmus_config_dir);
	f = fopen(filename_tmp, "w");
	if (f == NULL) {
		warn_errno("creating %s", filename_tmp);
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

	snprintf(filename, sizeof(filename), "%s/autosave", cmus_config_dir);
	i = rename(filename_tmp, filename);
	if (i)
		warn_errno("renaming %s to %s", filename_tmp, filename);
}

struct resume {
	enum player_status status;
	char *filename;
	long int position;
	char *lib_filename;
	int view;
	char *live_filter;
	char *browser_dir;
};

static int handle_resume_line(void *data, const char *line)
{
	struct resume *resume = data;
	char *cmd, *arg;

	if (!parse_command(line, &cmd, &arg))
		return 0;
	if (!arg)
		goto out;

	if (strcmp(cmd, "status") == 0) {
		parse_enum(arg, 0, NR_PLAYER_STATUS, player_status_names, (int *) &resume->status);
	} else if (strcmp(cmd, "file") == 0) {
		free(resume->filename);
		resume->filename = xstrdup(unescape(arg));
	} else if (strcmp(cmd, "position") == 0) {
		str_to_int(arg, &resume->position);
	} else if (strcmp(cmd, "lib_file") == 0) {
		free(resume->lib_filename);
		resume->lib_filename = xstrdup(unescape(arg));
	} else if (strcmp(cmd, "view") == 0) {
		parse_enum(arg, 0, NR_VIEWS, view_names, &resume->view);
	} else if (strcmp(cmd, "live-filter") == 0) {
		free(resume->live_filter);
		resume->live_filter = xstrdup(unescape(arg));
	} else if (strcmp(cmd, "browser-dir") == 0) {
		free(resume->browser_dir);
		resume->browser_dir = xstrdup(unescape(arg));
	}

	free(arg);
out:
	free(cmd);
	return 0;
}

void resume_load(void)
{
	char filename[512];
	struct track_info *ti, *old;
	struct resume resume = { .status = PLAYER_STATUS_STOPPED, .view = -1 };

	snprintf(filename, sizeof(filename), "%s/resume", cmus_config_dir);
	if (file_for_each_line(filename, handle_resume_line, &resume) == -1) {
		if (errno != ENOENT)
			error_msg("loading %s: %s", filename, strerror(errno));
		return;
	}
	if (resume.view >= 0 && resume.view != cur_view)
		set_view(resume.view);
	if (resume.lib_filename) {
		cache_lock();
		ti = old = cache_get_ti(resume.lib_filename, 0);
		cache_unlock();
		if (ti) {
			editable_lock();
			lib_add_track(ti);
			track_info_unref(ti);
			lib_store_cur_track(ti);
			track_info_unref(ti);
			ti = lib_set_track(lib_find_track(ti));
			if (ti) {
				BUG_ON(ti != old);
				track_info_unref(ti);
				tree_sel_current();
				sorted_sel_current();
			}
			editable_unlock();
		}
		free(resume.lib_filename);
	}
	if (resume.filename) {
		cache_lock();
		ti = cache_get_ti(resume.filename, 0);
		cache_unlock();
		if (ti) {
			player_set_file(ti);
			if (resume.status != PLAYER_STATUS_STOPPED)
				player_seek(resume.position, 0, resume.status == PLAYER_STATUS_PLAYING);
		}
		free(resume.filename);
	}
	if (resume.live_filter) {
		editable_lock();
		filters_set_live(resume.live_filter);
		editable_unlock();
		free(resume.live_filter);
	}
	if (resume.browser_dir) {
		browser_chdir(resume.browser_dir);
		free(resume.browser_dir);
	}
}

void resume_exit(void)
{
	char filename_tmp[512];
	char filename[512];
	struct track_info *ti;
	FILE *f;
	int rc;

	snprintf(filename_tmp, sizeof(filename_tmp), "%s/resume.tmp", cmus_config_dir);
	f = fopen(filename_tmp, "w");
	if (!f) {
		warn_errno("creating %s", filename_tmp);
		return;
	}

	player_info_lock();
	fprintf(f, "status %s\n", player_status_names[player_info.status]);
	ti = player_info.ti;
	if (ti) {
		fprintf(f, "file %s\n", escape(ti->filename));
		fprintf(f, "position %d\n", player_info.pos);
	}
	player_info_unlock();
	if (lib_cur_track)
		ti = tree_track_info(lib_cur_track);
	else
		ti = lib_get_cur_stored_track();
	if (ti)
		fprintf(f, "lib_file %s\n", escape(ti->filename));
	fprintf(f, "view %s\n", view_names[cur_view]);
	if (lib_live_filter)
		fprintf(f, "live-filter %s\n", escape(lib_live_filter));
	fprintf(f, "browser-dir %s\n", escape(browser_dir));

	fclose(f);

	snprintf(filename, sizeof(filename), "%s/resume", cmus_config_dir);
	rc = rename(filename_tmp, filename);
	if (rc)
		warn_errno("renaming %s to %s", filename_tmp, filename);
}
