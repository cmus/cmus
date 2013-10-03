/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2004 Timo Hirvonen
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

#include "ip.h"
#include "nomad.h"
#include "id3.h"
#include "ape.h"
#include "xmalloc.h"
#include "read_wrapper.h"
#include "debug.h"
#include "utils.h"
#include "comment.h"

#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* ------------------------------------------------------------------------- */

static ssize_t read_func(void *datasource, void *buffer, size_t count)
{
	struct input_plugin_data *ip_data = datasource;

	return read_wrapper(ip_data, buffer, count);
}

static off_t lseek_func(void *datasource, off_t offset, int whence)
{
	struct input_plugin_data *ip_data = datasource;

	return lseek(ip_data->fd, offset, whence);
}

static int close_func(void *datasource)
{
	struct input_plugin_data *ip_data = datasource;

	return close(ip_data->fd);
}

static struct nomad_callbacks callbacks = {
	.read = read_func,
	.lseek = lseek_func,
	.close = close_func
};

/* ------------------------------------------------------------------------- */

static int mad_open(struct input_plugin_data *ip_data)
{
	struct nomad *nomad;
	const struct nomad_info *info;
	int rc;

	rc = nomad_open_callbacks(&nomad, ip_data, &callbacks);
	switch (rc) {
	case -NOMAD_ERROR_ERRNO:
		return -1;
	case -NOMAD_ERROR_FILE_FORMAT:
		return -IP_ERROR_FILE_FORMAT;
	}
	ip_data->private = nomad;

	info = nomad_info(nomad);

	/* always 16-bit signed little-endian */
	ip_data->sf = sf_rate(info->sample_rate) | sf_channels(info->channels) |
		sf_bits(16) | sf_signed(1);
	channel_map_init_waveex(info->channels, 0, ip_data->channel_map);
	return 0;
}

static int mad_close(struct input_plugin_data *ip_data)
{
	struct nomad *nomad;

	nomad = ip_data->private;
	nomad_close(nomad);
	ip_data->fd = -1;
	ip_data->private = NULL;
	return 0;
}

static int mad_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	struct nomad *nomad;

	nomad = ip_data->private;
	return nomad_read(nomad, buffer, count);
}

static int mad_seek(struct input_plugin_data *ip_data, double offset)
{
	struct nomad *nomad;

	nomad = ip_data->private;
	return nomad_time_seek(nomad, offset);
}

static int mad_read_comments(struct input_plugin_data *ip_data,
		struct keyval **comments)
{
	struct nomad *nomad = ip_data->private;
	const struct nomad_lame *lame = nomad_lame(nomad);
	struct id3tag id3;
	int fd, rc, save, i;
	APETAG(ape);
	GROWING_KEYVALS(c);

	fd = open(ip_data->filename, O_RDONLY);
	if (fd == -1) {
		return -1;
	}
	d_print("filename: %s\n", ip_data->filename);

	id3_init(&id3);
	rc = id3_read_tags(&id3, fd, ID3_V1 | ID3_V2);
	save = errno;
	close(fd);
	errno = save;
	if (rc) {
		if (rc == -1) {
			d_print("error: %s\n", strerror(errno));
			return -1;
		}
		d_print("corrupted tag?\n");
		goto next;
	}

	for (i = 0; i < NUM_ID3_KEYS; i++) {
		char *val = id3_get_comment(&id3, i);

		if (val)
			comments_add(&c, id3_key_names[i], val);
	}

next:
	id3_free(&id3);

	rc = ape_read_tags(&ape, ip_data->fd, 0);
	if (rc < 0)
		goto out;

	for (i = 0; i < rc; i++) {
		char *k, *v;
		k = ape_get_comment(&ape, &v);
		if (!k)
			break;
		comments_add(&c, k, v);
		free(k);
	}

out:
	ape_free(&ape);

	/* add last so the other tags get preference */
	if (lame && !isnan(lame->trackGain)) {
		char buf[64];

		if (!isnan(lame->peak)) {
			sprintf(buf, "%f", lame->peak);
			comments_add_const(&c, "replaygain_track_peak", buf);
		}
		sprintf(buf, "%+.1f dB", lame->trackGain);
		comments_add_const(&c, "replaygain_track_gain", buf);
	}

	keyvals_terminate(&c);
	*comments = c.keyvals;
	return 0;
}

static int mad_duration(struct input_plugin_data *ip_data)
{
	struct nomad *nomad = ip_data->private;

	return nomad_info(nomad)->duration;
}

static long mad_bitrate(struct input_plugin_data *ip_data)
{
	struct nomad *nomad = ip_data->private;
	long bitrate = nomad_info(nomad)->avg_bitrate;

	return bitrate != -1 ? bitrate : -IP_ERROR_FUNCTION_NOT_SUPPORTED;
}

static long mad_current_bitrate(struct input_plugin_data *ip_data)
{
	struct nomad *nomad = ip_data->private;
	return nomad_current_bitrate(nomad);
}

static char *mad_codec(struct input_plugin_data *ip_data)
{
	struct nomad *nomad = ip_data->private;

	switch (nomad_info(nomad)->layer) {
	case 3:
		return xstrdup("mp3");
	case 2:
		return xstrdup("mp2");
	case 1:
		return xstrdup("mp1");
	}
	return NULL;
}

static char *mad_codec_profile(struct input_plugin_data *ip_data)
{
	struct nomad *nomad = ip_data->private;
	const struct nomad_lame *lame = nomad_lame(nomad);
	const char *mode = nomad_info(nomad)->vbr ? "VBR" : "CBR";

	if (lame) {
		/* LAME:
		 * 0: unknown
		 * 1: cbr
		 * 2: abr
		 * 3: vbr rh (--vbr-old)
		 * 4: vbr mtrh (--vbr-new)
		 * 5: vbr mt (obsolete)
		 */
		int method = lame->vbr_method;
		if (method == 2)
			mode = "ABR";
		else if (method >= 3 && method <= 5) {
			const struct nomad_xing *xing = nomad_xing(nomad);

			if (xing && xing->flags & XING_SCALE && xing->scale && xing->scale <= 100) {
				char buf[16];
				int v = 10 - (xing->scale + 9) / 10;
				/* quality (-q): 10 - (xing->scale - ((9 - v) * 10)) */

				sprintf(buf, "VBR V%d", v);
				return xstrdup(buf);

			}
		}
	}

	return xstrdup(mode);
}

const struct input_plugin_ops ip_ops = {
	.open = mad_open,
	.close = mad_close,
	.read = mad_read,
	.seek = mad_seek,
	.read_comments = mad_read_comments,
	.duration = mad_duration,
	.bitrate = mad_bitrate,
	.bitrate_current = mad_current_bitrate,
	.codec = mad_codec,
	.codec_profile = mad_codec_profile
};

const int ip_priority = 55;
const char * const ip_extensions[] = { "mp3", "mp2", NULL };
const char * const ip_mime_types[] = {
	"audio/mpeg", "audio/x-mp3", "audio/x-mpeg", NULL
};
const char * const ip_options[] = { NULL };
