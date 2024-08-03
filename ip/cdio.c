/*
 * Copyright 2011-2013 Various Authors
 * Copyright 2011 Johannes Weißl
 *
 * Based on cdda.c from XMMS2.
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

#include "../ip.h"
#include "../file.h"
#include "../xmalloc.h"
#include "../debug.h"
#include "../utils.h"
#include "../options.h"
#include "../comment.h"
#include "../discid.h"

#include <cdio/cdio.h>
#include <cdio/logging.h>
#if LIBCDIO_VERSION_NUM >= 90
#include <cdio/paranoia/cdda.h>
#else
#include <cdio/cdda.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#undef HAVE_CDDB

#ifdef HAVE_CONFIG
#include "../config/cdio.h"
#endif

#ifdef HAVE_CDDB
#include "../http.h"
#include "../xstrjoin.h"
#include <cddb/cddb.h>
#endif

#ifdef HAVE_CDDB
static char *cddb_url = NULL;
#endif

static struct {
	CdIo_t *cdio;
	cdrom_drive_t *drive;
	const char *disc_id;
	const char *device;
} cached;

struct cdda_private {
	CdIo_t *cdio;
	cdrom_drive_t *drive;
	char *disc_id;
	char *device;
	track_t track;
	lsn_t first_lsn;
	lsn_t last_lsn;
	lsn_t current_lsn;
	int first_read;

	char read_buf[CDIO_CD_FRAMESIZE_RAW];
	unsigned long buf_used;
};

static void libcdio_log(cdio_log_level_t level, const char *message)
{
	const char *level_names[] = { "DEBUG", "INFO", "WARN", "ERROR", "ASSERT" };
	int len = strlen(message);
	if (len > 0 && message[len-1] == '\n')
		len--;
	if (len > 0) {
		level = clamp(level, 1, N_ELEMENTS(level_names));
		d_print("%s: %.*s\n", level_names[level-1], len, message);
	}
}

static int libcdio_open(struct input_plugin_data *ip_data)
{
	struct cdda_private *priv, priv_init = {
		.first_read = 1,
		.buf_used = CDIO_CD_FRAMESIZE_RAW
	};
	CdIo_t *cdio = NULL;
	cdrom_drive_t *drive = NULL;
	const char *device = cdda_device;
	lsn_t first_lsn;
	int track = -1;
	char *disc_id = NULL;
	char *msg = NULL;
	int rc = 0, save = 0;

	if (!parse_cdda_url(ip_data->filename, &disc_id, &track, NULL)) {
		rc = -IP_ERROR_INVALID_URI;
		goto end;
	}

	if (track == -1) {
		d_print("invalid or missing track number, aborting!\n");
		rc = -IP_ERROR_INVALID_URI;
		goto end;
	}

	/* In case of cue/toc/nrg, take filename (= disc_id) as device.
	 * A real disc_id is base64 encoded and never contains a slash */
	if (strchr(disc_id, '/'))
		device = disc_id;

	ip_data->fd = open(device, O_RDONLY);
	if (ip_data->fd == -1) {
		save = errno;
		d_print("could not open device %s\n", device);
		rc = -IP_ERROR_ERRNO;
		goto end;
	}

	if (cached.cdio && strcmp(disc_id, cached.disc_id) == 0 && strcmp(device, cached.device) == 0) {
		cdio = cached.cdio;
		drive = cached.drive;
	} else {
		cdio_log_set_handler(libcdio_log);
		cdio = cdio_open(device, DRIVER_UNKNOWN);
		if (!cdio) {
			d_print("failed to open device %s\n", device);
			rc = -IP_ERROR_NO_DISC;
			goto end;
		}
		cdio_set_speed(cdio, 1);

		drive = cdio_cddap_identify_cdio(cdio, CDDA_MESSAGE_LOGIT, &msg);
		if (!drive) {
			d_print("failed to identify drive, aborting!\n");
			rc = -IP_ERROR_NO_DISC;
			goto end;
		}
		d_print("%s", msg);
		cdio_cddap_verbose_set(drive, CDDA_MESSAGE_LOGIT, CDDA_MESSAGE_LOGIT);
		drive->b_swap_bytes = 1;

		if (cdio_cddap_open(drive)) {
			d_print("unable to open disc, aborting!\n");
			rc = -IP_ERROR_NO_DISC;
			goto end;
		}
	}

	first_lsn = cdio_cddap_track_firstsector(drive, track);
	if (first_lsn == -1) {
		d_print("no such track: %d, aborting!\n", track);
		rc = -IP_ERROR_INVALID_URI;
		goto end;
	}

	priv = xnew(struct cdda_private, 1);
	*priv = priv_init;
	priv->cdio = cdio;
	priv->drive = drive;
	priv->disc_id = xstrdup(disc_id);
	priv->device = xstrdup(device);
	priv->track = track;
	priv->first_lsn = first_lsn;
	priv->last_lsn = cdio_cddap_track_lastsector(drive, priv->track);
	priv->current_lsn = first_lsn;

	cached.cdio = priv->cdio;
	cached.drive = priv->drive;
	cached.disc_id = priv->disc_id;
	cached.device = priv->device;

	ip_data->private = priv;
	ip_data->sf = sf_bits(16) | sf_rate(44100) | sf_channels(2) | sf_signed(1);
	ip_data->sf |= sf_host_endian();

end:
	free(disc_id);

	if (rc < 0) {
		if (ip_data->fd != -1)
			close(ip_data->fd);
		ip_data->fd = -1;
	}

	if (rc == -IP_ERROR_ERRNO)
		errno = save;
	return rc;
}

static int libcdio_close(struct input_plugin_data *ip_data)
{
	struct cdda_private *priv = ip_data->private;

	if (ip_data->fd != -1)
		close(ip_data->fd);
	ip_data->fd = -1;

	if (strcmp(priv->disc_id, cached.disc_id) != 0 || strcmp(priv->device, cached.device) != 0) {
		cdio_cddap_close_no_free_cdio(priv->drive);
		cdio_destroy(priv->cdio);
		free(priv->disc_id);
		free(priv->device);
	}

	free(priv);
	ip_data->private = NULL;
	return 0;
}

static int libcdio_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	struct cdda_private *priv = ip_data->private;
	int rc = 0;

	if (priv->first_read || cdio_get_media_changed(priv->cdio)) {
		char *disc_id;
		priv->first_read = 0;
		if (!get_disc_id(priv->device, &disc_id, NULL))
			return -IP_ERROR_NO_DISC;
		if (strcmp(disc_id, priv->disc_id) != 0) {
			free(disc_id);
			return -IP_ERROR_WRONG_DISC;
		}
		free(disc_id);
	}

	if (priv->current_lsn >= priv->last_lsn)
		return 0;

	if (priv->buf_used == CDIO_CD_FRAMESIZE_RAW) {
		cdio_cddap_read(priv->drive, priv->read_buf, priv->current_lsn, 1);
		priv->current_lsn++;
		priv->buf_used = 0;
	}

	if (count >= CDIO_CD_FRAMESIZE_RAW) {
		rc = CDIO_CD_FRAMESIZE_RAW - priv->buf_used;
		memcpy(buffer, priv->read_buf + priv->buf_used, rc);
	} else {
		unsigned long buf_left = CDIO_CD_FRAMESIZE_RAW - priv->buf_used;

		if (buf_left < count) {
			memcpy(buffer, priv->read_buf + priv->buf_used, buf_left);
			rc = buf_left;
		} else {
			memcpy(buffer, priv->read_buf + priv->buf_used, count);
			rc = count;
		}
	}
	priv->buf_used += rc;

	return rc;
}

static int libcdio_seek(struct input_plugin_data *ip_data, double offset)
{
	struct cdda_private *priv = ip_data->private;
	lsn_t new_lsn;
	int64_t samples = offset * 44100;

	/* Magic number 42... really should think of a better way to do this but
	 * it seemed that the lsn is off by about 42 everytime...
	 */
	new_lsn = samples / 441.0 * CDIO_CD_FRAMES_PER_SEC / 100 + 42;

	if ((priv->first_lsn + new_lsn) > priv->last_lsn) {
		d_print("trying to seek past the end of track.\n");
		return -1;
	}

	priv->current_lsn = priv->first_lsn + new_lsn;

	return 0;
}

#ifdef HAVE_CDDB
static int parse_cddb_url(const char *url, struct http_uri *http_uri, int *use_http)
{
	char *full_url;
	int rc;

	if (is_http_url(url)) {
		*use_http = 1;
		full_url = xstrdup(url);
	} else {
		*use_http = 0;
		full_url = xstrjoin("http://", url);
	}

	rc = http_parse_uri(full_url, http_uri);
	free(full_url);
	return rc == 0;
}

static void setup_cddb_conn(cddb_conn_t *cddb_conn)
{
	struct http_uri http_uri, http_proxy_uri;
	const char *proxy;
	int use_http;

	parse_cddb_url(cddb_url, &http_uri, &use_http);

	proxy = getenv("http_proxy");
	if (proxy && http_parse_uri(proxy, &http_proxy_uri) == 0) {
		cddb_http_proxy_enable(cddb_conn);
		cddb_set_http_proxy_server_name(cddb_conn, http_proxy_uri.host);
		cddb_set_http_proxy_server_port(cddb_conn, http_proxy_uri.port);
		if (http_proxy_uri.user)
			cddb_set_http_proxy_username(cddb_conn, http_proxy_uri.user);
		if (http_proxy_uri.pass)
			cddb_set_http_proxy_password(cddb_conn, http_proxy_uri.pass);
		http_free_uri(&http_proxy_uri);
	} else
		cddb_http_proxy_disable(cddb_conn);

	if (use_http)
		cddb_http_enable(cddb_conn);
	else
		cddb_http_disable(cddb_conn);

	cddb_set_server_name(cddb_conn, http_uri.host);
	cddb_set_email_address(cddb_conn, "me@home");
	cddb_set_server_port(cddb_conn, http_uri.port);
	if (strcmp(http_uri.path, "/") != 0)
		cddb_set_http_path_query(cddb_conn, http_uri.path);
#ifdef DEBUG_CDDB
	cddb_cache_disable(cddb_conn);
#endif

	http_free_uri(&http_uri);
}
#endif


#define add_comment(c, x)	do { if (x) comments_add_const(c, #x, x); } while (0)

static int libcdio_read_comments(struct input_plugin_data *ip_data, struct keyval **comments)
{
	struct cdda_private *priv = ip_data->private;
	GROWING_KEYVALS(c);
	const char *artist = NULL, *albumartist = NULL, *album = NULL,
		*title = NULL, *genre = NULL, *comment = NULL;
	const cdtext_t *cdt;
#ifdef HAVE_CDDB
	bool track_comments_found = false;
	cddb_conn_t *cddb_conn = NULL;
	cddb_disc_t *cddb_disc = NULL;
#endif
	char buf[64];

#if LIBCDIO_VERSION_NUM >= 90
	cdt = cdio_get_cdtext(priv->cdio);
	if (cdt) {
		artist = cdtext_get(cdt, CDTEXT_FIELD_PERFORMER, priv->track);
		title = cdtext_get(cdt, CDTEXT_FIELD_TITLE, priv->track);
		genre = cdtext_get(cdt, CDTEXT_FIELD_GENRE, priv->track);
		comment = cdtext_get(cdt, CDTEXT_FIELD_MESSAGE, priv->track);

#ifdef HAVE_CDDB
		if (title)
			track_comments_found = true;
#endif

		album = cdtext_get(cdt, CDTEXT_FIELD_TITLE, 0);
		albumartist = cdtext_get(cdt, CDTEXT_FIELD_PERFORMER, 0);
		if (!artist)
			artist = albumartist;
		if (!genre)
			genre = cdtext_get(cdt, CDTEXT_FIELD_GENRE, 0);
		if (!comment)
			comment = cdtext_get(cdt, CDTEXT_FIELD_MESSAGE, 0);
	}
#else
	cdt = cdio_get_cdtext(priv->cdio, priv->track);
	if (cdt) {
		char * const *field = cdt->field;
		artist = field[CDTEXT_PERFORMER];
		title = field[CDTEXT_TITLE];
		genre = field[CDTEXT_GENRE];
		comment = field[CDTEXT_MESSAGE];
#ifdef HAVE_CDDB
		track_comments_found = true;
#endif
	}
	cdt = cdio_get_cdtext(priv->cdio, 0);
	if (cdt) {
		char * const *field = cdt->field;
		album = field[CDTEXT_TITLE];
		albumartist = field[CDTEXT_PERFORMER];
		if (!artist)
			artist = field[CDTEXT_PERFORMER];
		if (!genre)
			genre = field[CDTEXT_GENRE];
		if (!comment)
			comment = field[CDTEXT_MESSAGE];
	}
#endif

#ifdef HAVE_CDDB
	if (!track_comments_found && cddb_url && cddb_url[0]) {
		cddb_track_t *cddb_track;
		track_t i_tracks = cdio_get_num_tracks(priv->cdio);
		track_t i_first_track = cdio_get_first_track_num(priv->cdio);
		unsigned int year;
		int i;

		cddb_conn = cddb_new();
		if (!cddb_conn)
			malloc_fail();

		setup_cddb_conn(cddb_conn);

		cddb_disc = cddb_disc_new();
		if (!cddb_disc)
			malloc_fail();
		for (i = 0; i < i_tracks; i++) {
			cddb_track = cddb_track_new();
			if (!cddb_track)
				malloc_fail();
			cddb_track_set_frame_offset(cddb_track,
					cdio_get_track_lba(priv->cdio, i+i_first_track));
			cddb_disc_add_track(cddb_disc, cddb_track);
		}

		cddb_disc_set_length(cddb_disc, cdio_get_track_lba(priv->cdio,
					CDIO_CDROM_LEADOUT_TRACK) / CDIO_CD_FRAMES_PER_SEC);
		if (cddb_query(cddb_conn, cddb_disc) == 1 && cddb_read(cddb_conn, cddb_disc)) {
			albumartist = cddb_disc_get_artist(cddb_disc);
			album = cddb_disc_get_title(cddb_disc);
			genre = cddb_disc_get_genre(cddb_disc);
			year = cddb_disc_get_year(cddb_disc);
			if (year) {
				sprintf(buf, "%u", year);
				comments_add_const(&c, "date", buf);
			}
			cddb_track = cddb_disc_get_track(cddb_disc, priv->track - 1);
			artist = cddb_track_get_artist(cddb_track);
			if (!artist)
				artist = albumartist;
			title = cddb_track_get_title(cddb_track);
		}
	}
#endif

	add_comment(&c, artist);
	add_comment(&c, albumartist);
	add_comment(&c, album);
	add_comment(&c, title);
	add_comment(&c, genre);
	add_comment(&c, comment);

	sprintf(buf, "%02d", priv->track);
	comments_add_const(&c, "tracknumber", buf);

#ifdef HAVE_CDDB
	if (cddb_disc)
		cddb_disc_destroy(cddb_disc);
	if (cddb_conn)
		cddb_destroy(cddb_conn);
#endif

	keyvals_terminate(&c);
	*comments = c.keyvals;
	return 0;
}

static int libcdio_duration(struct input_plugin_data *ip_data)
{
	struct cdda_private *priv = ip_data->private;

	return (priv->last_lsn - priv->first_lsn) / CDIO_CD_FRAMES_PER_SEC;
}

static long libcdio_bitrate(struct input_plugin_data *ip_data)
{
	return 44100 * 16 * 2;
}

static char *libcdio_codec(struct input_plugin_data *ip_data)
{
	return xstrdup("cdda");
}

static char *libcdio_codec_profile(struct input_plugin_data *ip_data)
{
	struct cdda_private *priv = ip_data->private;
	discmode_t cd_discmode = cdio_get_discmode(priv->cdio);

	return xstrdup(discmode2str[cd_discmode]);
}

#ifdef HAVE_CDDB
static int libcdio_set_cddb_url(const char *val)
{
	struct http_uri http_uri;
	int use_http;
	if (!parse_cddb_url(val, &http_uri, &use_http))
		return -IP_ERROR_INVALID_URI;
	http_free_uri(&http_uri);
	free(cddb_url);
	cddb_url = xstrdup(val);
	return 0;
}

static int libcdio_get_cddb_url(char **val)
{
	if (!cddb_url)
		cddb_url = xstrdup("freedb.freedb.org:8880");
	*val = xstrdup(cddb_url);
	return 0;
}
#endif

const struct input_plugin_ops ip_ops = {
	.open = libcdio_open,
	.close = libcdio_close,
	.read = libcdio_read,
	.seek = libcdio_seek,
	.read_comments = libcdio_read_comments,
	.duration = libcdio_duration,
	.bitrate = libcdio_bitrate,
	.codec = libcdio_codec,
	.codec_profile = libcdio_codec_profile,
};

const struct input_plugin_opt ip_options[] = {
#ifdef HAVE_CDDB
	{ "cddb_url", libcdio_set_cddb_url, libcdio_get_cddb_url },
#endif
	{ NULL },
};

const int ip_priority = 50;
const char * const ip_extensions[] = { NULL };
const char * const ip_mime_types[] = { "x-content/audio-cdda", NULL };
const unsigned ip_abi_version = IP_ABI_VERSION;
