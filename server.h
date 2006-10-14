/* 
 * Copyright Timo Hirvonen
 */

#ifndef _SERVER_H
#define _SERVER_H

extern int server_socket;

void server_init(char *address);
void server_exit(void);
int server_serve(void);

#endif
