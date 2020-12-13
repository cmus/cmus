/*
 * Copyright 2016 Various Authors
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

#include <systemd/sd-bus.h>

#include "mpris.h"
#include "ui_curses.h"
#include "cmus.h"
#include "player.h"
#include "options.h"
#include "output.h"
#include "track_info.h"
#include "utils.h"
#include "uchar.h"

#define CK(v) \
do { \
	int tmp = (v); \
	if (tmp < 0) \
		return tmp; \
} while (0)

static sd_bus *bus;
int mpris_fd = -1;

static int mpris_msg_ignore(sd_bus_message *m, void *_userdata,
		sd_bus_error *_ret_error)
{
	return sd_bus_reply_method_return(m, "");
}

static int mpris_read_false(sd_bus *_bus, const char *_path,
		const char *_interface, const char *_property,
		sd_bus_message *reply, void *_userdata,
		sd_bus_error *_ret_error)
{
	uint32_t b = 0;
	return sd_bus_message_append_basic(reply, 'b', &b);
}

static int mpris_read_true(sd_bus *_bus, const char *_path,
		const char *_interface, const char *_property,
		sd_bus_message *reply, void *_userdata,
		sd_bus_error *_ret_error)
{
	uint32_t b = 1;
	return sd_bus_message_append_basic(reply, 'b', &b);
}

static int mpris_write_ignore(sd_bus *_bus, const char *_path,
		const char *_interface, const char *_property,
		sd_bus_message *value, void *_userdata,
		sd_bus_error *_ret_error)
{
	return sd_bus_reply_method_return(value, "");
}

static int mpris_raise_vte(sd_bus_message *m, void *_userdata,
		sd_bus_error *_ret_error)
{
	cmus_raise_vte();
	return sd_bus_reply_method_return(m, "");
}

static int mpris_can_raise_vte(sd_bus *_bus, const char *_path,
		const char *_interface, const char *_property,
		sd_bus_message *reply, void *_userdata,
		sd_bus_error *_ret_error)
{
	uint32_t b = cmus_can_raise_vte();
	return sd_bus_message_append_basic(reply, 'b', &b);
}

static int mpris_identity(sd_bus *_bus, const char *_path,
		const char *_interface, const char *_property,
		sd_bus_message *reply, void *_userdata,
		sd_bus_error *_ret_error)
{
	const char *id = "cmus";
	return sd_bus_message_append_basic(reply, 's', id);
}

static int mpris_uri_schemes(sd_bus *_bus, const char *_path,
		const char *_interface, const char *_property,
		sd_bus_message *reply, void *_userdata,
		sd_bus_error *_ret_error)
{
	static const char * const schemes[] = { "file", "http", NULL };
	return sd_bus_message_append_strv(reply, (char **)schemes);
}

static int mpris_mime_types(sd_bus *_bus, const char *_path,
		const char *_interface, const char *_property,
		sd_bus_message *reply, void *_userdata,
		sd_bus_error *_ret_error)
{
	static const char * const types[] = { NULL };
	return sd_bus_message_append_strv(reply, (char **)types);
}

static int mpris_next(sd_bus_message *m, void *_userdata,
		sd_bus_error *_ret_error)
{
	cmus_next();
	return sd_bus_reply_method_return(m, "");
}

static int mpris_prev(sd_bus_message *m, void *_userdata,
		sd_bus_error *_ret_error)
{
	cmus_prev();
	return sd_bus_reply_method_return(m, "");
}

static int mpris_pause(sd_bus_message *m, void *_userdata,
		sd_bus_error *_ret_error)
{
	player_pause_playback();
	return sd_bus_reply_method_return(m, "");
}

static int mpris_toggle_pause(sd_bus_message *m, void *_userdata,
		sd_bus_error *_ret_error)
{
	player_pause();
	return sd_bus_reply_method_return(m, "");
}

static int mpris_stop(sd_bus_message *m, void *_userdata,
		sd_bus_error *_ret_error)
{
	player_stop();
	return sd_bus_reply_method_return(m, "");
}

static int mpris_play(sd_bus_message *m, void *_userdata,
		sd_bus_error *_ret_error)
{
	player_play();
	return sd_bus_reply_method_return(m, "");
}

static int mpris_seek(sd_bus_message *m, void *_userdata,
		sd_bus_error *_ret_error)
{
	int64_t val = 0;
	CK(sd_bus_message_read_basic(m, 'x', &val));
	player_seek(val / (1000 * 1000), 1, 0);
	return sd_bus_reply_method_return(m, "");
}

static int mpris_seek_abs(sd_bus_message *m, void *_userdata,
		sd_bus_error *_ret_error)
{
	char buf[] = "/1122334455667788";
	if (player_info.ti)
		sprintf(buf, "/%"PRIx64, player_info.ti->uid);
	else
		sprintf(buf, "/");

	const char *path = NULL;
	int64_t val = 0;
	CK(sd_bus_message_read_basic(m, 'o', &path));
	CK(sd_bus_message_read_basic(m, 'x', &val));

	if (strcmp(buf, path) == 0)
		player_seek(val / (1000 * 1000), 0, 0);
	return sd_bus_reply_method_return(m, "");
}

static int mpris_play_file(sd_bus_message *m, void *_userdata,
		sd_bus_error *_ret_error)
{
	const char *path = NULL;
	CK(sd_bus_message_read_basic(m, 's', &path));
	cmus_play_file(path);
	return sd_bus_reply_method_return(m, "");
}

static int mpris_playback_status(sd_bus *_bus, const char *_path,
		const char *_interface, const char *_property,
		sd_bus_message *reply, void *_userdata,
		sd_bus_error *_ret_error)
{
	const char *ss[] = { "Stopped", "Playing", "Paused" };
	const char *s = ss[player_info.status];
	return sd_bus_message_append_basic(reply, 's', s);
}

static int mpris_loop_status(sd_bus *_bus, const char *_path,
		const char *_interface, const char *_property,
		sd_bus_message *reply, void *_userdata,
		sd_bus_error *_ret_error)
{
	const char *t = "None";
	if (player_repeat_current)
		t = "Track";
	else if (repeat)
		t = "Playlist";
	return sd_bus_message_append_basic(reply, 's', t);
}

static int mpris_set_loop_status(sd_bus *_bus, const char *_path,
		const char *_interface, const char *_property,
		sd_bus_message *value, void *_userdata,
		sd_bus_error *_ret_error)
{
	const char *t = NULL;
	CK(sd_bus_message_read_basic(value, 's', &t));
	if (strcmp(t, "None") == 0) {
		player_repeat_current = 0;
		repeat = 0;
	} else if (strcmp(t, "Track") == 0) {
		player_repeat_current = 1;
	} else if (strcmp(t, "Playlist") == 0) {
		player_repeat_current = 0;
		repeat = 1;
	}
	update_statusline();
	return sd_bus_reply_method_return(value, "");
}

static int mpris_rate(sd_bus *_bus, const char *_path,
		const char *_interface, const char *_property,
		sd_bus_message *reply, void *_userdata,
		sd_bus_error *_ret_error)
{
	static const double d = 1.0;
	return sd_bus_message_append_basic(reply, 'd', &d);
}

static int mpris_shuffle(sd_bus *_bus, const char *_path,
		const char *_interface, const char *_property,
		sd_bus_message *reply, void *_userdata,
		sd_bus_error *_ret_error)
{
	const uint32_t s = shuffle;
	return sd_bus_message_append_basic(reply, 'b', &s);
}

static int mpris_set_shuffle(sd_bus *_bus, const char *_path,
		const char *_interface, const char *_property,
		sd_bus_message *value, void *_userdata,
		sd_bus_error *_ret_error)
{
	uint32_t s = 0;
	CK(sd_bus_message_read_basic(value, 'b', &s));
	shuffle = s;
	update_statusline();
	return sd_bus_reply_method_return(value, "");
}

static int mpris_volume(sd_bus *_bus, const char *_path,
		const char *_interface, const char *_property,
		sd_bus_message *reply, void *_userdata,
		sd_bus_error *_ret_error)
{
	double vol;
	if (soft_vol) {
		vol = (soft_vol_l + soft_vol_r) / 200.0;
	} else if (volume_max && volume_l >= 0 && volume_r >= 0) {
		int vol_left = scale_to_percentage(volume_l, volume_max);
		int vol_right = scale_to_percentage(volume_r, volume_max);
		vol = (vol_left + vol_right) / 200.0;
	}
	return sd_bus_message_append_basic(reply, 'd', &vol);
}

static int mpris_set_volume(sd_bus *_bus, const char *_path,
		const char *_interface, const char *_property,
		sd_bus_message *value, void *_userdata,
		sd_bus_error *_ret_error)
{
	double vol;
	CK(sd_bus_message_read_basic(value, 'd', &vol));
	if (vol < 0.0)
		vol = 0.0;
	else if (vol > 1.0)
		vol = 1.0;
	int ivol = vol * 100;
	player_set_vol(ivol, VF_PERCENTAGE, ivol, VF_PERCENTAGE);
	update_statusline();
	return sd_bus_reply_method_return(value, "");
}

static int mpris_position(sd_bus *_bus, const char *_path,
		const char *_interface, const char *_property,
		sd_bus_message *reply, void *_userdata,
		sd_bus_error *_ret_error)
{
	int64_t pos = player_info.pos;
	pos *= 1000 * 1000;
	return sd_bus_message_append_basic(reply, 'x', &pos);
}

static int mpris_msg_append_simple_dict(sd_bus_message *m, const char *tag,
		char type, const void *val)
{
	const char s[] = { type, 0 };
	CK(sd_bus_message_open_container(m, 'e', "sv"));
	CK(sd_bus_message_append_basic(m, 's', tag));
	CK(sd_bus_message_open_container(m, 'v', s));
	CK(sd_bus_message_append_basic(m, type, val));
	CK(sd_bus_message_close_container(m));
	CK(sd_bus_message_close_container(m));
	return 0;
}

static int mpris_msg_append_si_dict(sd_bus_message *m, const char *a,
		int32_t i)
{
	return mpris_msg_append_simple_dict(m, a, 'i', &i);
}

static int mpris_msg_append_sx_dict(sd_bus_message *m, const char *a,
		int64_t i)
{
	return mpris_msg_append_simple_dict(m, a, 'x', &i);
}

static int mpris_msg_append_ss_dict(sd_bus_message *m, const char *a,
		const char *b)
{
	return mpris_msg_append_simple_dict(m, a, 's', b);
}

static int mpris_msg_append_so_dict(sd_bus_message *m, const char *a,
		const char *b)
{
	return mpris_msg_append_simple_dict(m, a, 'o', b);
}

static int mpris_msg_append_sas_dict(sd_bus_message *m, const char *a,
		const char *b)
{
	CK(sd_bus_message_open_container(m, 'e', "sv"));
	CK(sd_bus_message_append_basic(m, 's', a));
	CK(sd_bus_message_open_container(m, 'v', "as"));
	CK(sd_bus_message_open_container(m, 'a', "s"));
	CK(sd_bus_message_append_basic(m, 's', b));
	CK(sd_bus_message_close_container(m));
	CK(sd_bus_message_close_container(m));
	CK(sd_bus_message_close_container(m));
	return 0;
}

static int mpris_metadata(sd_bus *_bus, const char *_path,
		const char *_interface, const char *_property,
		sd_bus_message *reply, void *_userdata,
		sd_bus_error *_ret_error)
{
	CK(sd_bus_message_open_container(reply, 'a', "{sv}"));

	struct track_info *ti = player_info.ti;
	if (ti) {
		char buf[] = "/1122334455667788";
		sprintf(buf, "/%"PRIx64, ti->uid);
		CK(mpris_msg_append_so_dict(reply, "mpris:trackid", buf));

		int64_t dur = ti->duration;
		dur *= 1000 * 1000;
		CK(mpris_msg_append_sx_dict(reply, "mpris:length", dur));

		//The dbus connection closes if invalid data is sent.
		//As a *temporary* fix, ensure all strings are encoded in utf8.
		if (ti->artist) {
			char corrected[u_str_print_size(ti->artist)];
			u_to_utf8(corrected, ti->artist);
			CK(mpris_msg_append_sas_dict(reply,
					"xesam:artist", corrected));
		}
		if (ti->title) {
			char corrected[u_str_print_size(ti->title)];
			u_to_utf8(corrected, ti->title);
			CK(mpris_msg_append_ss_dict(reply,
					"xesam:title", corrected));
		}
		if (ti->album) {
			char corrected[u_str_print_size(ti->album)];
			u_to_utf8(corrected, ti->album);
			CK(mpris_msg_append_ss_dict(reply,
					"xesam:album", corrected));
		}
		if (ti->albumartist) {
			char corrected[u_str_print_size(ti->albumartist)];
			u_to_utf8(corrected, ti->albumartist);
			CK(mpris_msg_append_sas_dict(reply,
					"xesam:albumArtist", corrected));
		}
		if (ti->genre) {
			char corrected[u_str_print_size(ti->genre)];
			u_to_utf8(corrected, ti->genre);
			CK(mpris_msg_append_sas_dict(reply,
					"xesam:genre", corrected));
		}
		if (ti->comment) {
			char corrected[u_str_print_size(ti->comment)];
			u_to_utf8(corrected, ti->comment);
			CK(mpris_msg_append_sas_dict(reply,
					"xesam:comment", corrected));
		}
		if (ti->bpm != -1)
			CK(mpris_msg_append_si_dict(reply, "xesam:audioBPM",
						ti->bpm));
		if (ti->tracknumber != -1)
			CK(mpris_msg_append_si_dict(reply, "xesam:trackNumber",
						ti->tracknumber));
		if (ti->discnumber != -1)
			CK(mpris_msg_append_si_dict(reply, "xesam:discNumber",
						ti->discnumber));
		if (is_http_url(ti->filename))
			CK(mpris_msg_append_ss_dict(reply, "cmus:stream_title",
						get_stream_title()));
	}

	CK(sd_bus_message_close_container(reply));
	return 0;
}

#define MPRIS_PROP(name, type, read) \
	SD_BUS_PROPERTY(name, type, read, 0, \
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE)

#define MPRIS_WPROP(name, type, read, write) \
	SD_BUS_WRITABLE_PROPERTY(name, type, read, write, 0, \
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE)

static const sd_bus_vtable media_player2_vt[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Raise", "", "", mpris_raise_vte, 0),
	SD_BUS_METHOD("Quit", "", "", mpris_msg_ignore, 0),
	MPRIS_PROP("CanQuit", "b", mpris_read_false),
	MPRIS_WPROP("Fullscreen", "b", mpris_read_false, mpris_write_ignore),
	MPRIS_PROP("CanSetFullscreen", "b", mpris_read_false),
	MPRIS_PROP("CanRaise", "b", mpris_can_raise_vte),
	MPRIS_PROP("HasTrackList", "b", mpris_read_false),
	MPRIS_PROP("Identity", "s", mpris_identity),
	MPRIS_PROP("SupportedUriSchemes", "as", mpris_uri_schemes),
	MPRIS_PROP("SupportedMimeTypes", "as", mpris_mime_types),
	SD_BUS_VTABLE_END,
};

static const sd_bus_vtable media_player2_player_vt[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Next", "", "", mpris_next, 0),
	SD_BUS_METHOD("Previous", "", "", mpris_prev, 0),
	SD_BUS_METHOD("Pause", "", "", mpris_pause, 0),
	SD_BUS_METHOD("PlayPause", "", "", mpris_toggle_pause, 0),
	SD_BUS_METHOD("Stop", "", "", mpris_stop, 0),
	SD_BUS_METHOD("Play", "", "", mpris_play, 0),
	SD_BUS_METHOD("Seek", "x", "", mpris_seek, 0),
	SD_BUS_METHOD("SetPosition", "ox", "", mpris_seek_abs, 0),
	SD_BUS_METHOD("OpenUri", "s", "", mpris_play_file, 0),
	MPRIS_PROP("PlaybackStatus", "s", mpris_playback_status),
	MPRIS_WPROP("LoopStatus", "s", mpris_loop_status, mpris_set_loop_status),
	MPRIS_WPROP("Rate", "d", mpris_rate, mpris_write_ignore),
	MPRIS_WPROP("Shuffle", "b", mpris_shuffle, mpris_set_shuffle),
	MPRIS_WPROP("Volume", "d", mpris_volume, mpris_set_volume),
	SD_BUS_PROPERTY("Position", "x", mpris_position, 0, 0),
	MPRIS_PROP("MinimumRate", "d", mpris_rate),
	MPRIS_PROP("MaximumRate", "d", mpris_rate),
	MPRIS_PROP("CanGoNext", "b", mpris_read_true),
	MPRIS_PROP("CanGoPrevious", "b", mpris_read_true),
	MPRIS_PROP("CanPlay", "b", mpris_read_true),
	MPRIS_PROP("CanPause", "b", mpris_read_true),
	MPRIS_PROP("CanSeek", "b", mpris_read_true),
	SD_BUS_PROPERTY("CanControl", "b", mpris_read_true, 0, 0),
	MPRIS_PROP("Metadata", "a{sv}", mpris_metadata),
	SD_BUS_SIGNAL("Seeked", "x", 0),
	SD_BUS_VTABLE_END,
};

void mpris_init(void)
{
	int res = 0;

	res = sd_bus_default_user(&bus);
	if (res < 0)
		goto out;
	res = sd_bus_add_object_vtable(bus, NULL, "/org/mpris/MediaPlayer2",
			"org.mpris.MediaPlayer2", media_player2_vt, NULL);
	if (res < 0)
		goto out;
	res = sd_bus_add_object_vtable(bus, NULL, "/org/mpris/MediaPlayer2",
			"org.mpris.MediaPlayer2.Player",
			media_player2_player_vt, NULL);
	if (res < 0)
		goto out;
	res = sd_bus_request_name(bus, "org.mpris.MediaPlayer2.cmus", 0);
	mpris_fd = sd_bus_get_fd(bus);

out:
	if (res < 0) {
		sd_bus_unref(bus);
		bus = NULL;
		mpris_fd = -1;

		const char *msg = "an error occured while initializing "
			          "MPRIS: %s. MPRIS will be disabled.";

		error_msg(msg, strerror(-res));
	}
}

void mpris_process(void)
{
	if (bus) {
		while (sd_bus_process(bus, NULL) > 0)
			;
	}
}

void mpris_free(void)
{
	sd_bus_unref(bus);
	bus = NULL;
	mpris_fd = -1;
}

static void mpris_player_property_changed(const char *name)
{
	const char * const strv[] = { name, NULL };
	if (bus) {
		sd_bus_emit_properties_changed_strv(bus,
				"/org/mpris/MediaPlayer2",
				"org.mpris.MediaPlayer2.Player", (char **)strv);
		sd_bus_flush(bus);
	}
}

void mpris_playback_status_changed(void)
{
	mpris_player_property_changed("PlaybackStatus");
}

void mpris_loop_status_changed(void)
{
	mpris_player_property_changed("LoopStatus");
}

void mpris_shuffle_changed(void)
{
	mpris_player_property_changed("Shuffle");
}

void mpris_volume_changed(void)
{
	mpris_player_property_changed("Volume");
}

void mpris_metadata_changed(void)
{
	mpris_player_property_changed("Metadata");
	// the following is not necessary according to the spec but some
	// applications seem to disregard the spec and expect this to happen
	mpris_seeked();
}

void mpris_seeked(void)
{
	if (!bus)
		return;
	int64_t pos = player_info.pos;
	pos *= 1000 * 1000;
	sd_bus_emit_signal(bus, "/org/mpris/MediaPlayer2",
			"org.mpris.MediaPlayer2.Player", "Seeked", "x", pos);
}
