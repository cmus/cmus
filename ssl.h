/*
 * Copyright (C) 2024 Various Authors
 *
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CMUS_SSL_H
#define CMUS_SSL_H

#include <openssl/types.h>
#include <stddef.h> /* size_t */
#include <sys/types.h> /* ssize_t */

struct connection;
typedef int (*connection_read)(struct connection*, char*, int);
typedef int (*connection_write)(struct connection*, const char*, int);
struct connection {
	int *fd_ref;
	SSL *ssl;
	connection_read read;
	connection_write write;
};

int init_ssl_context(void);

struct http_get;
int ssl_init(struct http_get *hg);
int ssl_connect(struct http_get *hg);
int ssl_close(SSL* ssl);
int handle_ssl_error(SSL* ssl, int ret);

int https_write(struct connection *conn, const char *buf, int count);
int https_read(struct connection *conn, char *buf, int count);
int socket_write(struct connection *conn, const char *in_buf, int count);
int socket_read(struct connection *conn, char *out_buf, int count);

#endif