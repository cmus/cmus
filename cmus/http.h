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

#ifndef _HTTP_H
#define _HTTP_H

/*
 * 1xx indicates an informational message only
 * 2xx indicates success of some kind
 * 3xx redirects the client to another URL
 * 4xx indicates an error on the client's part
 * 5xx indicates an error on the server's part
 */

struct http_header {
	char *key;
	char *val;
};

extern int http_parse_uri(const char *uri, char **userp, char **passp, char **hostp, int *portp, char **pathp);

extern int http_open(const char *hostname, unsigned int port, int timeout_ms);

/*
 * returns:  0 success
 *          -1 check errno
 *          -2 parse error
 */
extern int http_get(int fd, const char *path, struct http_header *headers,
		int *codep, char **errp, struct http_header **ret_headersp,
		int timeout_ms);

extern int http_read_body(int fd, char **bodyp, int timeout_ms);
extern const char *http_headers_get_value(const struct http_header *headers, const char *key);
extern void http_headers_free(struct http_header *headers);
extern char *base64_encode(const char *str);

#endif
