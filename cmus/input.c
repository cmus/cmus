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

#include <input.h>
#include <ip.h>
#include <pcm.h>
#include <http.h>
#include <xmalloc.h>
#include <file.h>
#include <utils.h>
#include <symbol.h>
#include <pls.h>
#include <list.h>
#include <misc.h>
#include <debug.h>
#include <config.h>

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <dlfcn.h>

struct input_plugin {
	const struct input_plugin_ops *ops;
	struct input_plugin_data data;
	unsigned int open : 1;
	unsigned int eof : 1;
	int http_code;
	char *http_reason;

	/*
	 * pcm is converted to 16-bit signed little-endian stereo
	 * NOTE: no conversion is done if channels > 2 or bits > 16
	 */
	void (*pcm_convert)(char *, const char *, int);
	void (*pcm_convert_in_place)(char *, int);
	/*
	 * 4  if 8-bit mono
	 * 2  if 8-bit stereo or 16-bit mono
	 * 1  otherwise
	 */
	int pcm_convert_scale;
};

struct ip {
	struct list_head node;
	char *name;
	void *handle;

	const char * const *extensions;
	const char * const *mime_types;
	const struct input_plugin_ops *ops;
};

static const char * const plugin_dir = LIBDIR "/" PACKAGE "/ip";
static LIST_HEAD(ip_head);

/* timeouts (ms) */
static int http_connection_timeout = 5e3;
static int http_read_timeout = 5e3;

static const char *get_extension(const char *filename)
{
	const char *ext;

	ext = filename + strlen(filename) - 1;
	while (ext >= filename && *ext != '/') {
		if (*ext == '.') {
			ext++;
			return ext;
		}
		ext--;
	}
	return NULL;
}

static const struct input_plugin_ops *get_ops_by_filename(const char *filename)
{
	struct ip *ip;
	const char *ext;

	ext = get_extension(filename);
	if (ext == NULL)
		return NULL;
	list_for_each_entry(ip, &ip_head, node) {
		const char * const *exts = ip->extensions;
		int i;

		for (i = 0; exts[i]; i++) {
			if (strcasecmp(ext, exts[i]) == 0)
				return ip->ops;
		}
	}
	return NULL;
}

static const struct input_plugin_ops *get_ops_by_mime_type(const char *mime_type)
{
	struct ip *ip;

	list_for_each_entry(ip, &ip_head, node) {
		const char * const *types = ip->mime_types;
		int i;

		for (i = 0; types[i]; i++) {
			if (strcasecmp(mime_type, types[i]) == 0)
				return ip->ops;
		}
	}
	return NULL;
}

static int do_http_get(const char *uri, struct http_header **headersp, int *codep, char **reasonp)
{
	char *user, *pass, *host, *path, *reason;
	int port, sock, i, rc, code;
	struct http_header *h;

	*headersp = NULL;
	*codep = -1;
	*reasonp = NULL;

	if (http_parse_uri(uri, &user, &pass, &host, &port, &path))
		return -IP_ERROR_INVALID_URI;

/* 	d_print("%s -> '%s':'%s'@'%s':%d'%s'\n", uri, user, pass, host, port, path); */

	sock = http_open(host, port, http_connection_timeout);
	if (sock == -1) {
		free(user);
		free(pass);
		free(host);
		free(path);
		return -IP_ERROR_ERRNO;
	}

	h = xnew(struct http_header, 5);
	i = 0;
	h[i].key = xstrdup("Host");
	h[i].val = xstrdup(host);
	i++;
	h[i].key = xstrdup("User-Agent");
	h[i].val = xstrdup(PACKAGE "/" VERSION);
	i++;
	h[i].key = xstrdup("Icy-MetaData");
	h[i].val = xstrdup("1");
	i++;
	if (user && pass) {
		char buf[256];
		char *encoded;

		snprintf(buf, sizeof(buf), "%s:%s", user, pass);
		encoded = base64_encode(buf);
		if (encoded == NULL) {
			d_print("couldn't base64 encode '%s'\n", buf);
		} else {
			snprintf(buf, sizeof(buf), "Basic %s", encoded);
			free(encoded);
			h[i].key = xstrdup("Authorization");
			h[i].val = xstrdup(buf);
			i++;
		}
	}
	h[i].key = NULL;
	h[i].val = NULL;
	i++;

	rc = http_get(sock, path, h, &code, &reason, headersp, http_read_timeout);
	http_headers_free(h);
	free(user);
	free(pass);
	free(host);
	free(path);
	switch (rc) {
	case -1:
		d_print("error: %s\n", strerror(errno));
		close(sock);
		return -IP_ERROR_ERRNO;
	case -2:
		d_print("error parsing HTTP response\n");
		close(sock);
		return -IP_ERROR_HTTP_RESPONSE;
	}
	d_print("HTTP response: %d %s\n", code, reason);
	if (code != 200) {
		*codep = code;
		*reasonp = reason;
		close(sock);
		return -IP_ERROR_HTTP_STATUS;
	}
	free(reason);
	return sock;
}

static int setup_remote(struct input_plugin *ip, const struct http_header *headers, int sock)
{
	const char *val;

	val = http_headers_get_value(headers, "Content-Type");
	if (val) {
		ip->ops = get_ops_by_mime_type(val);
		if (ip->ops == NULL) {
			d_print("unsupported content type: %s\n", val);
			close(sock);
			return -IP_ERROR_FILE_FORMAT;
		}
	} else {
		const char *type = "audio/mpeg";

		d_print("assuming %s content type\n", type);
		ip->ops = get_ops_by_mime_type(type);
		if (ip->ops == NULL) {
			d_print("unsupported content type: %s\n", type);
			close(sock);
			return -IP_ERROR_FILE_FORMAT;
		}
	}

	ip->data.fd = sock;
	ip->data.metadata = (char *)xmalloc(16 * 255 + 1);

	val = http_headers_get_value(headers, "icy-metaint");
	if (val) {
		long int lint;

		if (str_to_int(val, &lint) == 0 && lint >= 0) {
			ip->data.metaint = lint;
			d_print("metaint: %d\n", ip->data.metaint);
		}
	}
	return 0;
}

static void dump_lines(char **lines)
{
	int i;

	for (i = 0; lines[i]; i++)
		d_print("%d='%s'\n", i, lines[i]);
}

static int read_pls(struct input_plugin *ip, int sock)
{
	struct http_header *headers;
	char *body, *reason;
	char **lines;
	int rc, code;

	rc = http_read_body(sock, &body, http_read_timeout);
	close(sock);
	if (rc)
		return -IP_ERROR_ERRNO;

	lines = pls_get_files(body);
	free(body);

	if (lines == NULL) {
		d_print("error parsing playlist\n");
		return -IP_ERROR_HTTP_RESPONSE;
	}
	dump_lines(lines);
	if (lines[0] == NULL) {
		free(lines);
		d_print("empty playlist\n");
		return -IP_ERROR_HTTP_RESPONSE;
	}

	sock = do_http_get(lines[0], &headers, &code, &reason);
	free_str_array(lines);
	if (sock < 0) {
		ip->http_code = code;
		ip->http_reason = reason;
		return sock;
	}

	rc = setup_remote(ip, headers, sock);
	http_headers_free(headers);
	return rc;
}

static int read_m3u(struct input_plugin *ip, int sock)
{
	struct http_header *headers;
	char *body, *reason;
	char **lines;
	int rc, code, i;

	rc = http_read_body(sock, &body, http_read_timeout);
	close(sock);
	if (rc)
		return -IP_ERROR_ERRNO;

	lines = bsplit(body, strlen(body), '\n', 0);
	free(body);

	for (i = 0; lines[i]; i++) {
		char *ptr = strchr(lines[i], '\r');

		if (ptr)
			*ptr = 0;
	}
	if (i > 0 && lines[i - 1][0] == 0) {
		free(lines[i - 1]);
		lines[i - 1] = NULL;
	}
	dump_lines(lines);

	if (lines[0] == NULL) {
		free(lines);
		d_print("empty playlist\n");
		return -IP_ERROR_HTTP_RESPONSE;
	}

	sock = do_http_get(lines[0], &headers, &code, &reason);
	free_str_array(lines);
	if (sock < 0) {
		ip->http_code = code;
		ip->http_reason = reason;

		if (sock == -IP_ERROR_INVALID_URI)
			sock = -IP_ERROR_HTTP_RESPONSE;
		return sock;
	}

	rc = setup_remote(ip, headers, sock);
	http_headers_free(headers);
	return rc;
}

static int open_remote(struct input_plugin *ip)
{
	struct input_plugin_data *d = &ip->data;
	char *reason;
	int sock, rc, code;
	struct http_header *headers;
	const char *val;

	sock = do_http_get(d->filename, &headers, &code, &reason);
	if (sock < 0) {
		ip->http_code = code;
		ip->http_reason = reason;
		return sock;
	}

	val = http_headers_get_value(headers, "Content-Type");
	if (val) {
		d_print("Content-Type: %s\n", val);
		if (strcasecmp(val, "audio/x-scpls") == 0) {
			http_headers_free(headers);
			return read_pls(ip, sock);
		} else if (strcasecmp(val, "audio/m3u") == 0) {
			http_headers_free(headers);
			return read_m3u(ip, sock);
		}
	}

	rc = setup_remote(ip, headers, sock);
	http_headers_free(headers);
	return rc;
}

static int open_file(struct input_plugin *ip)
{
	ip->ops = get_ops_by_filename(ip->data.filename);
	if (ip->ops == NULL)
		return -IP_ERROR_UNRECOGNIZED_FILE_TYPE;
	ip->data.fd = open(ip->data.filename, O_RDONLY);
	if (ip->data.fd == -1) {
		ip->ops = NULL;
		return -IP_ERROR_ERRNO;
	}
	return 0;
}

void ip_init_plugins(void)
{
	DIR *dir;
	struct dirent *d;

	dir = opendir(plugin_dir);
	if (dir == NULL) {
		warn_errno("couldn't open directory `%s'", plugin_dir);
		return;
	}
	while ((d = readdir(dir)) != NULL) {
		char filename[256];
		struct ip *ip;
		void *so;
		char *ext;

		if (d->d_name[0] == '.')
			continue;
		ext = strrchr(d->d_name, '.');
		if (ext == NULL)
			continue;
		if (strcmp(ext, ".so"))
			continue;

		snprintf(filename, sizeof(filename), "%s/%s", plugin_dir, d->d_name);

		so = dlopen(filename, RTLD_NOW);
		if (so == NULL) {
			warn("%s\n", dlerror());
			continue;
		}

		ip = xnew(struct ip, 1);

		if (!get_symbol(so, "ip_extensions", filename, (void **)&ip->extensions, 0)) {
			free(ip);
			dlclose(so);
			continue;
		}

		if (!get_symbol(so, "ip_mime_types", filename, (void **)&ip->mime_types, 0)) {
			free(ip);
			dlclose(so);
			continue;
		}

		if (!get_symbol(so, "ip_ops", filename, (void **)&ip->ops, 0)) {
			free(ip);
			dlclose(so);
			continue;
		}

		ip->name = xstrndup(d->d_name, ext - d->d_name);
		ip->handle = so;

		list_add_tail(&ip->node, &ip_head);
	}
	closedir(dir);
}

struct input_plugin *ip_new(const char *filename)
{
	struct input_plugin *ip = xnew(struct input_plugin, 1);

	ip->ops = NULL;
	ip->open = 0;
	ip->eof = 0;
	ip->http_code = -1;
	ip->http_reason = NULL;

	ip->data.filename = xstrdup(filename);
	ip->data.fd = -1;

	ip->data.remote = is_url(filename);
	ip->data.metadata_changed = 0;
	ip->data.counter = 0;
	ip->data.metaint = 0;
	ip->data.metadata = NULL;

	ip->data.private = NULL;
	return ip;
}

void ip_delete(struct input_plugin *ip)
{
	if (ip->open)
		ip_close(ip);
	free(ip->data.filename);
	free(ip);
}

int ip_open(struct input_plugin *ip)
{
	int rc, bits, is_signed, channels;

	BUG_ON(ip->open);
	BUG_ON(ip->eof);
	BUG_ON(ip->ops);
	BUG_ON(ip->data.filename == NULL);
	BUG_ON(ip->data.fd != -1);

	/* set fd and ops */
	if (ip->data.remote) {
		rc = open_remote(ip);
	} else {
		rc = open_file(ip);
	}

	if (rc) {
		d_print("opening `%s' failed: %d %s\n", ip->data.filename, rc, rc == -1 ? strerror(errno) : "");
		return rc;
	}

	BUG_ON(ip->data.fd == -1);
	BUG_ON(ip->ops == NULL);

	BUG_ON(ip->ops->open == NULL);
	BUG_ON(ip->ops->close == NULL);
	BUG_ON(ip->ops->read == NULL);
	BUG_ON(ip->ops->seek == NULL);
	BUG_ON(ip->ops->read_comments == NULL);
	BUG_ON(ip->ops->duration == NULL);

	rc = ip->ops->open(&ip->data);
	if (rc) {
		d_print("opening file `%s' failed: %d %s\n", ip->data.filename, rc, rc == -1 ? strerror(errno) : "");
		if (ip->data.fd != -1)
			close(ip->data.fd);
		ip->data.fd = -1;
		ip->ops = NULL;
		free(ip->data.metadata);
		ip->data.metadata = NULL;
		return rc;
	}

	ip->pcm_convert_scale = 1;
	ip->pcm_convert = NULL;
	ip->pcm_convert_in_place = NULL;
	bits = sf_get_bits(ip->data.sf);
	is_signed = sf_get_signed(ip->data.sf);
	channels = sf_get_channels(ip->data.sf);
	if (bits == 8) {
		if (channels == 1) {
			ip->pcm_convert_scale = 4;
			if (is_signed) {
				ip->pcm_convert = convert_s8_1ch_to_s16_2ch;
			} else {
				ip->pcm_convert = convert_u8_1ch_to_s16_2ch;
			}
		} else if (channels == 2) {
			ip->pcm_convert_scale = 2;
			if (is_signed) {
				ip->pcm_convert = convert_s8_2ch_to_s16_2ch;
			} else {
				ip->pcm_convert = convert_u8_2ch_to_s16_2ch;
			}
		}
	} else if (bits == 16) {
		if (channels == 1) {
			ip->pcm_convert_scale = 2;
			ip->pcm_convert = convert_16_1ch_to_16_2ch;
		}
		if (channels <= 2) {
			int bigendian = sf_get_bigendian(ip->data.sf);

			if (is_signed) {
				if (bigendian)
					ip->pcm_convert_in_place = convert_s16_be_to_s16_le;
			} else {
				if (bigendian) {
					ip->pcm_convert_in_place = convert_u16_be_to_s16_le;
				} else {
					ip->pcm_convert_in_place = convert_u16_le_to_s16_le;
				}
			}
		}
	}
	d_print("pcm convert: scale=%d convert=%d convert_in_place=%d\n",
			ip->pcm_convert_scale,
			ip->pcm_convert != NULL,
			ip->pcm_convert_in_place != NULL);

	ip->open = 1;
	return 0;
}

int ip_close(struct input_plugin *ip)
{
	int rc;

	BUG_ON(!ip->open);

	rc = ip->ops->close(&ip->data);
	BUG_ON(ip->data.private);
	if (ip->data.fd != -1)
		close(ip->data.fd);
	free(ip->data.metadata);
	free(ip->http_reason);
	ip->data.fd = -1;
	ip->data.metadata = NULL;
	ip->http_reason = NULL;
	ip->ops = NULL;
	ip->open = 0;
	ip->eof = 0;

	ip->pcm_convert_scale = -1;
	ip->pcm_convert = NULL;
	ip->pcm_convert_in_place = NULL;
	return rc;
}

int ip_read(struct input_plugin *ip, char *buffer, int count)
{
	struct timeval tv;
	fd_set readfds;
	/* 4608 seems to be optimal for mp3s, 4096 for oggs */
	char tmp[8 * 1024];
	char *buf;
	int rc;

	BUG_ON(!ip->open);
	BUG_ON(count <= 0);

	FD_ZERO(&readfds);
	FD_SET(ip->data.fd, &readfds);
	/* zero timeout -> return immediately */
	tv.tv_sec = 0;
	tv.tv_usec = 50e3;
	rc = select(ip->data.fd + 1, &readfds, NULL, NULL, &tv);
	if (rc == -1) {
		d_print("select: error: %s\n", strerror(errno));
		if (errno == EINTR)
			errno = EAGAIN;
		return -1;
	}
	if (rc == 0) {
		errno = EAGAIN;
		return -1;
	}

	buf = buffer;
	if (ip->pcm_convert_scale > 1) {
		/* use tmp buffer for 16-bit mono and 8-bit */
		buf = tmp;
		count /= ip->pcm_convert_scale;
		if (count > sizeof(tmp))
			count = sizeof(tmp);
	}

	rc = ip->ops->read(&ip->data, buf, count);
	if (rc == 0)
		ip->eof = 1;
	if (rc == -1)
		d_print("error: %s\n", strerror(errno));

	if (rc > 0) {
		int sample_size = sf_get_sample_size(ip->data.sf);

		if (ip->pcm_convert_in_place != NULL)
			ip->pcm_convert_in_place(buf, rc / sample_size);
		if (ip->pcm_convert != NULL)
			ip->pcm_convert(buffer, tmp, rc / sample_size);
		rc *= ip->pcm_convert_scale;
	}
	return rc;
}

int ip_seek(struct input_plugin *ip, double offset)
{
	int rc;

	BUG_ON(!ip->open);

	if (ip->data.remote)
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
	rc = ip->ops->seek(&ip->data, offset);
	if (rc == 0)
		ip->eof = 0;
	return rc;
}

int ip_read_comments(struct input_plugin *ip, struct keyval **comments)
{
	int rc;

	BUG_ON(!ip->open);

	rc = ip->ops->read_comments(&ip->data, comments);
	return rc;
}

int ip_duration(struct input_plugin *ip)
{
	int rc;

	BUG_ON(!ip->open);

	rc = ip->ops->duration(&ip->data);
	return rc;
}

sample_format_t ip_get_sf(struct input_plugin *ip)
{
	BUG_ON(!ip->open);
	return ip->data.sf;
}

const char *ip_get_filename(struct input_plugin *ip)
{
	return ip->data.filename;
}

const char *ip_get_metadata(struct input_plugin *ip)
{
	BUG_ON(!ip->open);
	return ip->data.metadata;
}

int ip_is_remote(struct input_plugin *ip)
{
	return ip->data.remote;
}

int ip_metadata_changed(struct input_plugin *ip)
{
	int ret = ip->data.metadata_changed;

	BUG_ON(!ip->open);
	ip->data.metadata_changed = 0;
	return ret;
}

int ip_eof(struct input_plugin *ip)
{
	BUG_ON(!ip->open);
	return ip->eof;
}

char *ip_get_error_msg(struct input_plugin *ip, int rc, const char *arg)
{
	char buffer[1024];

	switch (-rc) {
	case IP_ERROR_ERRNO:
		snprintf(buffer, sizeof(buffer), "%s: %s", arg, strerror(errno));
		break;
	case IP_ERROR_UNRECOGNIZED_FILE_TYPE:
		snprintf(buffer, sizeof(buffer),
				"%s: unrecognized filename extension", arg);
		break;
	case IP_ERROR_FUNCTION_NOT_SUPPORTED:
		snprintf(buffer, sizeof(buffer),
				"%s: function not supported", arg);
		break;
	case IP_ERROR_FILE_FORMAT:
		snprintf(buffer, sizeof(buffer),
				"%s: file format not supported or corrupted file",
				arg);
		break;
	case IP_ERROR_INVALID_URI:
		snprintf(buffer, sizeof(buffer), "%s: invalid URI", arg);
		break;
	case IP_ERROR_SAMPLE_FORMAT:
		snprintf(buffer, sizeof(buffer),
				"%s: input plugin doesn't support the sample format",
				arg);
		break;
	case IP_ERROR_HTTP_RESPONSE:
		snprintf(buffer, sizeof(buffer), "%s: invalid HTTP response", arg);
		break;
	case IP_ERROR_HTTP_STATUS:
		snprintf(buffer, sizeof(buffer), "%s: %d %s", arg, ip->http_code, ip->http_reason);
		free(ip->http_reason);
		ip->http_reason = NULL;
		ip->http_code = -1;
		break;
	case IP_ERROR_INTERNAL:
		snprintf(buffer, sizeof(buffer), "%s: internal error", arg);
		break;
	case IP_ERROR_SUCCESS:
	default:
		snprintf(buffer, sizeof(buffer),
				"%s: this is not an error (%d), this is a bug",
				arg, rc);
		break;
	}
	return xstrdup(buffer);
}

static int strptrcmp(const void *a, const void *b)
{
	const char *as = *(char **)a;
	const char *bs = *(char **)b;

	return strcmp(as, bs);
}

char **ip_get_supported_extensions(void)
{
	struct ip *ip;
	char **exts;
	int i, size;
	int count = 0;

	size = 8;
	exts = xnew(char *, size);
	list_for_each_entry(ip, &ip_head, node) {
		const char * const *e = ip->extensions;

		for (i = 0; e[i]; i++) {
			if (count == size - 1) {
				size *= 2;
				exts = xrenew(char *, exts, size);
			}
			exts[count++] = xstrdup(e[i]);
		}
	}
	exts[count] = NULL;
	qsort(exts, count, sizeof(char *), strptrcmp);
	return exts;
}

void ip_dump_plugins(void)
{
	struct ip *ip;
	int i;

	printf("Input Plugins: %s\n", plugin_dir);
	list_for_each_entry(ip, &ip_head, node) {
		printf("  %s:\n    File Types:", ip->name);
		for (i = 0; ip->extensions[i]; i++)
			printf(" %s", ip->extensions[i]);
		printf("\n    MIME Types:");
		for (i = 0; ip->mime_types[i]; i++)
			printf(" %s", ip->mime_types[i]);
		printf("\n");
	}
}
