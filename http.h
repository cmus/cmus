/*
 * Copyright 2008-2013 Various Authors
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CMUS_HTTP_H
#define CMUS_HTTP_H

#include "keyval.h"

#include <stddef.h> /* size_t */
#include <stdbool.h>

/*
 * 1xx indicates an informational message only
 * 2xx indicates success of some kind
 * 3xx redirects the client to another URL
 * 4xx indicates an error on the client's part
 * 5xx indicates an error on the server's part
 */

struct http_uri {
	bool is_https;
	char *uri;
	char *user;
	char *pass;
	char *host;
	char *path_and_query;
	int port;
};

struct connection;
struct connection;
struct http_get {
	int is_https;
	struct http_uri uri;
	struct http_uri *proxy;
	int fd;
	struct keyval *headers;
	char *reason;
	int code;
};

int parse_uri(const char *uri, struct http_uri *u);

/* frees contents of @u, not @u itself */
void http_free_uri(struct http_uri *u);

int get_sockfd(struct connection *conn);
int socket_open(struct http_get *hg, int timeout_ms);
int connection_open(struct connection *conn, struct http_get *hg, int timeout_ms);
int connection_close(struct connection *conn);

int socket_write(struct connection *conn, const char *in_buf, int count);
int socket_read(struct connection *conn, char *out_buf, int count);

/*
 * returns:  0 success
 *          -1 check errno
 *          -2 parse error
 */
int http_get(struct connection *conn, struct http_get *hg, struct keyval *headers, int timeout_ms);
void http_get_free(struct http_get *hg);

char *http_read_body(struct connection *conn, size_t *size, int timeout_ms);
char *base64_encode(const char *str);

#endif
