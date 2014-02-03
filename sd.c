/*
 * Copyright 2013-2014 Various Authors
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

#include "sd.h"
#include "sd_introspect.h"
#include "output.h"
#include "player.h"
#include "server.h"
#include "ui_curses.h"
#include "options.h"
#include "utils.h"
#include "rbtree.h"
#include "lib.h"
#include "command_mode.h"

#include <dbus/dbus.h>
#include <stdio.h>

#define SD_CMUS_NAME      "net.sourceforge.cmus"
#define SD_CMUS_INTERFACE "net.sourceforge.cmus"
#define SD_CMUS_PATH      "/net/sourceforge/cmus"

static DBusConnection *sd_con;
int sd_socket;

static void sd_ok(DBusMessage *msg)
{
	DBusMessage *reply = dbus_message_new_method_return(msg);
	dbus_connection_send(sd_con, reply, NULL);
	dbus_message_unref(reply);
}

static void sd_reply(DBusMessage *msg, int type, void *info)
{
	DBusMessage *reply = dbus_message_new_method_return(msg);
	dbus_message_append_args(reply,
			type, info,
			DBUS_TYPE_INVALID);
	dbus_connection_send(sd_con, reply, NULL);
	dbus_message_unref(reply);
}

static void sd_error(DBusMessage *msg, const char *reason)
{
	DBusMessage *reply = dbus_message_new_error(msg, DBUS_ERROR_FAILED, reason);
	dbus_connection_send(sd_con, reply, NULL);
	dbus_message_unref(reply);
}

enum sd_info_arg {
	SD_INFO_ALBUM,
	SD_INFO_ALBUMARTIST,
	SD_INFO_ARTIST,
	SD_INFO_COMPILATION,
	SD_INFO_DATE,
	SD_INFO_DISCNUMBER,
	SD_INFO_DURATION,
	SD_INFO_FILENAME,
	SD_INFO_GENRE,
	SD_INFO_HAS_TRACK,
	SD_INFO_ORIGINAL_DATE,
	SD_INFO_POS,
	SD_INFO_REPEAT,
	SD_INFO_SHUFFLE,
	SD_INFO_STATUS,
	SD_INFO_TITLE,
	SD_INFO_TRACKNUMBER,
	SD_INFO_VERSION,
	SD_INFO_VOLUME,
};

static void sd_ti_s(DBusMessage *msg, int cmd)
{
	const char *info = NULL;
	struct track_info *ti = player_info.ti;
	if (ti != NULL) {
		switch (cmd) {
		case SD_INFO_ALBUMARTIST: info = ti->albumartist; break;
		case SD_INFO_ALBUM:       info = ti->album;       break;
		case SD_INFO_ARTIST:      info = ti->artist;      break;
		case SD_INFO_FILENAME:    info = ti->filename;    break;
		case SD_INFO_GENRE:       info = ti->genre;       break;
		case SD_INFO_TITLE:       info = ti->title;       break;
		}
	}
	info = info ? info : "";
	sd_reply(msg, DBUS_TYPE_STRING, &info);
}

static void sd_ti_b(DBusMessage *msg, int cmd)
{
	uint32_t info = 0;
	struct track_info *ti = player_info.ti;
	if (ti != NULL) {
		switch (cmd) {
		case  SD_INFO_COMPILATION:  info  =  ti->is_va_compilation;  break;
		}
	}
	info = !!info;
	sd_reply(msg, DBUS_TYPE_BOOLEAN, &info);
}

static void sd_ti_i(DBusMessage *msg, int cmd)
{
	int32_t info = 0;
	struct track_info *ti = player_info.ti;
	if (ti != NULL) {
		switch (cmd) {
		case SD_INFO_DATE:          info = ti->date;         break;
		case SD_INFO_DISCNUMBER:    info = ti->discnumber;   break;
		case SD_INFO_DURATION:      info = ti->duration;     break;
		case SD_INFO_ORIGINAL_DATE: info = ti->originaldate; break;
		case SD_INFO_TRACKNUMBER:   info = ti->tracknumber;  break;
		}
	}
	sd_reply(msg, DBUS_TYPE_INT32, &info);
}

static int sd_volume(void)
{
	int vol, vol_left, vol_right;
	if (soft_vol) {
		vol_left = soft_vol_l;
		vol_right = soft_vol_r;
	} else if (!volume_max) {
		vol_left = vol_right = -1;
	} else {
		vol_left = scale_to_percentage(volume_l, volume_max);
		vol_right = scale_to_percentage(volume_r, volume_max);
	}
	vol = (vol_left + vol_right + 1) / 2;
	return vol;
}

static const char *sd_status(void)
{
	switch (player_info.status) {
	case PLAYER_STATUS_PAUSED:  return "paused";
	case PLAYER_STATUS_PLAYING: return "playing";
	case PLAYER_STATUS_STOPPED: return "stopped";
	default:                    return "unknown";
	}
}

static void sd_info_s(DBusMessage *msg, int cmd)
{
	const char *info = NULL;
	switch (cmd) {
	case SD_INFO_STATUS:  info = sd_status(); break;
	case SD_INFO_VERSION: info = VERSION;     break;
	}
	info = info ? info : "";
	sd_reply(msg, DBUS_TYPE_STRING, &info);
}

static void sd_info_b(DBusMessage *msg, int cmd)
{
	uint32_t info = 0;
	switch (cmd) {
	case SD_INFO_HAS_TRACK: info = player_info.ti ? 1 : 0; break;
	case SD_INFO_REPEAT:    info = repeat;                 break;
	case SD_INFO_SHUFFLE:   info = shuffle;                break;
	}
	info = !!info;
	sd_reply(msg, DBUS_TYPE_BOOLEAN, &info);
}

static void sd_info_i(DBusMessage *msg, int cmd)
{
	int32_t info = 0;
	switch (cmd) {
	case SD_INFO_DURATION: info = player_info.pos; break;
	case SD_INFO_POS:      info = player_info.pos; break;
	case SD_INFO_VOLUME:   info = sd_volume();     break;
	}
	sd_reply(msg, DBUS_TYPE_INT32, &info);
}

enum sd_cmd_arg {
	SD_CMD_ADD_LIBRARY,
	SD_CMD_ADD_PLAYLIST,
	SD_CMD_ADD_QUEUE,
	SD_CMD_CLEAR_LIBRARY,
	SD_CMD_CLEAR_PLAYLIST,
	SD_CMD_CLEAR_QUEUE,
	SD_CMD_LOAD,
	SD_CMD_NEXT,
	SD_CMD_PAUSE,
	SD_CMD_PLAY,
	SD_CMD_PLAY_FILE,
	SD_CMD_PREV,
	SD_CMD_SEEK,
	SD_CMD_STOP,
	SD_CMD_TOGGLE_REPEAT,
	SD_CMD_TOGGLE_SHUFFLE,
	SD_CMD_VOLUME,
};

static const char *sd_cmd_arg_str[] = {
	[SD_CMD_ADD_LIBRARY]    = "add -l %s",
	[SD_CMD_ADD_PLAYLIST]   = "add -p %s",
	[SD_CMD_ADD_QUEUE]      = "add -q %s",
	[SD_CMD_CLEAR_LIBRARY]  = "clear -l",
	[SD_CMD_CLEAR_PLAYLIST] = "clear -p",
	[SD_CMD_CLEAR_QUEUE]    = "clear -q",
	[SD_CMD_LOAD]           = "load %s",
	[SD_CMD_NEXT]           = "player-next",
	[SD_CMD_PAUSE]          = "player-pause",
	[SD_CMD_PLAY_FILE]      = "player-play %s",
	[SD_CMD_PLAY]           = "player-play",
	[SD_CMD_PREV]           = "player-prev",
	[SD_CMD_SEEK]           = "seek %s",
	[SD_CMD_STOP]           = "player-stop",
	[SD_CMD_TOGGLE_REPEAT]  = "toggle repeat",
	[SD_CMD_TOGGLE_SHUFFLE] = "toggle shuffle",
	[SD_CMD_VOLUME]         = "vol %s",
};

static void sd_cmd(DBusMessage *msg, int cmd)
{
	run_command(sd_cmd_arg_str[cmd]);
	sd_ok(msg);
}

static void sd_cmd_s(DBusMessage *msg, int cmd)
{
	char *arg = NULL;
	dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID);
	if (arg != NULL) {
		char buffer[512];
		if (snprintf(buffer, sizeof(buffer), sd_cmd_arg_str[cmd], arg) < sizeof(buffer)) {
			run_command(buffer);
			sd_ok(msg);
		} else {
			sd_error(msg, "bad argument");
		}
	} else {
		sd_error(msg, "bad argument");
	}
}

static void sd_user_cmd(DBusMessage *msg, int cmd)
{
	char *arg = NULL;
	dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID);
	if (arg != NULL) {
		FILE *file = new_memstream();
		set_client_fd(fileno(file));
		run_command(arg);
		set_client_fd(-1);
		char *buf = close_memstream(file, NULL);
		sd_reply(msg, DBUS_TYPE_STRING, &buf);
		free(buf);
	} else {
		sd_error(msg, "bad argument");
	}
}

static void sd_hello(DBusMessage *msg, int cmd)
{
	sd_ok(msg);
}

static void sd_query_old(DBusMessage *msg, int cmd)
{
	FILE *file = new_memstream();
	server_query(file);
	char *buf = close_memstream(file, NULL);
	sd_reply(msg, DBUS_TYPE_STRING, &buf);
	free(buf);
}

static void sd_list_artists(DBusMessage *msg, int cmd)
{
	FILE *file = new_memstream();
	struct rb_node *tmp;
	struct artist *artist;
	rb_for_each_entry(artist, tmp, &lib_artist_root, tree_node) {
		fprintf(file, "%s\n", artist->name);
	}
	char *buf = close_memstream(file, NULL);
	sd_reply(msg, DBUS_TYPE_STRING, &buf);
	free(buf);
}

struct sd_handler {
	const char *method;
	void (*handler)(DBusMessage *, int);
	int arg;
};

static struct sd_handler sd_handlers[] = {
	{ "has_track",       sd_info_b,       SD_INFO_HAS_TRACK     },
	{ "pos",             sd_info_i,       SD_INFO_POS           },
	{ "repeat",          sd_info_b,       SD_INFO_REPEAT        },
	{ "shuffle",         sd_info_b,       SD_INFO_SHUFFLE       },
	{ "status",          sd_info_s,       SD_INFO_STATUS        },
	{ "version",         sd_info_s,       SD_INFO_VERSION       },
	{ "volume",          sd_info_i,       SD_INFO_VOLUME        },
	{ "albumartist",     sd_ti_s,         SD_INFO_ALBUMARTIST   },
	{ "album",           sd_ti_s,         SD_INFO_ALBUM         },
	{ "artist",          sd_ti_s,         SD_INFO_ARTIST        },
	{ "compilation",     sd_ti_b,         SD_INFO_COMPILATION   },
	{ "date",            sd_ti_i,         SD_INFO_DATE          },
	{ "discnumber",      sd_ti_i,         SD_INFO_DISCNUMBER    },
	{ "duration",        sd_ti_i,         SD_INFO_DURATION      },
	{ "filename",        sd_ti_s,         SD_INFO_FILENAME      },
	{ "genre",           sd_ti_s,         SD_INFO_GENRE         },
	{ "original_date",   sd_ti_i,         SD_INFO_ORIGINAL_DATE },
	{ "title",           sd_ti_s,         SD_INFO_TITLE         },
	{ "tracknumber",     sd_ti_i,         SD_INFO_TRACKNUMBER   },
	{ "clear_library",   sd_cmd,          SD_CMD_CLEAR_LIBRARY  },
	{ "clear_playlist",  sd_cmd,          SD_CMD_CLEAR_PLAYLIST },
	{ "clear_queue",     sd_cmd,          SD_CMD_CLEAR_QUEUE    },
	{ "next",            sd_cmd,          SD_CMD_NEXT           },
	{ "pause",           sd_cmd,          SD_CMD_PAUSE          },
	{ "play",            sd_cmd,          SD_CMD_PLAY           },
	{ "prev",            sd_cmd,          SD_CMD_PREV           },
	{ "stop",            sd_cmd,          SD_CMD_STOP           },
	{ "toggle_repeat",   sd_cmd,          SD_CMD_TOGGLE_REPEAT  },
	{ "toggle_shuffle",  sd_cmd,          SD_CMD_TOGGLE_SHUFFLE },
	{ "add_to_library",  sd_cmd_s,        SD_CMD_ADD_LIBRARY    },
	{ "add_to_playlist", sd_cmd_s,        SD_CMD_ADD_PLAYLIST   },
	{ "add_to_queue",    sd_cmd_s,        SD_CMD_ADD_QUEUE      },
	{ "load",            sd_cmd_s,        SD_CMD_LOAD           },
	{ "play_file",       sd_cmd_s,        SD_CMD_PLAY_FILE      },
	{ "seek",            sd_cmd_s,        SD_CMD_SEEK           },
	{ "set_volume",      sd_cmd_s,        SD_CMD_VOLUME         },
	{ "cmd",             sd_user_cmd,     0                     },
	{ "hello",           sd_hello,        0                     },
	{ "list_artists",    sd_list_artists, 0                     },
	{ "query_old",       sd_query_old,    0                     },
};

static DBusHandlerResult sd_message_function(DBusConnection *con, DBusMessage *msg, void *user_data)
{
	if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL)
		return DBUS_HANDLER_RESULT_HANDLED;

	if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable", "Introspect")) {
		sd_reply(msg, DBUS_TYPE_STRING, &SD_INTROSPECT);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	for (int i = 0; i < N_ELEMENTS(sd_handlers); i++) {
		struct sd_handler hlr = sd_handlers[i];
		if (dbus_message_is_method_call(msg, SD_CMUS_INTERFACE, hlr.method)) {
			hlr.handler(msg, hlr.arg);
			return DBUS_HANDLER_RESULT_HANDLED;
		}
	}

	sd_error(msg, "not implemented");
	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult sd_disconnect(DBusConnection *con, DBusMessage *msg, void *user_data)
{
	if (dbus_message_is_signal(msg, DBUS_INTERFACE_LOCAL, "Disconnected")) {
		sd_socket = 0;
		sd_con = NULL;
	}
	return DBUS_HANDLER_RESULT_HANDLED;
}

void sd_init(void)
{
	sd_con = dbus_bus_get(DBUS_BUS_SESSION, NULL);
	if (sd_con == NULL)
		return;
	dbus_connection_set_exit_on_disconnect(sd_con, FALSE);
	int ret = dbus_bus_request_name(sd_con, SD_CMUS_NAME,
			DBUS_NAME_FLAG_DO_NOT_QUEUE,
			NULL);
	if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		sd_con = NULL;
		return;
	}
	if (!dbus_connection_get_socket(sd_con, &sd_socket)) {
		sd_con = NULL;
		return;
	}
	DBusObjectPathVTable vtable = {0};
	vtable.message_function = sd_message_function;
	dbus_connection_register_object_path(sd_con, SD_CMUS_PATH, &vtable, NULL);
	vtable.message_function = sd_disconnect;
	dbus_connection_register_object_path(sd_con, DBUS_PATH_LOCAL, &vtable, NULL);
}

void sd_handle(void)
{
	if (sd_con == NULL)
		return;

	dbus_connection_read_write(sd_con, 0);
	while (dbus_connection_dispatch(sd_con) != DBUS_DISPATCH_COMPLETE);
	if (sd_con != NULL) {
		dbus_connection_flush(sd_con);
	}
}

const char *signal_str[] = {
	[SD_EXIT]          = "exit",
	[SD_STATUS_CHANGE] = "status_change",
	[SD_TRACK_CHANGE]  = "track_change",
	[SD_VOL_CHANGE]    = "vol_change",
};

void sd_notify(enum sd_signal signal)
{
	if (sd_con == NULL)
		return;

	DBusMessage *msg = dbus_message_new_signal(SD_CMUS_PATH,
			SD_CMUS_INTERFACE,
			signal_str[signal]);
	dbus_connection_send(sd_con, msg, NULL);
	dbus_message_unref(msg);
}
