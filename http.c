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

#include <http.h>
#include <file.h>
#include <debug.h>
#include <xmalloc.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>

static void buf_ensure_space(char **bufp, int *sizep, int *posp, int len)
{
	int size = *sizep;
	int pos = *posp;

	if (size - pos < len) {
		if (size == 0)
			size = 128;
		while (size - pos < len)
			size *= 2;
		*bufp = xrenew(char, *bufp, size);
		*sizep = size;
	}
}

static void buf_write(char **bufp, int *sizep, int *posp, const char *str)
{
	int len = strlen(str);

	buf_ensure_space(bufp, sizep, posp, len);
	memcpy(*bufp + *posp, str, len);
	*posp += len;
}

static void buf_write_ch(char **bufp, int *sizep, int *posp, char ch)
{
	buf_ensure_space(bufp, sizep, posp, 1);
	(*bufp)[*posp] = ch;
	*posp += 1;
}

/*
 * @uri is http://[user[:pass]@]host[:port][/path][?query]
 *
 * uri(7): If the URL supplies a user  name  but no  password, and the remote
 * server requests a password, the program interpreting the URL should request
 * one from the user.
 */
int http_parse_uri(const char *uri, char **userp, char **passp, char **hostp, int *portp, char **pathp)
{
	const char *str, *colon, *at, *slash, *host_start;
	char *user = NULL;
	char *pass = NULL;
	char *host = NULL;
	int port = 80;
	char *path = NULL;

	*userp = NULL;
	*passp = NULL;
	*hostp = NULL;
	*portp = -1;
	*pathp = NULL;

	if (strncmp(uri, "http://", 7))
		return -1;
	str = uri + 7;
	host_start = str;

	/* [/path] */
	slash = strchr(str, '/');
	if (slash) {
		path = xstrdup(slash);
	} else {
		path = xstrdup("/");
	}

	/* [user[:pass]@] */
	at = strchr(str, '@');
	if (at) {
		/* user[:pass]@ */
		host_start = at + 1;
		colon = strchr(str, ':');
		if (colon == NULL || colon > at) {
			/* user */
			user = xstrndup(str, at - str);
		} else {
			/* user:pass */
			user = xstrndup(str, colon - str);
			pass = xstrndup(colon + 1, at - (colon + 1));
		}
	}

	/* host[:port] */
	colon = strchr(host_start, ':');
	if (colon) {
		/* host:port */
		const char *start;

		host = xstrndup(host_start, colon - host_start);
		colon++;
		start = colon;

		port = 0;
		while (*colon >= '0' && *colon <= '9') {
			port *= 10;
			port += *colon - '0';
			colon++;
		}

		if (colon == start || (*colon != 0 && *colon != '/')) {
			free(user);
			free(pass);
			free(host);
			free(path);
			return -1;
		}
	} else {
		/* host */
		if (slash) {
			host = xstrndup(host_start, slash - host_start);
		} else {
			host = xstrdup(host_start);
		}
	}
	*userp = user;
	*passp = pass;
	*hostp = host;
	*portp = port;
	*pathp = path;
	return 0;
}

int http_open(const char *hostname, unsigned int port, int timeout_ms)
{
	struct hostent *hostent;
	struct sockaddr_in addr;
	struct timeval tv;
	int sock, save, flags;

	hostent = gethostbyname(hostname);
	if (hostent == NULL)
		return -1;
	if (hostent->h_length > sizeof(addr.sin_addr))
		hostent->h_length = sizeof(addr.sin_addr);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	memcpy(&addr.sin_addr, hostent->h_addr_list[0], hostent->h_length);

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == -1)
		return -1;

	flags = fcntl(sock, F_GETFL);
	if (fcntl(sock, F_SETFL, O_NONBLOCK) == -1)
		goto close_exit;

	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;
	while (1) {
		fd_set wfds;

		d_print("connecting. timeout=%ld s %ld us\n", tv.tv_sec, tv.tv_usec);
		if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0)
			break;
		if (errno != EAGAIN && errno != EINPROGRESS)
			goto close_exit;

		FD_ZERO(&wfds);
		FD_SET(sock, &wfds);
		while (1) {
			int rc;

			rc = select(sock + 1, NULL, &wfds, NULL, &tv);
			if (rc == -1) {
				if (errno != EINTR)
					goto close_exit;
				/* signalled */
				continue;
			}
			if (rc == 1) {
				/* socket ready */
				break;
			}
			if (tv.tv_sec == 0 && tv.tv_usec == 0) {
				errno = ETIMEDOUT;
				goto close_exit;
			}
		}
	}

	/* restore old flags */
	if (fcntl(sock, F_SETFL, flags) == -1)
		goto close_exit;
	return sock;
close_exit:
	save = errno;
	close(sock);
	errno = save;
	return -1;
}

static int http_write(int fd, const char *buf, int count, int timeout_ms)
{
	struct timeval tv;
	int pos = 0;

	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;
	while (1) {
		fd_set wfds;
		int rc;

		d_print("timeout=%ld s %ld us\n", tv.tv_sec, tv.tv_usec);

		FD_ZERO(&wfds);
		FD_SET(fd, &wfds);
		rc = select(fd + 1, NULL, &wfds, NULL, &tv);
		if (rc == -1) {
			if (errno != EINTR)
				return -1;
			/* signalled */
			continue;
		}
		if (rc == 1) {
			rc = write(fd, buf + pos, count - pos);
			if (rc == -1) {
				if (errno == EINTR || errno == EAGAIN)
					continue;
				return -1;
			}
			pos += rc;
			if (pos == count)
				return 0;
		} else if (tv.tv_sec == 0 && tv.tv_usec == 0) {
			errno = ETIMEDOUT;
			return -1;
		}
	}
}

static int read_timeout(int fd, int timeout_ms)
{
	struct timeval tv;

	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;
	while (1) {
		fd_set rfds;
		int rc;

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		rc = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (rc == -1) {
			if (errno != EINTR)
				return -1;
			/* signalled */
			continue;
		}
		if (rc == 1)
			return 0;
		if (tv.tv_sec == 0 && tv.tv_usec == 0) {
			errno = ETIMEDOUT;
			return -1;
		}
	}
}

/* reads response, ignores fscking carriage returns */
static int http_read_response(int fd, char **bufp, int *sizep, int *posp, int timeout_ms)
{
	if (read_timeout(fd, timeout_ms))
		return -1;
	while (1) {
		char *buf = *bufp;
		int pos = *posp;
		int rc;
		char ch;

		rc = read(fd, &ch, 1);
		if (rc == -1) {
			return -1;
		}
		if (rc == 0) {
			return -2;
		}
		if (ch == '\r')
			continue;
		if (ch == '\n' && pos > 0 && buf[pos - 1] == '\n') {
			buf_write_ch(bufp, sizep, posp, 0);
			return 0;
		}
		buf_write_ch(bufp, sizep, posp, ch);
	}
}

static int http_parse_response(const char *str, int *codep, char **reasonp, struct http_header **headersp)
{
	/* str is 0 terminated buffer of lines
	 * every line ends with '\n'
	 * no carriage returns
	 * no empty lines
	 */
	const char *end;
	char *reason;
	int code, i, count;
	struct http_header *h;

	*codep = -1;
	*reasonp = NULL;
	*headersp = NULL;

	if (strncmp(str, "HTTP/", 5) == 0) {
		str += 5;
		while (*str != ' ') {
			if (*str == '\n') {
				return -2;
			}
			str++;
		}
	} else if (strncmp(str, "ICY", 3) == 0) {
		str += 3;
	} else {
		return -2;
	}
	while (*str == ' ')
		str++;

	code = 0;
	for (i = 0; i < 3; i++) {
		if (*str < '0' || *str > '9') {
			return -2;
		}
		code *= 10;
		code += *str - '0';
		str++;
	}
	while (*str == ' ')
		str++;

	end = strchr(str, '\n');
	reason = xstrndup(str, end - str);
	str = end + 1;

	/* headers */
	count = 4;
	h = xnew(struct http_header, count);
	i = 0;
	while (*str) {
		const char *ptr;

		if (i == count - 1) {
			count *= 2;
			h = xrenew(struct http_header, h, count);
		}

		end = strchr(str, '\n');
		ptr = strchr(str, ':');
		if (ptr == NULL || ptr > end) {
			int j;

			for (j = 0; j < i; j++) {
				free(h[j].key);
				free(h[j].val);
			}
			free(h);
			free(reason);
			*reasonp = NULL;
			return -2;
		}
		h[i].key = xstrndup(str, ptr - str);
		ptr++;
		while (*ptr == ' ')
			ptr++;
		h[i].val = xstrndup(ptr, end - ptr);
		i++;
		str = end + 1;
	}
	h[i].key = NULL;
	h[i].val = NULL;
	*codep = code;
	*reasonp = reason;
	*headersp = h;
	return 0;
}

int http_get(int fd, const char *path, struct http_header *headers,
		int *codep, char **reasonp, struct http_header **ret_headersp,
		int timeout_ms)
{
	char *buf = NULL;
	int size = 0;
	int pos = 0;
	int i, rc, save;

	buf_write(&buf, &size, &pos, "GET ");
	buf_write(&buf, &size, &pos, path);
	buf_write(&buf, &size, &pos, " HTTP/1.0\r\n");
	for (i = 0; headers[i].key; i++) {
		buf_write(&buf, &size, &pos, headers[i].key);
		buf_write(&buf, &size, &pos, ": ");
		buf_write(&buf, &size, &pos, headers[i].val);
		buf_write(&buf, &size, &pos, "\r\n");
	}
	buf_write(&buf, &size, &pos, "\r\n");
	
	rc = http_write(fd, buf, pos, timeout_ms);
	if (rc)
		goto out;

	pos = 0;
	rc = http_read_response(fd, &buf, &size, &pos, timeout_ms);
	if (rc)
		goto out;

	rc = http_parse_response(buf, codep, reasonp, ret_headersp);
out:
	save = errno;
	free(buf);
	errno = save;
	return rc;
}

int http_read_body(int fd, char **bodyp, int timeout_ms)
{
	char *body = NULL;
	int size = 0;
	int pos = 0;

	*bodyp = NULL;
	if (read_timeout(fd, timeout_ms))
		return -1;
	while (1) {
		int rc;

		buf_ensure_space(&body, &size, &pos, 256);
		rc = read_all(fd, body + pos, size - pos);
		if (rc == -1) {
			free(body);
			return -1;
		}
		if (rc == 0) {
			buf_ensure_space(&body, &size, &pos, 1);
			body[pos] = 0;
			*bodyp = body;
			return 0;
		}
		pos += rc;
	}
}

const char *http_headers_get_value(const struct http_header *headers, const char *key)
{
	int i;

	for (i = 0; headers[i].key; i++) {
		if (strcasecmp(headers[i].key, key) == 0)
			return headers[i].val;
	}
	return NULL;
}

void http_headers_free(struct http_header *headers)
{
	int i;

	for (i = 0; headers[i].key; i++) {
		free(headers[i].key);
		free(headers[i].val);
	}
	free(headers);
}

char *base64_encode(const char *str)
{
	static const char t[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	int str_len, buf_len, i, s, d;
	char *buf;
	unsigned char b0, b1, b2;

	str_len = strlen(str);
	buf_len = (str_len + 2) / 3 * 4 + 1;
	buf = xnew(char, buf_len);
	s = 0;
	d = 0;
	for (i = 0; i < str_len / 3; i++) {
		b0 = str[s++];
		b1 = str[s++];
		b2 = str[s++];

		/* 6 ms bits of b0 */
		buf[d++] = t[b0 >> 2];

		/* 2 ls bits of b0 . 4 ms bits of b1 */
		buf[d++] = t[((b0 << 4) | (b1 >> 4)) & 0x3f];

		/* 4 ls bits of b1 . 2 ms bits of b2 */
		buf[d++] = t[((b1 << 2) | (b2 >> 6)) & 0x3f];

		/* 6 ls bits of b2 */
		buf[d++] = t[b2 & 0x3f];
	}
	switch (str_len % 3) {
	case 2:
		b0 = str[s++];
		b1 = str[s++];

		/* 6 ms bits of b0 */
		buf[d++] = t[b0 >> 2];

		/* 2 ls bits of b0 . 4 ms bits of b1 */
		buf[d++] = t[((b0 << 4) | (b1 >> 4)) & 0x3f];

		/* 4 ls bits of b1 */
		buf[d++] = t[(b1 << 2) & 0x3f];

		buf[d++] = '=';
		break;
	case 1:
		b0 = str[s++];

		/* 6 ms bits of b0 */
		buf[d++] = t[b0 >> 2];

		/* 2 ls bits of b0 */
		buf[d++] = t[(b0 << 4) & 0x3f];

		buf[d++] = '=';
		buf[d++] = '=';
		break;
	case 0:
		break;
	}
	buf[d++] = 0;
	return buf;
}
