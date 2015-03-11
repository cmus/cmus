/*
 * Copyright (C) 2008-2013 Various Authors
 * Copyright (C) 2011 Gregory Petrosyan
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

#include "ip.h"
#include "debug.h"
#include "input.h"
#include "utils.h"
#include "comment.h"
#include "xmalloc.h"
#include "cue_utils.h"

#include <stdio.h>
#include <fcntl.h>


struct cue_private {
	struct input_plugin *child;

	char *cue_filename;
	int track_n;

	double start_offset;
	double current_offset;
	double end_offset;
};


static int _parse_cue_url(const char *url, char **filename, int *track_n)
{
	const char *slash;
	long n;

	if (!is_cue_url(url))
		return 1;

	url += 6;

	slash = strrchr(url, '/');
	if (!slash)
		return 1;

	if (str_to_int(slash + 1, &n) != 0)
		return 1;

	*filename = xstrndup(url, slash - url);
	*track_n = n;
	return 0;
}


static double _to_seconds(long v)
{
	const int FRAMES_IN_SECOND = 75;

	return (double)v / FRAMES_IN_SECOND;
}


static char *_make_absolute_path(const char *abs_filename, const char *rel_filename)
{
	char *s;
	const char *slash;
	char buf[4096] = {0};

	slash = strrchr(abs_filename, '/');
	if (slash == NULL)
		return xstrdup(rel_filename);

	s = xstrndup(abs_filename, slash - abs_filename);
	snprintf(buf, sizeof buf, "%s/%s", s, rel_filename);

	free(s);
	return xstrdup(buf);
}


static int cue_open(struct input_plugin_data *ip_data)
{
	int rc;
	FILE *cue;
	Cd *cd;
	Track *t;
	char *child_filename;
	struct cue_private *priv;

	priv = xnew(struct cue_private, 1);

	rc = _parse_cue_url(ip_data->filename, &priv->cue_filename, &priv->track_n);
	if (rc) {
		rc = -IP_ERROR_INVALID_URI;
		goto url_parse_failed;
	}

	cue = fopen(priv->cue_filename, "r");
	if (cue == NULL) {
		rc = -IP_ERROR_ERRNO;
		goto cue_open_failed;
	}

	disable_stdio();
	cd = cue_parse_file(cue);
	enable_stdio();
	if (cd == NULL) {
		rc = -IP_ERROR_FILE_FORMAT;
		goto cue_parse_failed;
	}

	t = cd_get_track(cd, priv->track_n);
	if (t == NULL) {
		rc = -IP_ERROR_FILE_FORMAT;
		goto cue_read_failed;
	}

	child_filename = track_get_filename(t);
	if (child_filename == NULL) {
		rc = -IP_ERROR_FILE_FORMAT;
		goto cue_read_failed;
	}
	child_filename = _make_absolute_path(priv->cue_filename, child_filename);

	priv->child = ip_new(child_filename);
	free(child_filename);

	rc = ip_open(priv->child);
	if (rc)
		goto ip_open_failed;

	ip_setup(priv->child);

	priv->start_offset = _to_seconds(track_get_start(t));
	priv->current_offset = priv->start_offset;

	rc = ip_seek(priv->child, priv->start_offset);
	if (rc)
		goto ip_open_failed;

	if (track_get_length(t) != 0)
		priv->end_offset = priv->start_offset + _to_seconds(track_get_length(t));
	else
		priv->end_offset = ip_duration(priv->child);

	ip_data->fd = open(ip_get_filename(priv->child), O_RDONLY);
	if (ip_data->fd == -1)
		goto ip_open_failed;

	ip_data->private = priv;
	ip_data->sf = ip_get_sf(priv->child);
	ip_get_channel_map(priv->child, ip_data->channel_map);

	fclose(cue);
	cd_delete(cd);
	return 0;

ip_open_failed:
	ip_delete(priv->child);

cue_read_failed:
	cd_delete(cd);

cue_parse_failed:
	fclose(cue);

cue_open_failed:
	free(priv->cue_filename);

url_parse_failed:
	free(priv);

	return rc;
}


static int cue_close(struct input_plugin_data *ip_data)
{
	struct cue_private *priv = ip_data->private;

	close(ip_data->fd);
	ip_data->fd = -1;

	ip_delete(priv->child);
	free(priv->cue_filename);

	free(priv);
	ip_data->private = NULL;

	return 0;
}


static int cue_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	int rc;
	sample_format_t sf;
	double len;
	double rem_len;
	struct cue_private *priv = ip_data->private;

	if (priv->current_offset >= priv->end_offset)
		return 0;

	rc = ip_read(priv->child, buffer, count);
	if (rc <= 0)
		return rc;

	sf = ip_get_sf(priv->child);
	len = (double)rc / sf_get_second_size(sf);

	rem_len = priv->end_offset - priv->current_offset;
	priv->current_offset += len;

	if (priv->current_offset >= priv->end_offset)
		rc = (int)(rem_len * sf_get_rate(sf)) * sf_get_frame_size(sf);

	return rc;
}


static int cue_seek(struct input_plugin_data *ip_data, double offset)
{
	struct cue_private *priv = ip_data->private;
	double new_offset = priv->start_offset + offset;

	if (new_offset > priv->end_offset)
		new_offset = priv->end_offset;

	priv->current_offset = new_offset;

	return ip_seek(priv->child, new_offset);
}


static int cue_read_comments(struct input_plugin_data *ip_data, struct keyval **comments)
{
	int rc;
	FILE *cue;
	Cd *cd;
	Rem *cd_rem;
	Cdtext *cd_cdtext;
	Track *t;
	Rem *track_rem;
	Cdtext *track_cdtext;
	char *val;
	char buf[32] = {0};
	GROWING_KEYVALS(c);
	struct cue_private *priv = ip_data->private;

	cue = fopen(priv->cue_filename, "r");
	if (cue == NULL) {
		rc = -IP_ERROR_ERRNO;
		goto cue_open_failed;
	}

	disable_stdio();
	cd = cue_parse_file(cue);
	enable_stdio();
	if (cd == NULL) {
		rc = -IP_ERROR_FILE_FORMAT;
		goto cue_parse_failed;
	}

	t = cd_get_track(cd, priv->track_n);
	if (t == NULL) {
		rc = -IP_ERROR_FILE_FORMAT;
		goto get_track_failed;
	}

	snprintf(buf, sizeof buf, "%d", priv->track_n);
	comments_add_const(&c, "tracknumber", buf);

	cd_rem = cd_get_rem(cd);
	cd_cdtext = cd_get_cdtext(cd);
	track_rem = track_get_rem(t);
	track_cdtext = track_get_cdtext(t);

	val = cdtext_get(PTI_TITLE, track_cdtext);
	if (val != NULL)
		comments_add_const(&c, "title", val);

	val = cdtext_get(PTI_TITLE, cd_cdtext);
	if (val != NULL)
		comments_add_const(&c, "album", val);

	val = cdtext_get(PTI_PERFORMER, track_cdtext);
	if (val != NULL)
		comments_add_const(&c, "artist", val);

	val = cdtext_get(PTI_PERFORMER, cd_cdtext);
	if (val != NULL)
		comments_add_const(&c, "albumartist", val);

	val = rem_get(REM_DATE, track_rem);
	if (val != NULL) {
		comments_add_const(&c, "date", val);
	} else {
		val = rem_get(REM_DATE, cd_rem);
		if (val != NULL)
			comments_add_const(&c, "date", val);
	}

	/*
	 * TODO:
	 * - replaygain REMs
	 * - genre?
	 */

	keyvals_terminate(&c);
	*comments = c.keyvals;

	cd_delete(cd);
	fclose(cue);
	return 0;

get_track_failed:
	cd_delete(cd);

cue_parse_failed:
	fclose(cue);

cue_open_failed:
	return rc;
}


static int cue_duration(struct input_plugin_data *ip_data)
{
	struct cue_private *priv = ip_data->private;

	return priv->end_offset - priv->start_offset;
}


static long cue_bitrate(struct input_plugin_data *ip_data)
{
	struct cue_private *priv = ip_data->private;

	return ip_bitrate(priv->child);
}


static long cue_current_bitrate(struct input_plugin_data *ip_data)
{
	struct cue_private *priv = ip_data->private;

	return ip_current_bitrate(priv->child);
}


static char *cue_codec(struct input_plugin_data *ip_data)
{
	struct cue_private *priv = ip_data->private;

	return ip_codec(priv->child);
}


static char *cue_codec_profile(struct input_plugin_data *ip_data)
{
	struct cue_private *priv = ip_data->private;

	return ip_codec_profile(priv->child);
}


static int cue_set_option(int key, const char *val)
{
	return -IP_ERROR_NOT_OPTION;
}


static int cue_get_option(int key, char **val)
{
	return -IP_ERROR_NOT_OPTION;
}


const struct input_plugin_ops ip_ops = {
	.open            = cue_open,
	.close           = cue_close,
	.read            = cue_read,
	.seek            = cue_seek,
	.read_comments   = cue_read_comments,
	.duration        = cue_duration,
	.bitrate         = cue_bitrate,
	.bitrate_current = cue_current_bitrate,
	.codec           = cue_codec,
	.codec_profile   = cue_codec_profile,
	.set_option      = cue_set_option,
	.get_option      = cue_get_option
};

const int ip_priority = 50;
const char * const ip_extensions[] = { NULL };
const char * const ip_mime_types[] = { "application/x-cue", NULL };
const char * const ip_options[] = { NULL };
