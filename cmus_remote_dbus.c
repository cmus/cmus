/*
 * Copyright 2008-2014 Various Authors
 * Copyright 2005-2006 Timo Hirvonen
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

#include "path.h"
#include "prog.h"
#include "utils.h"
#include "xmalloc.h"

#include <dbus/dbus.h>
#include <stdio.h>
#include <stdbool.h>

#define STRING DBUS_TYPE_STRING

static DBusConnection *con;
typedef DBusMessage Message;

static Message *message(const char *method_name)
{
	return dbus_message_new_method_call("net.sourceforge.cmus",
		"/net/sourceforge/cmus",
		"net.sourceforge.cmus",
		method_name);
}

static void unref(Message *msg)
{
	dbus_message_unref(msg);
}

static Message *send_reply(Message *msg)
{
	Message *reply = dbus_connection_send_with_reply_and_block(con, msg, DBUS_TIMEOUT_INFINITE, NULL);
	if (reply == NULL)
		die("cmus is not running\n");
	dbus_message_unref(msg);
	return reply;
}

static void send_no_reply(Message *msg)
{
	Message *reply = dbus_connection_send_with_reply_and_block(con, msg, DBUS_TIMEOUT_INFINITE, NULL);
	if (reply == NULL)
		die("cmus is not running\n");
	dbus_message_unref(msg);
	dbus_message_unref(reply);
}

static void append(Message *msg, int type, void *arg)
{
	dbus_message_append_args(msg, type, arg, DBUS_TYPE_INVALID);
}

static void *unpack(Message *msg, int type)
{
	static uint64_t value;
	dbus_message_get_args(msg, NULL, type, &value, DBUS_TYPE_INVALID);
	return &value;
}

#define CMD_0_1(NAME, OUT, OUT_DBUS, OUT_FMT) \
static void cmus_##NAME(void) \
{ \
	Message *msg = message(#NAME); \
	Message *reply_m = send_reply(msg); \
	OUT *reply = unpack(reply_m, OUT_DBUS); \
	printf(OUT_FMT, *reply); \
	unref(reply_m); \
}

#define CMD_1_1(NAME, OUT, OUT_DBUS, OUT_FMT) \
static void cmus_##NAME(const char *arg_in) \
{ \
	Message *msg = message(#NAME); \
	append(msg, DBUS_TYPE_STRING, &arg_in); \
	Message *reply_m = send_reply(msg); \
	OUT *reply = unpack(reply_m, OUT_DBUS); \
	printf(OUT_FMT, *reply); \
	unref(reply_m); \
}

#define CMD_1_0(NAME) \
static void cmus_##NAME(const char *arg_in) \
{ \
	Message *msg = message(#NAME); \
	append(msg, DBUS_TYPE_STRING, &arg_in); \
	send_no_reply(msg); \
}

#define CMD_0_0(NAME) \
static void cmus_##NAME(void) \
{ \
	Message *msg = message(#NAME); \
	send_no_reply(msg); \
}

CMD_0_0( clear_library   );
CMD_0_0( clear_playlist  );
CMD_0_0( clear_queue     );
CMD_0_0( hello           );
CMD_0_0( next            );
CMD_0_0( pause           );
CMD_0_0( play            );
CMD_0_0( prev            );
CMD_0_0( stop            );
CMD_0_0( toggle_repeat   );
CMD_0_0( toggle_shuffle  );
CMD_1_0( add_to_library  );
CMD_1_0( add_to_playlist );
CMD_1_0( add_to_queue    );
CMD_1_0( play_file       );
CMD_1_0( seek            );
CMD_1_0( set_volume      );
CMD_0_1( query_old,      char *, STRING, "%s\n" );
CMD_0_1( artist,         char *, STRING, "%s\n" );
CMD_0_1( album,          char *, STRING, "%s\n" );
CMD_0_1( title,          char *, STRING, "%s\n" );
CMD_0_1( list_artists,   char *, STRING, "%s"   );
CMD_1_1( cmd,            char *, STRING, "%s"   );

static char *file_url_absolute(const char *str)
{
	char *absolute;

	if (strncmp(str, "http://", 7) == 0)
		return xstrdup(str);

	absolute = path_absolute(str);
	if (absolute == NULL)
		die_errno("get_current_dir_name");
	return absolute;
}

enum flags {
	FLAG_SERVER,
	FLAG_PASSWD,
	FLAG_HELP,
	FLAG_VERSION,

	FLAG_PLAY,
	FLAG_PAUSE,
	FLAG_STOP,
	FLAG_NEXT,
	FLAG_PREV,
	FLAG_FILE,
	FLAG_REPEAT,
	FLAG_SHUFFLE,
	FLAG_VOLUME,
	FLAG_SEEK,
	FLAG_QUERY,

	FLAG_LIBRARY,
	FLAG_PLAYLIST,
	FLAG_QUEUE,
	FLAG_CLEAR,

	FLAG_ARTIST,
	FLAG_ALBUM,
	FLAG_TITLE,
	FLAG_LIST_ARTISTS,

	FLAG_RAW
};

static struct option options[] = {
	[FLAG_SERVER]       = { 0,   "server",       1 },
	[FLAG_PASSWD]       = { 0,   "passwd",       1 },
	[FLAG_HELP]         = { 0,   "help",         0 },
	[FLAG_VERSION]      = { 0,   "version",      0 },

	[FLAG_PLAY]         = { 'p', "play",         0 },
	[FLAG_PAUSE]        = { 'u', "pause",        0 },
	[FLAG_STOP]         = { 's', "stop",         0 },
	[FLAG_NEXT]         = { 'n', "next",         0 },
	[FLAG_PREV]         = { 'r', "prev",         0 },
	[FLAG_FILE]         = { 'f', "file",         1 },
	[FLAG_REPEAT]       = { 'R', "repeat",       0 },
	[FLAG_SHUFFLE]      = { 'S', "shuffle",      0 },
	[FLAG_VOLUME]       = { 'v', "volume",       1 },
	[FLAG_SEEK]         = { 'k', "seek",         1 },
	[FLAG_QUERY]        = { 'Q', "query",        0 },

	[FLAG_LIBRARY]      = { 'l', "library",      0 },
	[FLAG_PLAYLIST]     = { 'P', "playlist",     0 },
	[FLAG_QUEUE]        = { 'q', "queue",        0 },
	[FLAG_CLEAR]        = { 'c', "clear",        0 },

	[FLAG_ARTIST]       = { 0,   "artist",       0 },
	[FLAG_ALBUM]        = { 0,   "album",        0 },
	[FLAG_TITLE]        = { 0,   "title",        0 },
	[FLAG_LIST_ARTISTS] = { 0,   "list-artists", 0 },

	[FLAG_RAW]          = { 'C', "raw",          0 },
	{ 0 }
};

struct handler {
	void (*handler0)(void);
	void (*handler1)(const char *);
	const char *arg;
} handlers[] = {
	[FLAG_PLAY]         = { cmus_play           },
	[FLAG_PAUSE]        = { cmus_pause          },
	[FLAG_STOP]         = { cmus_stop           },
	[FLAG_NEXT]         = { cmus_next           },
	[FLAG_PREV]         = { cmus_prev           },
	[FLAG_REPEAT]       = { cmus_toggle_repeat  },
	[FLAG_SHUFFLE]      = { cmus_toggle_shuffle },
	[FLAG_VOLUME]       = { NULL,               cmus_set_volume },
	[FLAG_SEEK]         = { NULL,               cmus_seek       },
	[FLAG_QUERY]        = { cmus_query_old      },
	[FLAG_ARTIST]       = { cmus_artist         },
	[FLAG_LIST_ARTISTS] = { cmus_list_artists   },
	[FLAG_ALBUM]        = { cmus_album          },
	[FLAG_TITLE]        = { cmus_title          },
	[FLAG_CLEAR]        = { cmus_clear_playlist },
	[FLAG_FILE]         = { NULL,               cmus_play_file  },
};

static int flags[N_ELEMENTS(options)];

static const char *usage =
"Usage: %s [OPTION]... [FILE|DIR|PLAYLIST]...\n"
"   or: %s -C COMMAND...\n"
"   or: %s\n"
"Control cmus through socket.\n"
"\n"
"      --help           display this help and exit\n"
"      --version        " VERSION "\n"
"\n"
"Cooked mode:\n"
"  -p, --play           player-play\n"
"  -u, --pause          player-pause\n"
"  -s, --stop           player-stop\n"
"  -n, --next           player-next\n"
"  -r, --prev           player-prev\n"
"  -f, --file           player-play FILE\n"
"  -R, --repeat         toggle repeat\n"
"  -S, --shuffle        toggle shuffle\n"
"  -v, --volume VOL     vol VOL\n"
"  -k, --seek SEEK      seek SEEK\n"
"  -Q, --query          get player status (same as -C status)\n"
"\n"
"  -l, --library        modify library instead of playlist\n"
"  -P, --playlist       modify playlist (default)\n"
"  -q, --queue          modify play queue instead of playlist\n"
"  -c, --clear          clear playlist, library (-l) or play queue (-q)\n"
"\n"
"  Add FILE/DIR/PLAYLIST to playlist, library (-l) or play queue (-q).\n"
"\n"
"Raw mode:\n"
"  -C, --raw            treat arguments (instead of stdin) as raw commands\n"
"\n"
"  By default cmus-remote reads raw commands from stdin (one command per line).\n"
"\n"
"Report bugs to <cmus-devel@lists.sourceforge.net>.\n";

int main(int argc, char *argv[])
{
	bool raw_args = false;
	bool cmds = false;
	void (*add_func)(const char *) = cmus_add_to_playlist;

	program_name = argv[0];
	argv++;
	while (1) {
		int idx;
		char *arg;

		idx = get_option(&argv, options, &arg);
		if (idx < 0)
			break;

		flags[idx] = 1;
		switch ((enum flags)idx) {
		case FLAG_HELP:
			printf(usage, program_name, program_name, program_name);
			return 0;
		case FLAG_VERSION:
			printf("cmus " VERSION
			       "\nCopyright 2004-2006 Timo Hirvonen"
			       "\nCopyright 2008-2014 Various Authors\n");
			return 0;
		case FLAG_SERVER:
			warn("The server flag is deprecated in this version of cmus-remote and will be ignored.\n");
			break;
		case FLAG_PASSWD:
			warn("The passwd flag is deprecated in this version of cmus-remote and will be ignored.\n");
			break;
		case FLAG_VOLUME:
			handlers[FLAG_VOLUME].arg = arg;
			cmds = true;
			break;
		case FLAG_SEEK:
			handlers[FLAG_SEEK].arg = arg;
			cmds = true;
			break;
		case FLAG_FILE:
			handlers[FLAG_FILE].arg = arg;
			cmds = true;
			break;
		case FLAG_LIBRARY:
			add_func = cmus_add_to_library;
			handlers[FLAG_CLEAR].handler0 = cmus_clear_library;
			break;
		case FLAG_QUEUE:
			add_func = cmus_add_to_queue;
			handlers[FLAG_CLEAR].handler0 = cmus_clear_queue;
			break;
		case FLAG_PLAYLIST:
			add_func = cmus_add_to_playlist;
			handlers[FLAG_CLEAR].handler0 = cmus_clear_playlist;
			break;
		case FLAG_PLAY:  case FLAG_PAUSE:  case FLAG_STOP:    case FLAG_NEXT:
		case FLAG_PREV:  case FLAG_REPEAT: case FLAG_SHUFFLE: case FLAG_CLEAR:
		case FLAG_ALBUM: case FLAG_ARTIST: case FLAG_TITLE:   case FLAG_LIST_ARTISTS:
		case FLAG_QUERY:
			cmds = true;
			break;
		case FLAG_RAW:
			raw_args = true;
			break;
		}
	}

	if (cmds && raw_args)
		die("don't mix raw and cooked stuff\n");

	con = dbus_bus_get(DBUS_BUS_SESSION, NULL);
	if (con == NULL)
		die("cannot connect to dbus\n");
	cmus_hello();

	if (raw_args) {
		while (*argv) {
			printf("%s\n", *argv);
			cmus_cmd(*argv++);
		}
	} else if (!cmds && argv[0] == NULL) {
		char line[512];
		while (fgets(line, sizeof(line), stdin)) {
			line[strnlen(line, sizeof(line)-1) - 1] = 0;
			cmus_cmd(line);
		}
	} else {
		for (int i = 0; argv[i]; i++)
			add_func(file_url_absolute(argv[i]));
		for (int i = 0; i < N_ELEMENTS(handlers); i++) {
			struct handler hlr = handlers[i];
			if (flags[i]) {
				if (hlr.handler0)
					hlr.handler0();
				else if (hlr.handler1)
					hlr.handler1(hlr.arg);
			}
		}
	}
}
