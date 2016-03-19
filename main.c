/*
 * Copyright 2008-2013 Various Authors
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

#include "misc.h"
#include "prog.h"
#include "file.h"
#include "path.h"
#include "xmalloc.h"
#include "utils.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

static int sock;
static int raw_args = 0;
static char *passwd;

static int read_answer(void)
{
	char buf[8192];
	int got_nl = 0;
	int len = 0;

	while (1) {
		int rc = read(sock, buf, sizeof(buf));

		if (rc < 0) {
			warn_errno("read");
			break;
		}
		if (!rc)
			die("unexpected EOF\n");

		len += rc;

		// Last line should be empty (i.e. read "\n" or "...\n\n").
		// Write everything but the last \n to stdout.
		if (got_nl && buf[0] == '\n')
			break;
		if (len == 1 && buf[0] == '\n')
			break;
		if (rc > 1 && buf[rc - 1] == '\n' && buf[rc - 2] == '\n') {
			write_all(1, buf, rc - 1);
			break;
		}
		got_nl = buf[rc - 1] == '\n';
		write_all(1, buf, rc);
	}
	return len;
}

static int write_line(const char *line)
{
	if (write_all(sock, line, strlen(line)) == -1)
		die_errno("write");

	return read_answer();
}

static int send_cmd(const char *format, ...)
{
	char buf[512];
	va_list ap;

	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);

	return write_line(buf);
}

static int remote_connect(const char *address)
{
	union {
		struct sockaddr sa;
		struct sockaddr_un un;
		struct sockaddr_storage sas;
	} addr;
	size_t addrlen;

	if (strchr(address, '/')) {
		if (passwd)
			warn("password ignored for unix connections\n");
		passwd = NULL;

		addr.sa.sa_family = AF_UNIX;
		strncpy(addr.un.sun_path, address, sizeof(addr.un.sun_path) - 1);

		addrlen = sizeof(addr.un);
	} else {
		const struct addrinfo hints = {
			.ai_socktype = SOCK_STREAM
		};
		const char *port = STRINGIZE(DEFAULT_PORT);
		struct addrinfo *result;
		char *s = strrchr(address, ':');
		int rc;

		if (!passwd)
			die("password required for tcp/ip connection\n");

		if (s) {
			*s++ = 0;
			port = s;
		}

		rc = getaddrinfo(address, port, &hints, &result);
		if (rc != 0)
			die("getaddrinfo: %s\n", gai_strerror(rc));
		memcpy(&addr.sa, result->ai_addr, result->ai_addrlen);
		addrlen = result->ai_addrlen;
		freeaddrinfo(result);
	}

	sock = socket(addr.sa.sa_family, SOCK_STREAM, 0);
	if (sock == -1)
		die_errno("socket");

	if (connect(sock, &addr.sa, addrlen)) {
		if (errno == ENOENT || errno == ECONNREFUSED) {
			/* "cmus-remote -C" can be used to check if cmus is running */
			if (!raw_args)
				warn("cmus is not running\n");
			exit(1);
		}
		die_errno("connect");
	}

	if (passwd)
		return send_cmd("passwd %s\n", passwd) == 1;
	return 1;
}

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
	FLAG_PAUSE_PLAYBACK,
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

	FLAG_RAW
#define NR_FLAGS (FLAG_RAW + 1)
};

static struct option options[NR_FLAGS + 1] = {
	{ 0, "server", 1 },
	{ 0, "passwd", 1 },
	{ 0, "help", 0 },
	{ 0, "version", 0 },

	{ 'p', "play", 0 },
	{ 'u', "pause", 0 },
	{ 'U', "pause-playback", 0 },
	{ 's', "stop", 0 },
	{ 'n', "next", 0 },
	{ 'r', "prev", 0 },
	{ 'f', "file", 1 },
	{ 'R', "repeat", 0 },
	{ 'S', "shuffle", 0 },
	{ 'v', "volume", 1 },
	{ 'k', "seek", 1 },
	{ 'Q', "query", 0 },

	{ 'l', "library", 0 },
	{ 'P', "playlist", 0 },
	{ 'q', "queue", 0 },
	{ 'c', "clear", 0 },

	{ 'C', "raw", 0 },
	{ 0, NULL, 0 }
};

static int flags[NR_FLAGS] = { 0, };

static const char *usage =
"Usage: %s [OPTION]... [FILE|DIR|PLAYLIST]...\n"
"   or: %s -C COMMAND...\n"
"   or: %s\n"
"Control cmus through socket.\n"
"\n"
"      --server ADDR    connect using ADDR instead of $CMUS_SOCKET or $XDG_RUNTIME_DIR/cmus-socket\n"
"                       ADDR is either a UNIX socket or host[:port]\n"
"                       WARNING: using TCP/IP is insecure!\n"
"      --passwd PASSWD  password to use for TCP/IP connection\n"
"      --help           display this help and exit\n"
"      --version        " VERSION "\n"
"\n"
"Cooked mode:\n"
"  -p, --play           player-play\n"
"  -u, --pause          player-pause\n"
"  -U, --pause-playback player-pause-playback\n"
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
	char *server = NULL;
	char *play_file = NULL;
	char *volume = NULL;
	char *seek = NULL;
	int query = 0;
	int i, nr_cmds = 0;
	int context = 'p';

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
			       "\nCopyright 2008-2016 Various Authors\n");
			return 0;
		case FLAG_SERVER:
			server = arg;
			break;
		case FLAG_PASSWD:
			passwd = arg;
			break;
		case FLAG_VOLUME:
			volume = arg;
			nr_cmds++;
			break;
		case FLAG_SEEK:
			seek = arg;
			nr_cmds++;
			break;
		case FLAG_QUERY:
			query = 1;
			nr_cmds++;
			break;
		case FLAG_FILE:
			play_file = arg;
			nr_cmds++;
			break;
		case FLAG_LIBRARY:
			context = 'l';
			break;
		case FLAG_PLAYLIST:
			context = 'p';
			break;
		case FLAG_QUEUE:
			context = 'q';
			break;
		case FLAG_PLAY:
		case FLAG_PAUSE:
		case FLAG_PAUSE_PLAYBACK:
		case FLAG_STOP:
		case FLAG_NEXT:
		case FLAG_PREV:
		case FLAG_REPEAT:
		case FLAG_SHUFFLE:
		case FLAG_CLEAR:
			nr_cmds++;
			break;
		case FLAG_RAW:
			raw_args = 1;
			break;
		}
	}

	if (nr_cmds && raw_args)
		die("don't mix raw and cooked stuff\n");

	misc_init();
	if (server == NULL)
		server = xstrdup(cmus_socket_path);

	if (!remote_connect(server))
		return 1;

	if (raw_args) {
		while (*argv)
			send_cmd("%s\n", *argv++);
		return 0;
	}

	if (nr_cmds == 0 && argv[0] == NULL) {
		char line[512];

		while (fgets(line, sizeof(line), stdin))
			write_line(line);
		return 0;
	}

	if (flags[FLAG_CLEAR])
		send_cmd("clear -%c\n", context);
	for (i = 0; argv[i]; i++) {
		char *filename = file_url_absolute(argv[i]);

		send_cmd("add -%c %s\n", context, filename);
		free(filename);
	}
	if (flags[FLAG_REPEAT])
		send_cmd("toggle repeat\n");
	if (flags[FLAG_SHUFFLE])
		send_cmd("toggle shuffle\n");
	if (flags[FLAG_STOP])
		send_cmd("player-stop\n");
	if (flags[FLAG_NEXT])
		send_cmd("player-next\n");
	if (flags[FLAG_PREV])
		send_cmd("player-prev\n");
	if (flags[FLAG_PLAY])
		send_cmd("player-play\n");
	if (flags[FLAG_PAUSE])
		send_cmd("player-pause\n");
	if (flags[FLAG_PAUSE_PLAYBACK])
		send_cmd("player-pause-playback\n");
	if (flags[FLAG_FILE])
		send_cmd("player-play %s\n", file_url_absolute(play_file));
	if (volume)
		send_cmd("vol %s\n", volume);
	if (seek)
		send_cmd("seek %s\n", seek);
	if (query)
		send_cmd("status\n");
	return 0;
}
