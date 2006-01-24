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

#include <server.h>
#include <prog.h>
#include <command_mode.h>
#include <debug.h>
#include <config.h>

#include <unistd.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>

static struct sockaddr_un addr;
static int remote_socket = -1;

#define MAX_CLIENTS 10

static void read_commands(int fd)
{
	char buf[1024];
	int pos = 0;

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
}

int remote_server_serve(void)
{
	struct sockaddr saddr;
	socklen_t saddr_size = sizeof(saddr);
	int fd;

	fd = accept(remote_socket, &saddr, &saddr_size);
	if (fd == -1) {
		return -1;
	}
	read_commands(fd);
	close(fd);
	return 0;
}

int remote_server_init(const char *address)
{
	/* create socket - domain, type, protocol (IP) */
	remote_socket = socket(PF_UNIX, SOCK_STREAM, 0);
	if (remote_socket == -1)
		die_errno("socket");
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, address, sizeof(addr.sun_path) - 1);

	if (bind(remote_socket, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		int sock;

		if (errno != EADDRINUSE)
			die_errno("bind");

		/* address already in use */

		/* try to connect to server */
		sock = socket(PF_UNIX, SOCK_STREAM, 0);
		if (sock == -1)
			die_errno("socket");

		if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
			if (errno != ENOENT && errno != ECONNREFUSED)
				die_errno("connect");

			close(sock);
			/* server not running => dead socket */

			/* try to remove dead socket */
			if (unlink(addr.sun_path) == -1 && errno != ENOENT)
				die_errno("unlink");
			if (bind(remote_socket, (struct sockaddr *)&addr, sizeof(addr)) == -1)
				die_errno("bind");
		} else {
			/* server already running */
			close(sock);
			die(PACKAGE " is already listening on socket %s\n", address);
		}
	}
	/* start listening */
	BUG_ON(listen(remote_socket, MAX_CLIENTS) == -1);
	return remote_socket;
}

void remote_server_exit(void)
{
	close(remote_socket);
	if (unlink(addr.sun_path) == -1)
		d_print("unlink failed\n");
}
