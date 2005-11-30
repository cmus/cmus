/* 
 * Copyright 2005 Timo Hirvonen
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

#include <config.h>
#include <prog.h>
#include <file.h>
#include <path.h>
#include <remote.h>
#include <xmalloc.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

static int sock;

static void remote_connect(const char *server)
{
	struct sockaddr_un addr;

	sock = socket(PF_UNIX, SOCK_STREAM, 0);
	if (sock == -1)
		die_errno("socket");
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, server, sizeof(addr.sun_path) - 1);
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr))) {
		if (errno == ENOENT || errno == ECONNREFUSED)
			die(PACKAGE " is not running\n");
		die_errno("connect");
	}
}

static void remote_send_cmd(enum remote_command cmd, void *data, size_t data_size)
{
	struct remote_command_header cmd_header;

	cmd_header.cmd = cmd;
	cmd_header.data_size = data_size;
	if (write_all(sock, &cmd_header, sizeof(struct remote_command_header)) == -1)
		die_errno("write");
	if (data_size > 0) {
		if (write_all(sock, data, data_size) == -1)
			die_errno("write");
	}
}

enum {
	FLAG_HELP,
	FLAG_VERSION,
	FLAG_SERVER,
	FLAG_PLAY,
	FLAG_PAUSE,
	FLAG_STOP,
	FLAG_NEXT,
	FLAG_PREV,
	FLAG_CLEAR,
	FLAG_CONTINUE,
	FLAG_REPEAT,
	FLAG_PLAY_MODE,
	FLAG_ENQUEUE,
	FLAG_VOLUME,
	FLAG_RESHUFFLE,
	FLAG_SEEK,
	NR_FLAGS
};

static struct option options[NR_FLAGS + 1] = {
	{ 0, "help", 0 },
	{ 0, "version", 0 },
	{ 0, "server", 1 },
	{ 'p', "play", 0 },
	{ 'u', "pause", 0 },
	{ 's', "stop", 0 },
	{ 'n', "next", 0 },
	{ 'r', "prev", 0 },
	{ 'c', "clear", 0 },
	{ 'C', "continue", 0 },
	{ 'R', "repeat", 0 },
	{ 'P', "play-mode", 0 },
	{ 'e', "enqueue", 0 },
	{ 'v', "volume", 1 },
	{ 0, "reshuffle", 0 },
	{ 0, "seek", 1 },
	{ 0, NULL, 0 }
};

static int flags[NR_FLAGS] = { 0, };

static const char *usage =
"Usage: %s [OPTION]... [FILE|DIR|PLAYLIST]...\n"
"Control cmus throught socket.\n"
"\n"
"Add FILE/DIR/PLAYLIST to playlist or enqueue if -e flag given.\n"
"\n"
"      --server SOCKET  connect using socket SOCKET instead of /tmp/cmus-$USER\n"
"      --help           display this help and exit\n"
"      --version        " VERSION "\n"
"\n"
"  -p, --play           start playing\n"
"  -u, --pause          toggle pause\n"
"  -s, --stop           stop playing\n"
"  -n, --next           skip forward in playlist\n"
"  -r, --prev           skip backwards in playlist\n"
"  -c, --clear          clear playlist\n"
"  -C, --continue       toggle continue\n"
"  -R, --repeat         toggle repeat\n"
"  -P, --play-mode      toggle play mode\n"
"  -e, --enqueue        enqueue instead of adding to playlist\n"
"  -v, --volume DELTA   increase/decrease volume\n"
"      --reshuffle      shuffle playlist again\n"
"      --seek SECONDS   seek\n"
"\n"
"Documentation: " DATADIR "/cmus/doc/cmus.html\n"
"Report bugs to <" PACKAGE_BUGREPORT ">.\n";

int main(int argc, char *argv[])
{
	char server_buf[256];
	char *server = NULL;
	int volume = 0;
	int seek = 0;
	int nr_commands = 0;
	int i;

	program_name = argv[0];
	argv++;
	while (1) {
		int idx;
		char *arg;

		idx = get_option(&argv, options, &arg);
		if (idx < 0)
			break;

		flags[idx] = 1;
		switch (idx) {
		case FLAG_HELP:
			printf(usage, program_name);
			return 0;
		case FLAG_VERSION:
			printf(PACKAGE " " VERSION "\nCopyright 2004-2005 Timo Hirvonen\n");
			return 0;
		case FLAG_SERVER:
			server = arg;
			break;
                case FLAG_VOLUME:
			{
				char *end;

				volume = strtol(arg, &end, 10);
				if (*arg == 0 || *end != 0 || volume == 0)
					die("argument for --volume must be non-zero integer\n");
				nr_commands++;
			}
                       break;
		case FLAG_SEEK:
			{
				char *end;

				seek = strtol(arg, &end, 10);
				if (*arg == 0 || *end != 0 || seek == 0)
					die("argument for --seek must be non-zero integer\n");
				nr_commands++;
			}
			break;
		default:
			nr_commands++;
			break;
		}
	}

	if (nr_commands == 0 && argv[0] == NULL)
		die("too few arguments\n");

	if (server == NULL) {
		const char *user_name;

		user_name = getenv("USER");
		if (user_name == NULL || user_name[0] == 0) {
			user_name = getenv("USERNAME");
			if (user_name == NULL || user_name[0] == 0)
				die("neither USER or USERNAME environment varible is set\n");
		}
		snprintf(server_buf, sizeof(server_buf), "/tmp/cmus-%s", user_name);
		server = server_buf;
	}

	remote_connect(server);
	if (flags[FLAG_CLEAR])
		remote_send_cmd(CMD_PLCLEAR, NULL, 0);
	for (i = 0; argv[i]; i++) {
		char *filename;

		if (strncmp(argv[i], "http://", 7) == 0) {
			filename = xstrdup(argv[i]);
		} else {
			filename = path_absolute(argv[i]);
			if (filename == NULL) {
				warn_errno("get_current_dir_name");
				continue;
			}
		}
		if (flags[FLAG_ENQUEUE]) {
			remote_send_cmd(CMD_ENQUEUE, filename, strlen(filename) + 1);
		} else {
			remote_send_cmd(CMD_PLADD, filename, strlen(filename) + 1);
		}
		free(filename);
	}
	if (flags[FLAG_CONTINUE])
		remote_send_cmd(CMD_TCONT, NULL, 0);
	if (flags[FLAG_REPEAT])
		remote_send_cmd(CMD_TREPEAT, NULL, 0);
	if (flags[FLAG_PLAY_MODE])
		remote_send_cmd(CMD_TPLAYMODE, NULL, 0);
	if (flags[FLAG_RESHUFFLE])
		remote_send_cmd(CMD_PLRESHUFFLE, NULL, 0);
	if (flags[FLAG_STOP])
		remote_send_cmd(CMD_STOP, NULL, 0);
	if (flags[FLAG_NEXT])
		remote_send_cmd(CMD_NEXT, NULL, 0);
	if (flags[FLAG_PREV])
		remote_send_cmd(CMD_PREV, NULL, 0);
	if (flags[FLAG_PLAY])
		remote_send_cmd(CMD_PLAY, NULL, 0);
	if (flags[FLAG_PAUSE])
		remote_send_cmd(CMD_PAUSE, NULL, 0);
	if (volume)
		remote_send_cmd(CMD_MIX_VOL, &volume, sizeof(int));
	if (seek)
		remote_send_cmd(CMD_SEEK, &seek, sizeof(int));
	return 0;
}
