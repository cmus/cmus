/* 
 * Copyright Timo Hirvonen
 */

#ifndef _SERVER_H
#define _SERVER_H

#include "list.h"

struct client {
	struct list_head node;
	int fd;
	unsigned int authenticated : 1;
};

extern int server_socket;
extern struct list_head client_head;

void server_init(char *address);
void server_exit(void);
void server_accept(void);
void server_serve(struct client *client);

#endif
