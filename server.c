/* 
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "server.h"
#include "prog.h"
#include "command_mode.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int server_socket;

static union {
	struct sockaddr sa;
	struct sockaddr_un un;
	struct sockaddr_in in;
} addr;

#define MAX_CLIENTS 10

static void read_commands(int fd)
{
	char buf[1024];
	int pos = 0;

	/* unix connection is secure, other insecure */
	run_only_safe_commands = addr.sa.sa_family != AF_UNIX;

	while (1) {
		int rc, s;

		rc = read(fd, buf + pos, sizeof(buf) - pos);
		if (rc == -1) {
			break;
		}
		if (rc == 0) {
			break;
		}
		pos += rc;

		s = 0;
		while (1) {
			int i;

			for (i = s; i < pos; i++) {
				if (buf[i] == '\n') {
					buf[i] = 0;
					run_command(buf + s);
					s = i + 1;
					break;
				}
			}
			if (i == pos)
				break;
		}
		memmove(buf, buf + s, pos - s);
		pos -= s;
	}

	run_only_safe_commands = 0;
}

int server_serve(void)
{
	struct sockaddr saddr;
	socklen_t saddr_size = sizeof(saddr);
	int fd;

	fd = accept(server_socket, &saddr, &saddr_size);
	if (fd == -1) {
		return -1;
	}
	read_commands(fd);
	close(fd);
	return 0;
}

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

void server_init(char *address)
{
	int port = DEFAULT_PORT;
	int addrlen;

	if (strchr(address, '/')) {
		addr.sa.sa_family = AF_UNIX;
		strncpy(addr.un.sun_path, address, sizeof(addr.un.sun_path) - 1);

		addrlen = sizeof(struct sockaddr_un);
	} else {
		char *s = strchr(address, ':');
		struct hostent *hent;

		/* Some kind of authentication is must.  Sending user/pass over
		 * insecure connection isn't wise either considering what you
		 * can do with cmus (anything).
		 */

		die("TCP/IP support is currently disabled. It's too dangerous.\n");

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

	server_socket = socket(addr.sa.sa_family, SOCK_STREAM, 0);
	if (server_socket == -1)
		die_errno("socket");

	if (bind(server_socket, &addr.sa, addrlen) == -1) {
		int sock;

		if (errno != EADDRINUSE)
			die_errno("bind");

		/* address already in use */
		if (addr.sa.sa_family != AF_UNIX)
			die("cmus is already listening on %s:%d\n", address, port);

		/* try to connect to server */
		sock = socket(AF_UNIX, SOCK_STREAM, 0);
		if (sock == -1)
			die_errno("socket");

		if (connect(sock, &addr.sa, addrlen) == -1) {
			if (errno != ENOENT && errno != ECONNREFUSED)
				die_errno("connect");

			/* server not running => dead socket */

			/* try to remove dead socket */
			if (unlink(addr.un.sun_path) == -1 && errno != ENOENT)
				die_errno("unlink");
			if (bind(server_socket, &addr.sa, addrlen) == -1)
				die_errno("bind");
		} else {
			/* server already running */
			die("cmus is already listening on socket %s\n", address);
		}
		close(sock);
	}

	if (listen(server_socket, MAX_CLIENTS) == -1)
		die_errno("listen");
}

void server_exit(void)
{
	close(server_socket);
	if (addr.sa.sa_family == AF_UNIX)
		unlink(addr.un.sun_path);
}
