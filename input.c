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

#include "convert.h"
#include "input.h"
#include "ip.h"
#include "pcm.h"
#include "http.h"
#include "xmalloc.h"
#include "file.h"
#include "path.h"
#include "utils.h"
#include "cmus.h"
#include "options.h"
#include "list.h"
#include "mergesort.h"
#include "misc.h"
#include "debug.h"
#include "ui_curses.h"
#include "locking.h"
#include "xstrjoin.h"

#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <dlfcn.h>
#include <strings.h>

struct input_plugin {
	const struct input_plugin_ops *ops;
	struct input_plugin_data data;
	unsigned int open : 1;
	unsigned int eof : 1;
	int http_code;
	char *http_reason;

	/* cached duration, -1 = unset */
	int duration;
	/* cached bitrate, -1 = unset */
	long bitrate;
	/* cached codec, NULL = unset */
	char *codec;
	/* cached codec_profile, NULL = unset */
	char *codec_profile;

	/*
	 * pcm is converted to 16-bit signed little-endian stereo
	 * NOTE: no conversion is done if channels > 2 or bits > 16
	 */
	void (*pcm_convert)(void *, const void *, int);
	void (*pcm_convert_in_place)(void *, int);
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

	int priority;
	const char * const *extensions;
	const char * const *mime_types;
	const struct input_plugin_ops *ops;
	const struct input_plugin_opt *options;
};

static const char *plugin_dir;
static LIST_HEAD(ip_head);

/* protects ip->priority and ip_head */
static pthread_rwlock_t ip_lock = CMUS_RWLOCK_INITIALIZER;

#define ip_rdlock() cmus_rwlock_rdlock(&ip_lock)
#define ip_wrlock() cmus_rwlock_wrlock(&ip_lock)
#define ip_unlock() cmus_rwlock_unlock(&ip_lock)

/* timeouts (ms) */
static int http_connection_timeout = 5e3;
static int http_read_timeout = 5e3;

static const char *pl_mime_types[] = {
	"audio/m3u",
	"audio/x-scpls",
	"audio/x-mpegurl"
};

static const struct input_plugin_ops *
get_ops_by_extension_locked(const char *ext, struct list_head **headp)
{
	struct list_head *node = *headp;

	for (node = node->next; node != &ip_head; node = node->next) {
		struct ip *ip = list_entry(node, struct ip, node);
		const char * const *exts = ip->extensions;
		int i;

		if (ip->priority <= 0) {
			break;
		}

		for (i = 0; exts[i]; i++) {
			if (strcasecmp(ext, exts[i]) == 0 || strcmp("*", exts[i]) == 0) {
				*headp = node;
				return ip->ops;
			}
		}
	}
	return NULL;
}

static const struct input_plugin_ops *
get_ops_by_extension(const char *ext, struct list_head **headp)
{
	ip_rdlock();
	const struct input_plugin_ops *rv = get_ops_by_extension_locked(ext,
			headp);
	ip_unlock();
	return rv;
}

static const struct input_plugin_ops *
get_ops_by_mime_type_locked(const char *mime_type)
{
	struct ip *ip;

	list_for_each_entry(ip, &ip_head, node) {
		const char * const *types = ip->mime_types;
		int i;

		if (ip->priority <= 0) {
			break;
		}

		for (i = 0; types[i]; i++) {
			if (strcasecmp(mime_type, types[i]) == 0)
				return ip->ops;
		}
	}
	return NULL;
}

static const struct input_plugin_ops *
get_ops_by_mime_type(const char *mime_type)
{
	ip_rdlock();
	const struct input_plugin_ops *rv =
		get_ops_by_mime_type_locked(mime_type);
	ip_unlock();
	return rv;
}

static void keyvals_add_basic_auth(struct growing_keyvals *c,
				   const char *user,
				   const char *pass,
				   const char *header)
{
	char buf[256];
	char *encoded;

	snprintf(buf, sizeof(buf), "%s:%s", user, pass);
	encoded = base64_encode(buf);
	if (encoded == NULL) {
		d_print("couldn't base64 encode '%s'\n", buf);
	} else {
		snprintf(buf, sizeof(buf), "Basic %s", encoded);
		free(encoded);
		keyvals_add(c, header, xstrdup(buf));
	}
}

static int do_http_get(struct http_get *hg, const char *uri, int redirections)
{
	GROWING_KEYVALS(h);
	int i, rc;
	const char *val;
	char *redirloc;

	d_print("%s\n", uri);

	hg->headers = NULL;
	hg->reason = NULL;
	hg->proxy = NULL;
	hg->code = -1;
	hg->fd = -1;
	if (http_parse_uri(uri, &hg->uri))
		return -IP_ERROR_INVALID_URI;

	if (http_open(hg, http_connection_timeout))
		return -IP_ERROR_ERRNO;

	keyvals_add(&h, "Host", xstrdup(hg->uri.host));
	if (hg->proxy && hg->proxy->user && hg->proxy->pass)
		keyvals_add_basic_auth(&h, hg->proxy->user, hg->proxy->pass, "Proxy-Authorization");
	keyvals_add(&h, "User-Agent", xstrdup("cmus/" VERSION));
	keyvals_add(&h, "Icy-MetaData", xstrdup("1"));
	if (hg->uri.user && hg->uri.pass)
		keyvals_add_basic_auth(&h, hg->uri.user, hg->uri.pass, "Authorization");
	keyvals_terminate(&h);

	rc = http_get(hg, h.keyvals, http_read_timeout);
	keyvals_free(h.keyvals);
	switch (rc) {
	case -1:
		return -IP_ERROR_ERRNO;
	case -2:
		return -IP_ERROR_HTTP_RESPONSE;
	}

	d_print("HTTP response: %d %s\n", hg->code, hg->reason);
	for (i = 0; hg->headers[i].key != NULL; i++)
		d_print("  %s: %s\n", hg->headers[i].key, hg->headers[i].val);

	switch (hg->code) {
	case 200: /* OK */
		return 0;
	/*
	 * 3xx Codes (Redirections)
	 *     unhandled: 300 Multiple Choices
	 */
	case 301: /* Moved Permanently */
	case 302: /* Found */
	case 303: /* See Other */
	case 307: /* Temporary Redirect */
		val = keyvals_get_val(hg->headers, "location");
		if (!val)
			return -IP_ERROR_HTTP_RESPONSE;

		redirections++;
		if (redirections > 2)
			return -IP_ERROR_HTTP_REDIRECT_LIMIT;

		redirloc = xstrdup(val);
		http_get_free(hg);
		close(hg->fd);

		rc = do_http_get(hg, redirloc, redirections);

		free(redirloc);
		return rc;
	default:
		return -IP_ERROR_HTTP_STATUS;
	}
}

static int setup_remote(struct input_plugin *ip, const struct keyval *headers, int sock)
{
	const char *val;

	val = keyvals_get_val(headers, "Content-Type");
	if (val) {
		d_print("Content-Type: %s\n", val);
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
	ip->data.metadata = xnew(char, 16 * 255 + 1);

	val = keyvals_get_val(headers, "icy-metaint");
	if (val) {
		long int lint;

		if (str_to_int(val, &lint) == 0 && lint >= 0) {
			ip->data.metaint = lint;
			d_print("metaint: %d\n", ip->data.metaint);
		}
	}

	val = keyvals_get_val(headers, "icy-name");
	if (val)
		ip->data.icy_name = to_utf8(val, icecast_default_charset);

	val = keyvals_get_val(headers, "icy-genre");
	if (val)
		ip->data.icy_genre = to_utf8(val, icecast_default_charset);

	val = keyvals_get_val(headers, "icy-url");
	if (val)
		ip->data.icy_url = to_utf8(val, icecast_default_charset);

	return 0;
}

struct read_playlist_data {
	struct input_plugin *ip;
	int rc;
	int count;
};

static int handle_line(void *data, const char *uri)
{
	struct read_playlist_data *rpd = data;
	struct http_get hg;

	rpd->count++;
	rpd->rc = do_http_get(&hg, uri, 0);
	if (rpd->rc) {
		rpd->ip->http_code = hg.code;
		rpd->ip->http_reason = hg.reason;
		if (hg.fd >= 0)
			close(hg.fd);

		hg.reason = NULL;
		http_get_free(&hg);
		return 0;
	}

	rpd->rc = setup_remote(rpd->ip, hg.headers, hg.fd);
	http_get_free(&hg);
	return 1;
}

static int read_playlist(struct input_plugin *ip, int sock)
{
	struct read_playlist_data rpd = { ip, 0, 0 };
	char *body;
	size_t size;

	body = http_read_body(sock, &size, http_read_timeout);
	close(sock);
	if (!body)
		return -IP_ERROR_ERRNO;

	cmus_playlist_for_each(body, size, 0, handle_line, &rpd);
	free(body);
	if (!rpd.count) {
		d_print("empty playlist\n");
		rpd.rc = -IP_ERROR_HTTP_RESPONSE;
	}
	return rpd.rc;
}

static int open_remote(struct input_plugin *ip)
{
	struct input_plugin_data *d = &ip->data;
	struct http_get hg;
	const char *val;
	int rc;

	rc = do_http_get(&hg, d->filename, 0);
	if (rc) {
		ip->http_code = hg.code;
		ip->http_reason = hg.reason;
		hg.reason = NULL;
		http_get_free(&hg);
		return rc;
	}

	val = keyvals_get_val(hg.headers, "Content-Type");
	if (val) {
		int i;

		for (i = 0; i < N_ELEMENTS(pl_mime_types); i++) {
			if (!strcasecmp(val, pl_mime_types[i])) {
				d_print("Content-Type: %s\n", val);
				http_get_free(&hg);
				return read_playlist(ip, hg.fd);
			}
		}
	}

	rc = setup_remote(ip, hg.headers, hg.fd);
	http_get_free(&hg);
	return rc;
}

static void ip_init(struct input_plugin *ip, char *filename)
{
	const struct input_plugin t = {
		.http_code          = -1,
		.pcm_convert_scale  = -1,
		.duration           = -1,
		.bitrate            = -1,
		.data = {
			.fd         = -1,
			.filename   = filename,
			.remote     = is_http_url(filename),
			.channel_map = CHANNEL_MAP_INIT
		}
	};
	*ip = t;
}

static void ip_reset(struct input_plugin *ip, int close_fd)
{
	int fd = ip->data.fd;
	free(ip->data.metadata);
	ip_init(ip, ip->data.filename);
	if (fd != -1) {
		if (close_fd)
			close(fd);
		else {
			lseek(fd, 0, SEEK_SET);
			ip->data.fd = fd;
		}
	}
}

static int open_file_locked(struct input_plugin *ip)
{
	const struct input_plugin_ops *ops;
	struct list_head *head = &ip_head;
	const char *ext;
	int rc = 0;

	ext = get_extension(ip->data.filename);
	if (!ext)
		return -IP_ERROR_UNRECOGNIZED_FILE_TYPE;

	ops = get_ops_by_extension(ext, &head);
	if (!ops)
		return -IP_ERROR_UNRECOGNIZED_FILE_TYPE;

	ip->data.fd = open(ip->data.filename, O_RDONLY);
	if (ip->data.fd == -1)
		return -IP_ERROR_ERRNO;

	while (1) {
		ip->ops = ops;
		rc = ip->ops->open(&ip->data);
		if (rc != -IP_ERROR_UNSUPPORTED_FILE_TYPE)
			break;

		ops = get_ops_by_extension(ext, &head);
		if (!ops)
			break;

		ip_reset(ip, 0);
		d_print("fallback: try next plugin for `%s'\n", ip->data.filename);
	}

	return rc;
}

static int open_file(struct input_plugin *ip)
{
	ip_rdlock();
	int rv = open_file_locked(ip);
	ip_unlock();
	return rv;
}

static int sort_ip(const struct list_head *a_, const struct list_head *b_)
{
	const struct ip *a = list_entry(a_, struct ip, node);
	const struct ip *b = list_entry(b_, struct ip, node);
	return b->priority - a->priority;
}

void ip_load_plugins(void)
{
	DIR *dir;
	struct dirent *d;

	plugin_dir = xstrjoin(cmus_lib_dir, "/ip");
	dir = opendir(plugin_dir);
	if (dir == NULL) {
		error_msg("couldn't open directory `%s': %s", plugin_dir, strerror(errno));
		return;
	}

	ip_wrlock();
	while ((d = (struct dirent *) readdir(dir)) != NULL) {
		char filename[512];
		struct ip *ip;
		void *so;
		char *ext;
		const int *priority_ptr;
		const unsigned *abi_version_ptr;
		bool err = false;

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
			d_print("%s: %s\n", filename, dlerror());
			continue;
		}

		ip = xnew(struct ip, 1);

		abi_version_ptr = dlsym(so, "ip_abi_version");
		priority_ptr = dlsym(so, "ip_priority");
		ip->extensions = dlsym(so, "ip_extensions");
		ip->mime_types = dlsym(so, "ip_mime_types");
		ip->ops = dlsym(so, "ip_ops");
		ip->options = dlsym(so, "ip_options");
		if (!priority_ptr || !ip->extensions || !ip->mime_types || !ip->ops || !ip->options) {
			error_msg("%s: missing symbol", filename);
			err = true;
		}
		if (!abi_version_ptr || *abi_version_ptr != IP_ABI_VERSION) {
			error_msg("%s: incompatible plugin version", filename);
			err = true;
		}
		if (err) {
			free(ip);
			dlclose(so);
			continue;
		}
		ip->priority = *priority_ptr;

		ip->name = xstrndup(d->d_name, ext - d->d_name);
		ip->handle = so;

		list_add_tail(&ip->node, &ip_head);
	}
	list_mergesort(&ip_head, sort_ip);
	closedir(dir);
	ip_unlock();
}

struct input_plugin *ip_new(const char *filename)
{
	struct input_plugin *ip = xnew(struct input_plugin, 1);

	ip_init(ip, xstrdup(filename));
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
	int rc;

	BUG_ON(ip->open);

	/* set fd and ops, call ops->open */
	if (ip->data.remote) {
		rc = open_remote(ip);
		if (rc == 0)
			rc = ip->ops->open(&ip->data);
	} else {
		if (is_cdda_url(ip->data.filename)) {
			ip->ops = get_ops_by_mime_type("x-content/audio-cdda");
			rc = ip->ops ? ip->ops->open(&ip->data) : 1;
		} else if (is_cue_url(ip->data.filename)) {
			ip->ops = get_ops_by_mime_type("application/x-cue");
			rc = ip->ops ? ip->ops->open(&ip->data) : 1;
		} else
			rc = open_file(ip);
	}

	if (rc) {
		d_print("opening `%s' failed: %d %s\n", ip->data.filename, rc,
				rc == -1 ? strerror(errno) : "");
		ip_reset(ip, 1);
		return rc;
	}
	ip->open = 1;
	return 0;
}

void ip_setup(struct input_plugin *ip)
{
	unsigned int bits, is_signed, channels;
	sample_format_t sf = ip->data.sf;

	bits = sf_get_bits(sf);
	is_signed = sf_get_signed(sf);
	channels = sf_get_channels(sf);

	ip->pcm_convert_scale = 1;
	ip->pcm_convert = NULL;
	ip->pcm_convert_in_place = NULL;

	if (bits <= 16 && channels <= 2) {
		unsigned int mask = ((bits >> 2) & 4) | (is_signed << 1);

		ip->pcm_convert = pcm_conv[mask | (channels - 1)];
		ip->pcm_convert_in_place = pcm_conv_in_place[mask | sf_get_bigendian(sf)];

		ip->pcm_convert_scale = (3 - channels) * (3 - bits / 8);
	}

	d_print("pcm convert: scale=%d convert=%d convert_in_place=%d\n",
			ip->pcm_convert_scale,
			ip->pcm_convert != NULL,
			ip->pcm_convert_in_place != NULL);
}

int ip_close(struct input_plugin *ip)
{
	int rc;

	rc = ip->ops->close(&ip->data);
	BUG_ON(ip->data.private);
	if (ip->data.fd != -1)
		close(ip->data.fd);
	free(ip->data.metadata);
	free(ip->data.icy_name);
	free(ip->data.icy_genre);
	free(ip->data.icy_url);
	free(ip->http_reason);

	ip_init(ip, ip->data.filename);
	return rc;
}

int ip_read(struct input_plugin *ip, char *buffer, int count)
{
	struct timeval tv;
	fd_set readfds;
	/* 4608 seems to be optimal for mp3s, 4096 for oggs */
	char tmp[8 * 1024];
	char *buf;
	int sample_size;
	int rc;

	BUG_ON(count <= 0);

	FD_ZERO(&readfds);
	FD_SET(ip->data.fd, &readfds);
	/* zero timeout -> return immediately */
	tv.tv_sec = 0;
	tv.tv_usec = 50e3;
	rc = select(ip->data.fd + 1, &readfds, NULL, NULL, &tv);
	if (rc == -1) {
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
	if (rc == -1 && (errno == EAGAIN || errno == EINTR)) {
		errno = EAGAIN;
		return -1;
	}
	if (rc <= 0) {
		ip->eof = 1;
		return rc;
	}

	BUG_ON(rc % sf_get_frame_size(ip->data.sf) != 0);

	sample_size = sf_get_sample_size(ip->data.sf);
	if (ip->pcm_convert_in_place != NULL)
		ip->pcm_convert_in_place(buf, rc / sample_size);
	if (ip->pcm_convert != NULL)
		ip->pcm_convert(buffer, tmp, rc / sample_size);
	return rc * ip->pcm_convert_scale;
}

int ip_seek(struct input_plugin *ip, double offset)
{
	int rc;

	if (ip->data.remote)
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
	rc = ip->ops->seek(&ip->data, offset);
	if (rc == 0)
		ip->eof = 0;
	return rc;
}

int ip_read_comments(struct input_plugin *ip, struct keyval **comments)
{
	struct keyval *kv = NULL;
	int rc;

	rc = ip->ops->read_comments(&ip->data, &kv);

	if (ip->data.remote) {
		GROWING_KEYVALS(c);

		if (kv) {
			keyvals_init(&c, kv);
			keyvals_free(kv);
		}

		if (ip->data.icy_name && !keyvals_get_val_growing(&c, "title"))
			keyvals_add(&c, "title", xstrdup(ip->data.icy_name));

		if (ip->data.icy_genre && !keyvals_get_val_growing(&c, "genre"))
			keyvals_add(&c, "genre", xstrdup(ip->data.icy_genre));

		if (ip->data.icy_url && !keyvals_get_val_growing(&c, "comment"))
			keyvals_add(&c, "comment", xstrdup(ip->data.icy_url));

		keyvals_terminate(&c);

		kv = c.keyvals;
	}

	*comments = kv;

	return ip->data.remote ? 0 : rc;
}

int ip_duration(struct input_plugin *ip)
{
	if (ip->data.remote)
		return -1;
	if (ip->duration == -1)
		ip->duration = ip->ops->duration(&ip->data);
	if (ip->duration < 0)
		return -1;
	return ip->duration;
}

int ip_bitrate(struct input_plugin *ip)
{
	if (ip->data.remote)
		return -1;
	if (ip->bitrate == -1)
		ip->bitrate = ip->ops->bitrate(&ip->data);
	if (ip->bitrate < 0)
		return -1;
	return ip->bitrate;
}

int ip_current_bitrate(struct input_plugin *ip)
{
	return ip->ops->bitrate_current(&ip->data);
}

char *ip_codec(struct input_plugin *ip)
{
	if (ip->data.remote)
		return NULL;
	if (!ip->codec)
		ip->codec = ip->ops->codec(&ip->data);
	return ip->codec;
}

char *ip_codec_profile(struct input_plugin *ip)
{
	if (ip->data.remote)
		return NULL;
	if (!ip->codec_profile)
		ip->codec_profile = ip->ops->codec_profile(&ip->data);
	return ip->codec_profile;
}

sample_format_t ip_get_sf(struct input_plugin *ip)
{
	BUG_ON(!ip->open);
	return ip->data.sf;
}

void ip_get_channel_map(struct input_plugin *ip, channel_position_t *channel_map)
{
	BUG_ON(!ip->open);
	channel_map_copy(channel_map, ip->data.channel_map);
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

static void option_error(int rc)
{
	char *msg = ip_get_error_msg(NULL, rc, "setting option");
	error_msg("%s", msg);
	free(msg);
}

static void set_ip_option(void *data, const char *val)
{
	const struct input_plugin_opt *ipo = data;
	int rc;

	rc = ipo->set(val);
	if (rc)
		option_error(rc);
}

static void get_ip_option(void *data, char *buf, size_t size)
{
	const struct input_plugin_opt *ipo = data;
	char *val = NULL;

	ipo->get(&val);
	if (val) {
		strscpy(buf, val, size);
		free(val);
	}
}

static void set_ip_priority(void *data, const char *val)
{
	/* warn only once during the lifetime of the program. */
	static bool warned = false;
	long tmp;
	struct ip *ip = data;

	if (str_to_int(val, &tmp) == -1 || tmp < 0 || (long)(int)tmp != tmp) {
		error_msg("non-negative integer expected");
		return;
	}
	if (ui_initialized) {
		if (!warned) {
			static const char *msg =
				"Metadata might become inconsistent "
				"after this change. Continue? [y/N]";
			if (yes_no_query("%s", msg) != UI_QUERY_ANSWER_YES) {
				info_msg("Aborted");
				return;
			}
			warned = true;
		}
		info_msg("Run \":update-cache -f\" to refresh the metadata.");
	}

	ip_wrlock();
	ip->priority = (int)tmp;
	list_mergesort(&ip_head, sort_ip);
	ip_unlock();
}

static void get_ip_priority(void *data, char *val, size_t size)
{
	const struct ip *ip = data;
	ip_rdlock();
	snprintf(val, size, "%d", ip->priority);
	ip_unlock();
}

void ip_add_options(void)
{
	struct ip *ip;
	const struct input_plugin_opt *ipo;
	char key[64];

	ip_rdlock();
	list_for_each_entry(ip, &ip_head, node) {
		for (ipo = ip->options; ipo->name; ipo++) {
			snprintf(key, sizeof(key), "input.%s.%s", ip->name,
					ipo->name);
			option_add(xstrdup(key), ipo, get_ip_option,
					set_ip_option, NULL, 0);
		}
		snprintf(key, sizeof(key), "input.%s.priority", ip->name);
		option_add(xstrdup(key), ip, get_ip_priority, set_ip_priority, NULL, 0);
	}
	ip_unlock();
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
	case IP_ERROR_UNSUPPORTED_FILE_TYPE:
		snprintf(buffer, sizeof(buffer),
				"%s: unsupported file format", arg);
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
	case IP_ERROR_WRONG_DISC:
		snprintf(buffer, sizeof(buffer), "%s: wrong disc inserted, aborting!", arg);
		break;
	case IP_ERROR_NO_DISC:
		snprintf(buffer, sizeof(buffer), "%s: could not read disc", arg);
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
	case IP_ERROR_HTTP_REDIRECT_LIMIT:
		snprintf(buffer, sizeof(buffer), "%s: too many HTTP redirections", arg);
		break;
	case IP_ERROR_NOT_OPTION:
		snprintf(buffer, sizeof(buffer),
				"%s: no such option", arg);
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

char **ip_get_supported_extensions(void)
{
	struct ip *ip;
	char **exts;
	int i, size;
	int count = 0;

	size = 8;
	exts = xnew(char *, size);
	ip_rdlock();
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
	ip_unlock();
	exts[count] = NULL;
	qsort(exts, count, sizeof(char *), strptrcmp);
	return exts;
}

void ip_dump_plugins(void)
{
	struct ip *ip;
	int i;

	printf("Input Plugins: %s\n", plugin_dir);
	ip_rdlock();
	list_for_each_entry(ip, &ip_head, node) {
		printf("  %s:\n    Priority: %d\n    File Types:", ip->name, ip->priority);
		for (i = 0; ip->extensions[i]; i++)
			printf(" %s", ip->extensions[i]);
		printf("\n    MIME Types:");
		for (i = 0; ip->mime_types[i]; i++)
			printf(" %s", ip->mime_types[i]);
		printf("\n");
	}
	ip_unlock();
}
