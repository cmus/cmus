/* 
 * Copyright 2005-2006 Timo Hirvonen
 */

#include "prog.h"
#include "file.h"
#include "path.h"
#include "xmalloc.h"

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

static void gethostbyname_failed(void)
{
	const char *error = "Unknown error.";

	switch (h_errno) {
	case HOST_NOT_FOUND:
	case NO_ADDRESS:
		error = "Host not found.";
		break;
	case NO_RECOVERY:
		error = "A non-recoverable name server error.";
		break;
	case TRY_AGAIN:
		error = "A temporary error occurred on an authoritative name server.";
		break;
	}
	die("gethostbyname: %s\n", error);
}

static void write_line(const char *line)
{
	if (write_all(sock, line, strlen(line)) == -1)
		die_errno("write");
}

static void send_cmd(const char *format, ...)
{
	char buf[512];
	va_list ap;

	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);

	write_line(buf);
}

static void remote_connect(const char *address)
{
	union {
		struct sockaddr sa;
		struct sockaddr_un un;
		struct sockaddr_in in;
	} addr;
	int addrlen;

	if (strchr(address, '/')) {
		if (passwd)
			warn("password ignored for unix connections\n");
		passwd = NULL;

		addr.sa.sa_family = AF_UNIX;
		strncpy(addr.un.sun_path, address, sizeof(addr.un.sun_path) - 1);

		addrlen = sizeof(addr.un);
	} else {
		char *s = strchr(address, ':');
		int port = DEFAULT_PORT;
		struct hostent *hent;

		if (!passwd)
			die("password required for tcp/ip connection\n");

		if (s) {
			*s++ = 0;
			port = atoi(s);
		}
		hent = gethostbyname(address);
		if (!hent)
			gethostbyname_failed();

		addr.sa.sa_family = hent->h_addrtype;
		switch (addr.sa.sa_family) {
		case AF_INET:
			memcpy(&addr.in.sin_addr, hent->h_addr_list[0], hent->h_length);
			addr.in.sin_port = htons(port);

			addrlen = sizeof(addr.in);
			break;
		default:
			die("unsupported address type\n");
		}
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
		send_cmd("%s\n", passwd);
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
	FLAG_STOP,
	FLAG_NEXT,
	FLAG_PREV,
	FLAG_FILE,
	FLAG_REPEAT,
	FLAG_SHUFFLE,
	FLAG_VOLUME,
	FLAG_SEEK,

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
	{ 's', "stop", 0 },
	{ 'n', "next", 0 },
	{ 'r', "prev", 0 },
	{ 'f', "file", 1 },
	{ 'R', "repeat", 0 },
	{ 'S', "shuffle", 0 },
	{ 'v', "volume", 1 },
	{ 'k', "seek", 1 },

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
"      --server ADDR    connect using ADDR instead of ~/.cmus/socket\n"
"                       ADDR is either a UNIX socket or host[:port]\n"
"                       WARNING: using TCP/IP is insecure!\n"
"      --passwd PASSWD  password to use for TCP/IP connection\n"
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
	char server_buf[256];
	char *server = NULL;
	char *play_file = NULL;
	char *volume = NULL;
	char *seek = NULL;
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
			printf("cmus " VERSION "\nCopyright 2004-2006 Timo Hirvonen\n");
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

	if (server == NULL) {
		const char *config_dir = getenv("CMUS_HOME");

		if (config_dir && config_dir[0]) {
			snprintf(server_buf, sizeof(server_buf), "%s/socket", config_dir);
		} else {
			const char *home = getenv("HOME");

			if (!home)
				die("error: environment variable HOME not set\n");
			snprintf(server_buf, sizeof(server_buf), "%s/.cmus/socket", home);
		}
		server = server_buf;
	}

	remote_connect(server);

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
	if (flags[FLAG_FILE])
		send_cmd("player-play %s\n", file_url_absolute(play_file));
	if (volume)
		send_cmd("vol %s\n", volume);
	if (seek)
		send_cmd("seek %s\n", seek);
	return 0;
}
