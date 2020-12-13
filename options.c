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
#include "xstrjoin.h"
#include "track_info.h"
#include "cache.h"
#include "debug.h"
#include "discid.h"
#include "mpris.h"

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
int auto_expand_albums_follow = 1;
int auto_expand_albums_search = 1;
int auto_expand_albums_selcur = 1;
int show_all_tracks = 1;
int mouse = 0;
int mpris = 1;
int time_show_leading_zero = 1;
int start_view = TREE_VIEW;
int stop_after_queue = 0;
int tree_width_percent = 33;
int tree_width_max = 0;

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
	COLOR_WHITE | BRIGHT,
	-1,

	-1,
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
	A_NORMAL,
	A_NORMAL,
	A_BOLD,
	A_NORMAL,
};

/* uninitialized option variables */
char *tree_win_format = NULL;
char *tree_win_artist_format = NULL;
char *track_win_album_format = NULL;
char *track_win_format = NULL;
char *track_win_format_va = NULL;
char *track_win_alt_format = NULL;
char *list_win_format = NULL;
char *list_win_format_va = NULL;
char *list_win_alt_format = NULL;
char *current_format = NULL;
char *current_alt_format = NULL;
char *statusline_format = NULL;
char *window_title_format = NULL;
char *window_title_alt_format = NULL;
char *id3_default_charset = NULL;
char *icecast_default_charset = NULL;
char *lib_add_filter = NULL;

static void buf_int(char *buf, int val, size_t size)
{
	snprintf(buf, size, "%d", val);
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
	FMT_STATUSLINE,
	FMT_PLAYLIST,
	FMT_PLAYLIST_ALT,
	FMT_PLAYLIST_VA,
	FMT_TITLE,
	FMT_TITLE_ALT,
	FMT_TRACKWIN,
	FMT_TRACKWIN_ALBUM,
	FMT_TRACKWIN_ALT,
	FMT_TRACKWIN_VA,
	FMT_TREEWIN,
	FMT_TREEWIN_ARTIST,

	NR_FMTS
};

/* default values for the variables which we must initialize but
 * can't do it statically */
static const struct {
	const char *name;
	const char *value;
} str_defaults[] = {
	[FMT_CURRENT_ALT]	= { "altformat_current"		, " %F "						},
	[FMT_CURRENT]		= { "format_current"		, " %a - %l -%3n. %t%= %y "				},
	[FMT_STATUSLINE]	= { "format_statusline"		,
		" %{status} %{?show_playback_position?%{position} %{?duration?/ %{duration} }?%{?duration?%{duration} }}"
		"- %{total} %{?bpm>0?at %{bpm} BPM }"
		"%{?volume>=0?vol: %{?lvolume!=rvolume?%{lvolume},%{rvolume} ?%{volume} }}"
		"%{?stream?buf: %{buffer} }"
		"%{?show_current_bitrate & bitrate>=0? %{bitrate} kbps }"
		"%="
		"%{?repeat_current?repeat current?%{?play_library?%{playlist_mode} from %{?play_sorted?sorted }library?playlist}}"
		" | %1{continue}%1{follow}%1{repeat}%1{shuffle} "
	},
	[FMT_PLAYLIST_ALT]	= { "altformat_playlist"	, " %f%= %d "						},
	[FMT_PLAYLIST]		= { "format_playlist"		, " %-21%a %3n. %t%= %y %d %{?X!=0?%3X ?    }"		},
	[FMT_PLAYLIST_VA]	= { "format_playlist_va"	, " %-21%A %3n. %t (%a)%= %y %d %{?X!=0?%3X ?    }"	},
	[FMT_TITLE_ALT]		= { "altformat_title"		, "%f"							},
	[FMT_TITLE]		= { "format_title"		, "%a - %l - %t (%y)"					},
	[FMT_TRACKWIN_ALBUM]	= { "format_trackwin_album"	, " %l %= %{albumduration} "				},
	[FMT_TRACKWIN_ALT]	= { "altformat_trackwin"	, " %f%= %d "						},
	[FMT_TRACKWIN]		= { "format_trackwin"		, "%3n. %t%= %y %d "					},
	[FMT_TRACKWIN_VA]	= { "format_trackwin_va"	, "%3n. %t (%a)%= %y %d "				},
	[FMT_TREEWIN]		= { "format_treewin"		, "  %l"						},
	[FMT_TREEWIN_ARTIST]	= { "format_treewin_artist"	, "%a"							},

	[NR_FMTS] =

	{ "lib_sort", "albumartist date album discnumber tracknumber title filename play_count" },
	{ "pl_sort", "" },
	{ "id3_default_charset", "ISO-8859-1" },
	{ "icecast_default_charset", "ISO-8859-1" },
	{ NULL, NULL }
};

/* callbacks for normal options {{{ */

static void get_device(void *data, char *buf, size_t size)
{
	strscpy(buf, cdda_device, size);
}

static void set_device(void *data, const char *buf)
{
	free(cdda_device);
	cdda_device = expand_filename(buf);
}

#define SECOND_SIZE (44100 * 16 / 8 * 2)
static void get_buffer_seconds(void *data, char *buf, size_t size)
{
	int val = (player_get_buffer_chunks() * CHUNK_SIZE + SECOND_SIZE / 2) /
		SECOND_SIZE;
	buf_int(buf, val, size);
}

static void set_buffer_seconds(void *data, const char *buf)
{
	int sec;

	if (parse_int(buf, 1, 300, &sec))
		player_set_buffer_chunks((sec * SECOND_SIZE + CHUNK_SIZE / 2) / CHUNK_SIZE);
}

static void get_scroll_offset(void *data, char *buf, size_t size)
{
	buf_int(buf, scroll_offset, size);
}

static void set_scroll_offset(void *data, const char *buf)
{
	int offset;

	if (parse_int(buf, 0, 9999, &offset))
		scroll_offset = offset;
}

static void get_rewind_offset(void *data, char *buf, size_t size)
{
	buf_int(buf, rewind_offset, size);
}

static void set_rewind_offset(void *data, const char *buf)
{
	int offset;

	if (parse_int(buf, -1, 9999, &offset))
		rewind_offset = offset;
}

static void get_id3_default_charset(void *data, char *buf, size_t size)
{
	strscpy(buf, id3_default_charset, size);
}

static void get_icecast_default_charset(void *data, char *buf, size_t size)
{
	strscpy(buf, icecast_default_charset, size);
}

static void set_id3_default_charset(void *data, const char *buf)
{
	free(id3_default_charset);
	id3_default_charset = xstrdup(buf);
}

static void set_icecast_default_charset(void *data, const char *buf)
{
	free(icecast_default_charset);
	icecast_default_charset = xstrdup(buf);
}

static void get_lib_sort(void *data, char *buf, size_t size)
{
	strscpy(buf, lib_editable.shared->sort_str, size);
}

static void set_lib_sort(void *data, const char *buf)
{
	sort_key_t *keys = parse_sort_keys(buf);

	if (keys) {
		editable_shared_set_sort_keys(lib_editable.shared, keys);
		editable_sort(&lib_editable);
		sort_keys_to_str(keys, lib_editable.shared->sort_str,
				sizeof(lib_editable.shared->sort_str));
	}
}

static void get_pl_sort(void *data, char *buf, size_t size)
{
	pl_get_sort_str(buf, size);
}

static void set_pl_sort(void *data, const char *buf)
{
	pl_set_sort_str(buf);
}

static void get_output_plugin(void *data, char *buf, size_t size)
{
	const char *value = op_get_current();

	if (value)
		strscpy(buf, value, size);
}

static void set_output_plugin(void *data, const char *buf)
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

static void get_passwd(void *data, char *buf, size_t size)
{
	if (server_password)
		strscpy(buf, server_password, size);
}

static void set_passwd(void *data, const char *buf)
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

static void get_replaygain_preamp(void *data, char *buf, size_t size)
{
	snprintf(buf, size, "%f", replaygain_preamp);
}

static void set_replaygain_preamp(void *data, const char *buf)
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

static void get_softvol_state(void *data, char *buf, size_t size)
{
	snprintf(buf, size, "%d %d", soft_vol_l, soft_vol_r);
}

static void set_softvol_state(void *data, const char *buf)
{
	char buffer[OPTION_MAX_SIZE];
	char *ptr;
	long int l, r;

	strscpy(buffer, buf, sizeof(buffer));
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

static void get_status_display_program(void *data, char *buf, size_t size)
{
	if (status_display_program)
		strscpy(buf, status_display_program, size);
}

static void set_status_display_program(void *data, const char *buf)
{
	free(status_display_program);
	status_display_program = NULL;
	if (buf[0])
		status_display_program = expand_filename(buf);
}

static void get_tree_width_percent(void *data, char *buf, size_t size)
{
	buf_int(buf, tree_width_percent, size);
}

static void set_tree_width_percent(void *data, const char *buf)
{
	int percent;

	if (parse_int(buf, 1, 100, &percent))
		tree_width_percent = percent;
	update_size();
}

static void get_tree_width_max(void *data, char *buf, size_t size)
{
	buf_int(buf, tree_width_max, size);
}

static void set_tree_width_max(void *data, const char *buf)
{
	int cols;

	if (parse_int(buf, 0, 9999, &cols))
		tree_width_max = cols;
	update_size();
}

/* }}} */

/* callbacks for toggle options {{{ */

static void get_auto_reshuffle(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[auto_reshuffle], size);
}

static void set_auto_reshuffle(void *data, const char *buf)
{
	parse_bool(buf, &auto_reshuffle);
}

static void toggle_auto_reshuffle(void *data)
{
	auto_reshuffle ^= 1;
}

static void get_follow(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[follow], size);
}

static void set_follow(void *data, const char *buf)
{
	if (!parse_bool(buf, &follow))
		return;
	update_statusline();
}

static void toggle_follow(void *data)
{
	follow ^= 1;
	update_statusline();
}

static void get_continue(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[player_cont], size);
}

static void set_continue(void *data, const char *buf)
{
	if (!parse_bool(buf, &player_cont))
		return;
	update_statusline();
}

static void toggle_continue(void *data)
{
	player_cont ^= 1;
	update_statusline();
}

static void get_continue_album(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[player_cont_album], size);
}

static void set_continue_album(void *data, const char *buf)
{
	if (!parse_bool(buf, &player_cont_album))
		return;
	update_statusline();
}

static void toggle_continue_album(void *data)
{
	player_cont_album ^= 1;
	update_statusline();
}

static void get_repeat_current(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[player_repeat_current], size);
}

static void set_repeat_current(void *data, const char *buf)
{
	int old = player_repeat_current;
	if (!parse_bool(buf, &player_repeat_current))
		return;
	if (old != player_repeat_current)
		mpris_loop_status_changed();
	update_statusline();
}

static void toggle_repeat_current(void *data)
{
	player_repeat_current ^= 1;
	mpris_loop_status_changed();
	update_statusline();
}

static void get_confirm_run(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[confirm_run], size);
}

static void set_confirm_run(void *data, const char *buf)
{
	parse_bool(buf, &confirm_run);
}

static void toggle_confirm_run(void *data)
{
	confirm_run ^= 1;
}

const char * const view_names[NR_VIEWS + 1] = {
	"tree", "sorted", "playlist", "queue", "browser", "filters", "settings", NULL
};

static void get_play_library(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[play_library], size);
}

static void set_play_library(void *data, const char *buf)
{
	if (!parse_bool(buf, &play_library))
		return;
	update_statusline();
}

static void toggle_play_library(void *data)
{
	play_library ^= 1;
	update_statusline();
}

static void get_play_sorted(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[play_sorted], size);
}

static void set_play_sorted(void *data, const char *buf)
{
	int tmp;

	if (!parse_bool(buf, &tmp))
		return;

	play_sorted = tmp;

	update_statusline();
}

static void toggle_play_sorted(void *data)
{
	play_sorted = play_sorted ^ 1;

	/* shuffle would override play_sorted... */
	if (play_sorted) {
		/* play_sorted makes no sense in playlist */
		play_library = 1;
		shuffle = 0;
	}

	update_statusline();
}

static void get_smart_artist_sort(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[smart_artist_sort], size);
}

static void set_smart_artist_sort(void *data, const char *buf)
{
	if (parse_bool(buf, &smart_artist_sort))
		tree_sort_artists();
}

static void toggle_smart_artist_sort(void *data)
{
	smart_artist_sort ^= 1;
	tree_sort_artists();
}

static void get_display_artist_sort_name(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[display_artist_sort_name], size);
}

static void set_display_artist_sort_name(void *data, const char *buf)
{
	parse_bool(buf, &display_artist_sort_name);
	lib_tree_win->changed = 1;
}

static void toggle_display_artist_sort_name(void *data)
{
	display_artist_sort_name ^= 1;
	lib_tree_win->changed = 1;
}

const char * const aaa_mode_names[] = {
	"all", "artist", "album", NULL
};

static void get_aaa_mode(void *data, char *buf, size_t size)
{
	strscpy(buf, aaa_mode_names[aaa_mode], size);
}

static void set_aaa_mode(void *data, const char *buf)
{
	int tmp;

	if (!parse_enum(buf, 0, 2, aaa_mode_names, &tmp))
		return;

	aaa_mode = tmp;
	update_statusline();
}

static void toggle_aaa_mode(void *data)
{
	/* aaa mode makes no sense in playlist */
	play_library = 1;

	aaa_mode++;
	aaa_mode %= 3;
	update_statusline();
}

static void get_repeat(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[repeat], size);
}

static void set_repeat(void *data, const char *buf)
{
	int old = repeat;
	if (!parse_bool(buf, &repeat))
		return;
	if (!player_repeat_current && old != repeat)
		mpris_loop_status_changed();
	update_statusline();
}

static void toggle_repeat(void *data)
{
	repeat ^= 1;
	if (!player_repeat_current)
		mpris_loop_status_changed();
	update_statusline();
}

static const char * const replaygain_names[] = {
	"disabled", "track", "album", "track-preferred", "album-preferred", NULL
};

static void get_replaygain(void *data, char *buf, size_t size)
{
	strscpy(buf, replaygain_names[replaygain], size);
}

static void set_replaygain(void *data, const char *buf)
{
	int tmp;

	if (!parse_enum(buf, 0, 4, replaygain_names, &tmp))
		return;
	player_set_rg(tmp);
}

static void toggle_replaygain(void *data)
{
	player_set_rg((replaygain + 1) % 5);
}

static void get_replaygain_limit(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[replaygain_limit], size);
}

static void set_replaygain_limit(void *data, const char *buf)
{
	int tmp;

	if (!parse_bool(buf, &tmp))
		return;
	player_set_rg_limit(tmp);
}

static void toggle_replaygain_limit(void *data)
{
	player_set_rg_limit(replaygain_limit ^ 1);
}

static void get_resume(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[resume_cmus], size);
}

static void set_resume(void *data, const char *buf)
{
	parse_bool(buf, &resume_cmus);
}

static void toggle_resume(void *data)
{
	resume_cmus ^= 1;
}

static void get_show_hidden(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[show_hidden], size);
}

static void set_show_hidden(void *data, const char *buf)
{
	if (!parse_bool(buf, &show_hidden))
		return;
	browser_reload();
}

static void toggle_show_hidden(void *data)
{
	show_hidden ^= 1;
	browser_reload();
}

static void set_show_all_tracks_int(int); /* defined below */

static void get_auto_expand_albums_follow(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[auto_expand_albums_follow], size);
}

static void set_auto_expand_albums_follow_int(int value)
{
	auto_expand_albums_follow = !!value;
	if (!auto_expand_albums_follow && !show_all_tracks)
		set_show_all_tracks_int(1);
}

static void set_auto_expand_albums_follow(void *data, const char *buf)
{
	int tmp = 0;
	parse_bool(buf, &tmp);
	set_auto_expand_albums_follow_int(tmp);
}

static void toggle_auto_expand_albums_follow(void *data)
{
	set_auto_expand_albums_follow_int(!auto_expand_albums_follow);
}

static void get_auto_expand_albums_search(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[auto_expand_albums_search], size);
}

static void set_auto_expand_albums_search_int(int value)
{
	auto_expand_albums_search = !!value;
	if (!auto_expand_albums_search && !show_all_tracks)
		set_show_all_tracks_int(1);
}

static void set_auto_expand_albums_search(void *data, const char *buf)
{
	int tmp = 0;
	parse_bool(buf, &tmp);
	set_auto_expand_albums_search_int(tmp);
}

static void toggle_auto_expand_albums_search(void *data)
{
	set_auto_expand_albums_search_int(!auto_expand_albums_search);
}

static void get_auto_expand_albums_selcur(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[auto_expand_albums_selcur], size);
}

static void set_auto_expand_albums_selcur_int(int value)
{
	auto_expand_albums_selcur = !!value;
	if (!auto_expand_albums_selcur && !show_all_tracks)
		set_show_all_tracks_int(1);
}

static void set_auto_expand_albums_selcur(void *data, const char *buf)
{
	int tmp = 0;
	parse_bool(buf, &tmp);
	set_auto_expand_albums_selcur_int(tmp);
}

static void toggle_auto_expand_albums_selcur(void *data)
{
	set_auto_expand_albums_selcur_int(!auto_expand_albums_selcur);
}


static void get_show_all_tracks(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[show_all_tracks], size);
}

static void set_show_all_tracks_int(int value)
{
	value = !!value;
	if (show_all_tracks == value)
		return;
	show_all_tracks = value;
	if (!show_all_tracks) {
		if  (!auto_expand_albums_follow)
			set_auto_expand_albums_follow_int(1);
		if  (!auto_expand_albums_search)
			set_auto_expand_albums_search_int(1);
		if  (!auto_expand_albums_selcur)
			set_auto_expand_albums_selcur_int(1);
	}
	tree_sel_update(0);
}

static void set_show_all_tracks(void *data, const char *buf)
{
	int tmp = 0;
	parse_bool(buf, &tmp);
	set_show_all_tracks_int(tmp);
}

static void toggle_show_all_tracks(void *data)
{
	set_show_all_tracks_int(!show_all_tracks);
}

static void get_show_current_bitrate(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[show_current_bitrate], size);
}

static void set_show_current_bitrate(void *data, const char *buf)
{
	if (parse_bool(buf, &show_current_bitrate))
		update_statusline();
}

static void toggle_show_current_bitrate(void *data)
{
	show_current_bitrate ^= 1;
	update_statusline();
}

static void get_show_playback_position(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[show_playback_position], size);
}

static void set_show_playback_position(void *data, const char *buf)
{
	if (!parse_bool(buf, &show_playback_position))
		return;
	update_statusline();
}

static void toggle_show_playback_position(void *data)
{
	show_playback_position ^= 1;
	update_statusline();
}

static void get_show_remaining_time(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[show_remaining_time], size);
}

static void set_show_remaining_time(void *data, const char *buf)
{
	if (!parse_bool(buf, &show_remaining_time))
		return;
	update_statusline();
}

static void toggle_show_remaining_time(void *data)
{
	show_remaining_time ^= 1;
	update_statusline();
}

static void get_set_term_title(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[set_term_title], size);
}

static void set_set_term_title(void *data, const char *buf)
{
	parse_bool(buf, &set_term_title);
}

static void toggle_set_term_title(void *data)
{
	set_term_title ^= 1;
}

static void get_shuffle(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[shuffle], size);
}

static void set_shuffle(void *data, const char *buf)
{
	int old = shuffle;
	if (!parse_bool(buf, &shuffle))
		return;
	if (old != shuffle)
		mpris_shuffle_changed();
	update_statusline();
}

static void toggle_shuffle(void *data)
{
	shuffle ^= 1;
	mpris_shuffle_changed();
	update_statusline();
}

static void get_softvol(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[soft_vol], size);
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

static void set_softvol(void *data, const char *buf)
{
	int soft;

	if (!parse_bool(buf, &soft))
		return;
	do_set_softvol(soft);
}

static void toggle_softvol(void *data)
{
	do_set_softvol(soft_vol ^ 1);
}

static void get_wrap_search(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[wrap_search], size);
}

static void set_wrap_search(void *data, const char *buf)
{
	parse_bool(buf, &wrap_search);
}

static void toggle_wrap_search(void *data)
{
	wrap_search ^= 1;
}

static void get_skip_track_info(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[skip_track_info], size);
}

static void set_skip_track_info(void *data, const char *buf)
{
	parse_bool(buf, &skip_track_info);
}

static void toggle_skip_track_info(void *data)
{
	skip_track_info ^= 1;
}

void update_mouse(void)
{
	if (mouse) {
		mouseinterval(25);
		mousemask(BUTTON_CTRL | BUTTON_ALT
		  | BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED
		  | BUTTON1_DOUBLE_CLICKED | BUTTON1_TRIPLE_CLICKED
		  | BUTTON2_PRESSED | BUTTON2_RELEASED | BUTTON2_CLICKED
		  | BUTTON3_PRESSED | BUTTON3_RELEASED | BUTTON3_CLICKED
		  | BUTTON3_DOUBLE_CLICKED | BUTTON3_TRIPLE_CLICKED
		  | BUTTON4_PRESSED | BUTTON4_RELEASED | BUTTON4_CLICKED
		  | BUTTON4_DOUBLE_CLICKED | BUTTON4_TRIPLE_CLICKED
#if NCURSES_MOUSE_VERSION >= 2
		  | BUTTON5_PRESSED | BUTTON5_RELEASED | BUTTON5_CLICKED
		  | BUTTON5_DOUBLE_CLICKED | BUTTON5_TRIPLE_CLICKED
#endif
		  , NULL);
	} else {
		mousemask(0, NULL);
	}
}

static void get_mouse(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[mouse], size);
}

static void set_mouse(void *data, const char *buf)
{
	parse_bool(buf, &mouse);
	update_mouse();
}

static void toggle_mouse(void *data)
{
	mouse ^= 1;
	update_mouse();
}

static void get_mpris(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[mpris], size);
}

static void set_mpris(void *data, const char *buf)
{
	parse_bool(buf, &mpris);
}

static void toggle_mpris(void *data)
{
	mpris ^= 1;
}

static void get_time_show_leading_zero(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[time_show_leading_zero], size);
}

static void set_time_show_leading_zero(void *data, const char *buf)
{
	if (!parse_bool(buf, &time_show_leading_zero))
		return;
	update_statusline();
}

static void toggle_time_show_leading_zero(void *data)
{
	time_show_leading_zero ^= 1;
	update_statusline();
}

static void get_lib_add_filter(void *data, char *buf, size_t size)
{
	strscpy(buf, lib_add_filter ? lib_add_filter : "", size);
}

static void set_lib_add_filter(void *data, const char *buf)
{
	struct expr *expr = NULL;

	if (strlen(buf) != 0) {
		/* parse expression if non-empty string given */
		expr = expr_parse(buf);

		if (!expr)
			return;
	}

	free(lib_add_filter);
	lib_add_filter = xstrdup(buf);

	lib_set_add_filter(expr);
}

static void get_stop_after_queue(void *data, char *buf, size_t size)
{
	strscpy(buf, bool_names[stop_after_queue], size);
}

static void set_stop_after_queue(void *data, const char *buf)
{
	parse_bool(buf, &stop_after_queue);
}

static void toggle_stop_after_queue(void *data)
{
	stop_after_queue ^= 1;
}

/* }}} */

/* special callbacks (id set) {{{ */

static const char * const color_enum_names[1 + 8 * 2 + 1] = {
	"default",
	"black", "red", "green", "yellow", "blue", "magenta", "cyan", "gray",
	"darkgray", "lightred", "lightgreen", "lightyellow", "lightblue", "lightmagenta", "lightcyan", "white",
	NULL
};

static void get_color(void *data, char *buf, size_t size)
{
	int val = *(int *)data;
	if (val < 16) {
		strscpy(buf, color_enum_names[val + 1], size);
	} else {
		buf_int(buf, val, size);
	}
}

static void set_color(void *data, const char *buf)
{
	int color;

	if (!parse_enum(buf, -1, 255, color_enum_names, &color))
		return;

	*(int *)data = color;
	update_colors();
	update_full();
}

static void get_start_view(void *data, char *buf, size_t size)
{
	strscpy(buf, view_names[start_view], size);
}

static void set_start_view(void *data, const char *buf)
{
	int view;

	if (parse_enum(buf, 0, NR_VIEWS - 1, view_names, &view)) {
		start_view = view;
	}
}

static void get_attr(void *data, char *buf, size_t size)
{
	int attr = *(int *)data;

	if (attr == 0) {
		strscpy(buf, "default", size);
		return;
	}

	const char *standout = "";
	const char *underline = "";
	const char *reverse = "";
	const char *blink = "";
	const char *bold = "";

	if (attr & A_STANDOUT)
		standout = "standout|";
	if (attr & A_UNDERLINE)
		underline = "underline|";
	if (attr & A_REVERSE)
		reverse = "reverse|";
	if (attr & A_BLINK)
		blink = "blink|";
	if (attr & A_BOLD)
		bold = "bold|";

	size_t len = snprintf(buf, size, "%s%s%s%s%s", standout, underline, reverse,
			blink, bold);

	if (0 < len && len < size)
		buf[len - 1] = 0;
}

static void set_attr(void *data, const char *buf)
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

	*(int *)data = attr;
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
	case FMT_TRACKWIN_ALBUM:
		return &track_win_album_format;
	case FMT_TRACKWIN_VA:
		return &track_win_format_va;
	case FMT_TREEWIN:
		return &tree_win_format;
	case FMT_TREEWIN_ARTIST:
		return &tree_win_artist_format;
	case FMT_STATUSLINE:
		return &statusline_format;
	default:
		die("unhandled format code: %d\n", id);
	}
	return NULL;
}

static void get_format(void *data, char *buf, size_t size)
{
	char **fmtp = data;

	strscpy(buf, *fmtp, size);
}

static void set_format(void *data, const char *buf)
{
	char **fmtp = data;

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
	DT(continue_album)
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
	DT(auto_expand_albums_follow)
	DT(auto_expand_albums_search)
	DT(auto_expand_albums_selcur)
	DT(show_all_tracks)
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
	DT(mouse)
	DT(mpris)
	DT(time_show_leading_zero)
	DN(lib_add_filter)
	DN(start_view)
	DT(stop_after_queue)
	DN(tree_width_percent)
	DN(tree_width_max)
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
	"color_win_title_fg",
	"color_trackwin_album_bg",
	"color_trackwin_album_fg",
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
	"color_win_title_attr",
	"color_trackwin_album_attr",
	"color_win_cur_attr",
};

LIST_HEAD(option_head);
int nr_options = 0;

void option_add(const char *name, const void *data, opt_get_cb get,
		opt_set_cb set, opt_toggle_cb toggle, unsigned int flags)
{
	struct cmus_opt *opt = xnew(struct cmus_opt, 1);
	struct list_head *item;

	opt->name = name;
	opt->data = (void *)data;
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
	struct cmus_opt *opt = option_find_silent(name);
	if (opt == NULL)
		error_msg("no such option %s", name);
	return opt;
}

struct cmus_opt *option_find_silent(const char *name)
{
	struct cmus_opt *opt;

	list_for_each_entry(opt, &option_head, node) {
		if (strcmp(name, opt->name) == 0)
			return opt;
	}
	return NULL;
}

void option_set(const char *name, const char *value)
{
	struct cmus_opt *opt = option_find(name);

	if (opt)
		opt->set(opt->data, value);
}

void options_add(void)
{
	int i;

	for (i = 0; simple_options[i].name; i++)
		option_add(simple_options[i].name, NULL, simple_options[i].get,
				simple_options[i].set, simple_options[i].toggle,
				simple_options[i].flags);

	for (i = 0; i < NR_FMTS; i++)
		option_add(str_defaults[i].name, id_to_fmt(i), get_format,
				set_format, NULL, 0);

	for (i = 0; i < NR_COLORS; i++)
		option_add(color_names[i], &colors[i], get_color, set_color,
				NULL, 0);

	for (i = 0; i < NR_ATTRS; i++)
		option_add(attr_names[i], &attrs[i], get_attr, set_attr, NULL,
				0);

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
		char *def = xstrjoin(cmus_data_dir, "/rc");

		if (errno != ENOENT)
			error_msg("loading %s: %s", filename, strerror(errno));

		/* load defaults */
		if (source_file(def) == -1)
			die_errno("loading %s", def);

		free(def);
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
		opt->get(opt->data, buf, OPTION_MAX_SIZE);
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
	char *marked_pl;
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
	} else if (strcmp(cmd, "marked-pl") == 0) {
		free(resume->marked_pl);
		resume->marked_pl = xstrdup(unescape(arg));
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
			lib_add_track(ti, NULL);
			track_info_unref(ti);
			lib_store_cur_track(ti);
			track_info_unref(ti);
			ti = lib_set_track(lib_find_track(ti));
			if (ti) {
				BUG_ON(ti != old);
				track_info_unref(ti);
				tree_sel_current(auto_expand_albums_follow);
				sorted_sel_current();
			}
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
		filters_set_live(resume.live_filter);
		free(resume.live_filter);
	}
	if (resume.browser_dir) {
		browser_chdir(resume.browser_dir);
		free(resume.browser_dir);
	}
	if (resume.marked_pl) {
		pl_set_marked_pl_by_name(resume.marked_pl);
		free(resume.marked_pl);
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

	fprintf(f, "status %s\n", player_status_names[player_info.status]);
	ti = player_info.ti;
	if (ti) {
		fprintf(f, "file %s\n", escape(ti->filename));
		fprintf(f, "position %d\n", player_info.pos);
	}
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

	fprintf(f, "marked-pl %s\n", escape(pl_marked_pl_name()));

	fclose(f);

	snprintf(filename, sizeof(filename), "%s/resume", cmus_config_dir);
	rc = rename(filename_tmp, filename);
	if (rc)
		warn_errno("renaming %s to %s", filename_tmp, filename);
}
