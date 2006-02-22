/* 
 * Copyright Timo Hirvonen
 */

#ifndef _SERVER_H
#define _SERVER_H

int remote_server_init(const char *address);
int remote_server_serve(void);
void remote_server_exit(void);

#endif
