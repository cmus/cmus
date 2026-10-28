/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Use an inherited terminal for the real command parser during tests.
 * No listening socket, audio device or user configuration is required. */
#define server_init unused_server_init
#define server_exit unused_server_exit
#include "../server.c"
#undef server_init
#undef server_exit

static int idle_write;
void server_init(char *address);
void server_exit(void);

void server_init(char *address)
{
	int idle[2];
	struct client *client = xnew(struct client, 1);

	if (pipe(idle))
		die_errno("test pipe");
	server_socket = idle[0];
	idle_write = idle[1];
	addr.sa.sa_family = AF_UNIX;
	client->fd = atoi(getenv("CMUS_TEST_CONTROL_FD"));
	client->authenticated = 1;
	fcntl(client->fd, F_SETFL, O_NONBLOCK);
	list_add_tail(&client->node, &client_head);
}

void server_exit(void)
{
	close(server_socket);
	close(idle_write);
}
