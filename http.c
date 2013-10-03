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

#include "http.h"
#include "file.h"
#include "debug.h"
#include "xmalloc.h"
#include "gbuf.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>

/*
 * @uri is http://[user[:pass]@]host[:port][/path][?query]
 *
 * uri(7): If the URL supplies a user  name  but no  password, and the remote
 * server requests a password, the program interpreting the URL should request
 * one from the user.
 */
int http_parse_uri(const char *uri, struct http_uri *u)
{
	const char *str, *colon, *at, *slash, *host_start;

	/* initialize all fields */
	u->uri  = xstrdup(uri);
	u->user = NULL;
	u->pass = NULL;
	u->host = NULL;
	u->path = NULL;
	u->port = 80;

	if (strncmp(uri, "http://", 7))
		return -1;
	str = uri + 7;
	host_start = str;

	/* [/path] */
	slash = strchr(str, '/');
	if (slash) {
		u->path = xstrdup(slash);
	} else {
		u->path = xstrdup("/");
	}

	/* [user[:pass]@] */
	at = strchr(str, '@');
	if (at) {
		/* user[:pass]@ */
		host_start = at + 1;
		colon = strchr(str, ':');
		if (colon == NULL || colon > at) {
			/* user */
			u->user = xstrndup(str, at - str);
		} else {
			/* user:pass */
			u->user = xstrndup(str, colon - str);
			u->pass = xstrndup(colon + 1, at - (colon + 1));
		}
	}

	/* host[:port] */
	colon = strchr(host_start, ':');
	if (colon) {
		/* host:port */
		const char *start;
		int port;

		u->host = xstrndup(host_start, colon - host_start);
		colon++;
		start = colon;

		port = 0;
		while (*colon >= '0' && *colon <= '9') {
			port *= 10;
			port += *colon - '0';
			colon++;
		}
		u->port = port;

		if (colon == start || (*colon != 0 && *colon != '/')) {
			http_free_uri(u);
			return -1;
		}
	} else {
		/* host */
		if (slash) {
			u->host = xstrndup(host_start, slash - host_start);
		} else {
			u->host = xstrdup(host_start);
		}
	}
	return 0;
}

void http_free_uri(struct http_uri *u)
{
	free(u->uri);
	free(u->user);
	free(u->pass);
	free(u->host);
	free(u->path);

	u->uri  = NULL;
	u->user = NULL;
	u->pass = NULL;
	u->host = NULL;
	u->path = NULL;
}

int http_open(struct http_get *hg, int timeout_ms)
{
	const struct addrinfo hints = {
		.ai_socktype = SOCK_STREAM
	};
	struct addrinfo *result;
	union {
		struct sockaddr sa;
		struct sockaddr_storage sas;
	} addr;
	size_t addrlen;
	struct timeval tv;
	int save, flags, rc;
	char port[16];

	char *proxy = getenv("http_proxy");
	if (proxy) {
		hg->proxy = xnew(struct http_uri, 1);
		if (http_parse_uri(proxy, hg->proxy)) {
			d_print("Failed to parse HTTP proxy URI '%s'\n", proxy);
			return -1;
		}
	} else {
		hg->proxy = NULL;
	}

	snprintf(port, sizeof(port), "%d", hg->proxy ? hg->proxy->port : hg->uri.port);
	rc = getaddrinfo(hg->proxy ? hg->proxy->host : hg->uri.host, port, &hints, &result);
	if (rc != 0) {
		d_print("getaddrinfo: %s\n", gai_strerror(rc));
		return -1;
	}
	memcpy(&addr.sa, result->ai_addr, result->ai_addrlen);
	addrlen = result->ai_addrlen;
	freeaddrinfo(result);

	hg->fd = socket(addr.sa.sa_family, SOCK_STREAM, 0);
	if (hg->fd == -1)
		return -1;

	flags = fcntl(hg->fd, F_GETFL);
	if (fcntl(hg->fd, F_SETFL, O_NONBLOCK) == -1)
		goto close_exit;

	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;
	while (1) {
		fd_set wfds;

		d_print("connecting. timeout=%lld s %lld us\n", (long long)tv.tv_sec, (long long)tv.tv_usec);
		if (connect(hg->fd, &addr.sa, addrlen) == 0)
			break;
		if (errno == EISCONN)
			break;
		if (errno != EAGAIN && errno != EINPROGRESS)
			goto close_exit;

		FD_ZERO(&wfds);
		FD_SET(hg->fd, &wfds);
		while (1) {
			rc = select(hg->fd + 1, NULL, &wfds, NULL, &tv);
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
	if (fcntl(hg->fd, F_SETFL, flags) == -1)
		goto close_exit;
	return 0;
close_exit:
	save = errno;
	close(hg->fd);
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

		d_print("timeout=%lld s %lld us\n", (long long)tv.tv_sec, (long long)tv.tv_usec);

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
static int http_read_response(int fd, struct gbuf *buf, int timeout_ms)
{
	char prev = 0;

	if (read_timeout(fd, timeout_ms))
		return -1;
	while (1) {
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
		if (ch == '\n' && prev == '\n')
			return 0;
		gbuf_add_ch(buf, ch);
		prev = ch;
	}
}

static int http_parse_response(char *str, struct http_get *hg)
{
	/* str is 0 terminated buffer of lines
	 * every line ends with '\n'
	 * no carriage returns
	 * no empty lines
	 */
	GROWING_KEYVALS(h);
	char *end;

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

	hg->code = 0;
	while (*str >= '0' && *str <= '9') {
		hg->code *= 10;
		hg->code += *str - '0';
		str++;
	}
	if (!hg->code)
		return -2;
	while (*str == ' ')
		str++;

	end = strchr(str, '\n');
	hg->reason = xstrndup(str, end - str);
	str = end + 1;

	/* headers */
	while (*str) {
		char *ptr;

		end = strchr(str, '\n');
		ptr = strchr(str, ':');
		if (ptr == NULL || ptr > end) {
			free(hg->reason);
			hg->reason = NULL;
			keyvals_terminate(&h);
			keyvals_free(h.keyvals);
			return -2;
		}

		*ptr++ = 0;
		while (*ptr == ' ')
			ptr++;

		keyvals_add(&h, str, xstrndup(ptr, end - ptr));
		str = end + 1;
	}
	keyvals_terminate(&h);
	hg->headers = h.keyvals;
	return 0;
}

int http_get(struct http_get *hg, struct keyval *headers, int timeout_ms)
{
	GBUF(buf);
	int i, rc, save;

	gbuf_add_str(&buf, "GET ");
	gbuf_add_str(&buf, hg->proxy ? hg->uri.uri : hg->uri.path);
	gbuf_add_str(&buf, " HTTP/1.0\r\n");
	for (i = 0; headers[i].key; i++) {
		gbuf_add_str(&buf, headers[i].key);
		gbuf_add_str(&buf, ": ");
		gbuf_add_str(&buf, headers[i].val);
		gbuf_add_str(&buf, "\r\n");
	}
	gbuf_add_str(&buf, "\r\n");

	rc = http_write(hg->fd, buf.buffer, buf.len, timeout_ms);
	if (rc)
		goto out;

	gbuf_clear(&buf);
	rc = http_read_response(hg->fd, &buf, timeout_ms);
	if (rc)
		goto out;

	rc = http_parse_response(buf.buffer, hg);
out:
	save = errno;
	gbuf_free(&buf);
	errno = save;
	return rc;
}

char *http_read_body(int fd, size_t *size, int timeout_ms)
{
	GBUF(buf);

	if (read_timeout(fd, timeout_ms))
		return NULL;
	while (1) {
		int count = 1023;
		int rc;

		gbuf_grow(&buf, count);
		rc = read_all(fd, buf.buffer + buf.len, count);
		if (rc == -1) {
			gbuf_free(&buf);
			return NULL;
		}
		buf.len += rc;
		if (rc == 0) {
			*size = buf.len;
			return gbuf_steal(&buf);
		}
	}
}

void http_get_free(struct http_get *hg)
{
	http_free_uri(&hg->uri);
	if (hg->proxy) {
		http_free_uri(hg->proxy);
		free(hg->proxy);
	}
	if (hg->headers)
		keyvals_free(hg->headers);
	free(hg->reason);
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
