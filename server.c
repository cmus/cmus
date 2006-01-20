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
#include <cmus.h>
#include <player.h>
#include <xmalloc.h>
#include <prog.h>
#include <file.h>
#include <lib.h>
#include <pl.h>
#include <ui_curses.h>
#include <command_mode.h>
#include <utils.h>
#include <debug.h>
#include <config.h>

#include <unistd.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>

/* add and clear work on a specific view */
static int server_view = TREE_VIEW;

static void cmd_library(void *data, size_t data_size)
{
	server_view = TREE_VIEW;
}

static void cmd_playlist(void *data, size_t data_size)
{
	server_view = PLAYLIST_VIEW;
}

static void cmd_queue(void *data, size_t data_size)
{
	server_view = QUEUE_VIEW;
}

static void cmd_play(void *data, size_t data_size)
{
	player_play();
}

static void cmd_pause(void *data, size_t data_size)
{
	player_pause();
}

static void cmd_stop(void *data, size_t data_size)
{
	player_stop();
}

static void cmd_next(void *data, size_t data_size)
{
	cmus_next();
}

static void cmd_prev(void *data, size_t data_size)
{
	cmus_prev();
}

static void cmd_seek(void *data, size_t data_size)
{
	int seconds;

	if (data_size != sizeof(int))
		return;
	seconds = *(int *)data;
	player_seek(seconds, SEEK_CUR);
}

static void cmd_tcont(void *data, size_t data_size)
{
	player_toggle_cont();
}

static void cmd_trepeat(void *data, size_t data_size)
{
	cmus_toggle_repeat();
}

static void cmd_tshuffle(void *data, size_t data_size)
{
	cmus_toggle_shuffle();
}

static void cmd_reshuffle(void *data, size_t data_size)
{
	lib_reshuffle();
	pl_reshuffle();
}

static void cmd_add(void *data, size_t data_size)
{
	char *ptr = data;

	view_add(server_view, ptr);
}

static void cmd_clear(void *data, size_t data_size)
{
	view_clear(server_view);
}

static void cmd_mix_vol(void *data, size_t data_size)
{
	int l, r, max_vol, volume_step;

	if (data_size != sizeof(int))
		return;
	volume_step = *(int *)data;

	player_get_volume(&l, &r, &max_vol);
	volume_step = scale_from_percentage(volume_step, max_vol);
	player_set_volume(l + volume_step, r + volume_step);
}

typedef void cmd_func_t(void *data, size_t data_size);

static cmd_func_t *commands[CMD_MAX] = {
	cmd_library,
	cmd_playlist,
	cmd_queue,

	cmd_play,
	cmd_pause,
	cmd_stop,
	cmd_next,
	cmd_prev,
	cmd_seek,
	cmd_tcont,
	cmd_trepeat,
	cmd_tshuffle,
	cmd_reshuffle,
	cmd_mix_vol,

	cmd_add,
	cmd_clear
};

static struct sockaddr_un addr;
static int remote_socket = -1;

#define MAX_CLIENTS 10

static void read_commands(int fd)
{
	struct remote_command_header header;
	void *data;
	int rc;

	do {
		rc = read_all(fd, &header, sizeof(struct remote_command_header));
		if (rc == -1) {
			d_print("read: %s\n", strerror(errno));
			return;
		} else if (rc == 0) {
			/* eof */
			return;
		} else if (rc != sizeof(struct remote_command_header)) {
			d_print("error: header size\n");
			return;
		}
		if (header.data_size > 0) {
			data = xmalloc(header.data_size);
			rc = read_all(fd, data, header.data_size);
			if (rc != header.data_size) {
				d_print("error: data size\n");
				free(data);
				return;
			}
		} else {
			data = NULL;
		}
		commands[header.cmd](data, header.data_size);
		free(data);
	} while (1);
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
