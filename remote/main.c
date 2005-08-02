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
#include <file.h>
#include <path.h>
#include <remote.h>
#include <get_option.h>
#include <xmalloc.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

char *program_name;

static int remote_connect(const char *server)
{
	struct sockaddr_un addr;
	int sock;

	sock = socket(PF_UNIX, SOCK_STREAM, 0);
	if (sock == -1) {
		perror("socket");
		exit(1);
	}
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, server, sizeof(addr.sun_path) - 1);
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr))) {
		if (errno == ENOENT || errno == ECONNREFUSED) {
			close(sock);
			fprintf(stderr, "%s: " PACKAGE " is not running\n"
					"Try `%s --help' for more information.\n",
					program_name, program_name);
			exit(255);
		}
		perror("connect");
		close(sock);
		exit(1);
	}
	return sock;
}

static int remote_send_cmd(int sock, enum remote_command cmd, void *data, size_t data_size)
{
	struct remote_command_header cmd_header;

	cmd_header.cmd = cmd;
	cmd_header.data_size = data_size;
	if (write_all(sock, &cmd_header, sizeof(struct remote_command_header)) == -1) {
		perror("write");
		return -1;
	}
	if (data_size > 0) {
		if (write_all(sock, data, data_size) == -1) {
			perror("write");
			return -1;
		}
	}
	return 0;
}

static void remote_close(int sock)
{
	close(sock);
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
	int sock, i;
	int volume = 0;
	int seek = 0;
	int nr_commands = 0;

	program_name = argv[0];
	argv++;
	while (1) {
		int rc, idx;
		char *arg;

		rc = get_option(&argv, options, 1, &idx, &arg);
		if (rc == 1)
			break;
		if (rc > 1)
			return 1;
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
				if (*arg == 0 || *end != 0 || volume == 0) {
					fprintf(stderr, "%s: argument for --volume must be non-zero integer\n", program_name);
					return 1;
				}
				nr_commands++;
			}
                       break;
		case FLAG_SEEK:
			{
				char *end;

				seek = strtol(arg, &end, 10);
				if (*arg == 0 || *end != 0 || seek == 0) {
					fprintf(stderr, "%s: argument for --seek must be non-zero integer\n", program_name);
					return 1;
				}
				nr_commands++;
			}
			break;
		default:
			nr_commands++;
			break;
		}
	}

	if (nr_commands == 0 && argv[0] == NULL) {
		fprintf(stderr, "%s: too few arguments\n"
				"Try `%s --help' for more information.\n",
				program_name, program_name);
		return 1;
	}

	if (server == NULL) {
		const char *user_name;

		user_name = getenv("USER");
		if (user_name == NULL || user_name[0] == 0) {
			user_name = getenv("USERNAME");
			if (user_name == NULL || user_name[0] == 0) {
				fprintf(stderr, "%s: neither USER or USERNAME environment varible is set\n", program_name);
				return 1;
			}
		}
		snprintf(server_buf, sizeof(server_buf), "/tmp/cmus-%s", user_name);
		server = server_buf;
	}

	sock = remote_connect(server);
	if (flags[FLAG_CLEAR])
		remote_send_cmd(sock, CMD_PLCLEAR, NULL, 0);
	for (i = 0; argv[i]; i++) {
		char *filename;

		if (strncmp(argv[i], "http://", 7) == 0) {
			filename = xstrdup(argv[i]);
		} else {
			filename = path_absolute(argv[i]);
			if (filename == NULL) {
				fprintf(stderr, "%s: get_current_dir_name: %s\n",
						program_name, strerror(errno));
				continue;
			}
		}
		if (flags[FLAG_ENQUEUE]) {
			remote_send_cmd(sock, CMD_ENQUEUE, filename, strlen(filename) + 1);
		} else {
			remote_send_cmd(sock, CMD_PLADD, filename, strlen(filename) + 1);
		}
		free(filename);
	}
	if (flags[FLAG_CONTINUE])
		remote_send_cmd(sock, CMD_TCONT, NULL, 0);
	if (flags[FLAG_REPEAT])
		remote_send_cmd(sock, CMD_TREPEAT, NULL, 0);
	if (flags[FLAG_PLAY_MODE])
		remote_send_cmd(sock, CMD_TPLAYMODE, NULL, 0);
	if (flags[FLAG_RESHUFFLE])
		remote_send_cmd(sock, CMD_PLRESHUFFLE, NULL, 0);
	if (flags[FLAG_PLAY])
		remote_send_cmd(sock, CMD_PLAY, NULL, 0);
	if (flags[FLAG_PAUSE])
		remote_send_cmd(sock, CMD_PAUSE, NULL, 0);
	if (flags[FLAG_STOP])
		remote_send_cmd(sock, CMD_STOP, NULL, 0);
	if (flags[FLAG_NEXT])
		remote_send_cmd(sock, CMD_NEXT, NULL, 0);
	if (flags[FLAG_PREV])
		remote_send_cmd(sock, CMD_PREV, NULL, 0);
	if (volume)
		remote_send_cmd(sock, CMD_MIX_VOL, &volume, sizeof(int));
	if (seek)
		remote_send_cmd(sock, CMD_SEEK, &seek, sizeof(int));
	remote_close(sock);
	return 0;
}
