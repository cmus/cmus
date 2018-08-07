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

#include "../ip.h"
#include "../xmalloc.h"
#include "../read_wrapper.h"
#include "../debug.h"
#ifdef HAVE_CONFIG
#include "../config/tremor.h"
#endif
#include "../comment.h"

#ifdef CONFIG_TREMOR
#include <tremor/ivorbisfile.h>
#else
#include <vorbis/vorbisfile.h>
#endif

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <math.h>

struct vorbis_private {
	OggVorbis_File vf;
	int current_section;
};

/* http://www.xiph.org/vorbis/doc/vorbisfile/callbacks.html */

static size_t read_func(void *ptr, size_t size, size_t nmemb, void *datasource)
{
	struct input_plugin_data *ip_data = datasource;
	int rc;

	rc = read_wrapper(ip_data, ptr, size * nmemb);
	if (rc == -1) {
		d_print("error: %s\n", strerror(errno));
		return 0;
	}
	if (rc == 0) {
		errno = 0;
		return 0;
	}
	return rc / size;
}

static int seek_func(void *datasource, ogg_int64_t offset, int whence)
{
	struct input_plugin_data *ip_data = datasource;

	if (lseek(ip_data->fd, offset, whence) == -1)
		return -1;
	return 0;
}

static int close_func(void *datasource)
{
	struct input_plugin_data *ip_data = datasource;
	int rc;

	rc = close(ip_data->fd);
	ip_data->fd = -1;
	return rc;
}

static long tell_func(void *datasource)
{
	struct input_plugin_data *ip_data = datasource;
	off_t off;

	off = lseek(ip_data->fd, 0, SEEK_CUR);
	return (off == -1) ? -1 : off;
}

/*
 * typedef struct {
 *   size_t (*read_func)  (void *ptr, size_t size, size_t nmemb, void *datasource);
 *   int    (*seek_func)  (void *datasource, ogg_int64_t offset, int whence);
 *   int    (*close_func) (void *datasource);
 *   long   (*tell_func)  (void *datasource);
 * } ov_callbacks;
 */
static ov_callbacks callbacks = {
	.read_func = read_func,
	.seek_func = seek_func,
	.close_func = close_func,
	.tell_func = tell_func
};

/* http://xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-800004.3.9 */
static void channel_map_init_vorbis(int channels, channel_position_t *map)
{
	switch (channels) {
	case 8:
		channel_map_init_vorbis(7, map);
		map[5] = CHANNEL_POSITION_REAR_LEFT;
		map[6] = CHANNEL_POSITION_REAR_RIGHT;
		map[7] = CHANNEL_POSITION_LFE;
		break;
	case 7:
		channel_map_init_vorbis(3, map);
		map[3] = CHANNEL_POSITION_SIDE_LEFT;
		map[4] = CHANNEL_POSITION_SIDE_RIGHT;
		map[5] = CHANNEL_POSITION_REAR_CENTER;
		map[6] = CHANNEL_POSITION_LFE;
		break;
	case 6:
		map[5] = CHANNEL_POSITION_LFE;
		/* Fall through */
	case 5:
		map[3] = CHANNEL_POSITION_REAR_LEFT;
		map[4] = CHANNEL_POSITION_REAR_RIGHT;
		/* Fall through */
	case 3:
		map[0] = CHANNEL_POSITION_FRONT_LEFT;
		map[1] = CHANNEL_POSITION_CENTER;
		map[2] = CHANNEL_POSITION_FRONT_RIGHT;
		break;
	case 4:
		map[2] = CHANNEL_POSITION_REAR_LEFT;
		map[3] = CHANNEL_POSITION_REAR_RIGHT;
		/* Fall through */
	case 2:
		map[0] = CHANNEL_POSITION_FRONT_LEFT;
		map[1] = CHANNEL_POSITION_FRONT_RIGHT;
		break;
	case 1:
		map[0] = CHANNEL_POSITION_MONO;
		break;
	default:
		map[0] = CHANNEL_POSITION_INVALID;
		break;
	}
}

static int vorbis_open(struct input_plugin_data *ip_data)
{
	struct vorbis_private *priv;
	vorbis_info *vi;
	int rc;

	priv = xnew(struct vorbis_private, 1);
	priv->current_section = -1;
	memset(&priv->vf, 0, sizeof(priv->vf));

	rc = ov_open_callbacks(ip_data, &priv->vf, NULL, 0, callbacks);
	if (rc != 0) {
		d_print("ov_open failed: %d\n", rc);
		free(priv);
		/* ogg is a container format, so it is likely to contain
		 * something else if it isn't vorbis */
		return -IP_ERROR_UNSUPPORTED_FILE_TYPE;
	}
	ip_data->private = priv;

	vi = ov_info(&priv->vf, -1);
	ip_data->sf = sf_rate(vi->rate) | sf_channels(vi->channels) | sf_bits(16) | sf_signed(1);
	ip_data->sf |= sf_host_endian();
	channel_map_init_vorbis(vi->channels, ip_data->channel_map);
	return 0;
}

static int vorbis_close(struct input_plugin_data *ip_data)
{
	struct vorbis_private *priv;
	int rc;

	priv = ip_data->private;
	/* this closes ip_data->fd! */
	rc = ov_clear(&priv->vf);
	ip_data->fd = -1;
	if (rc)
		d_print("ov_clear returned %d\n", rc);
	free(priv);
	ip_data->private = NULL;
	return 0;
}

static inline int vorbis_endian(void)
{
#ifdef WORDS_BIGENDIAN
	return 1;
#else
	return 0;
#endif
}

/*
 * OV_HOLE
 *     indicates there was an interruption in the data.
 *     (one of: garbage between pages, loss of sync followed by recapture,
 *     or a corrupt page)
 * OV_EBADLINK
 *     indicates that an invalid stream section was supplied to libvorbisfile,
 *     or the requested link is corrupt.
 * 0
 *     indicates EOF
 * n
 *     indicates actual number of bytes read. ov_read() will decode at most
 *     one vorbis packet per invocation, so the value returned will generally
 *     be less than length.
 */
static int vorbis_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	struct vorbis_private *priv;
	int rc;
	int current_section;

	priv = ip_data->private;
#ifdef CONFIG_TREMOR
	/* Tremor can only handle signed 16 bit data */
	rc = ov_read(&priv->vf, buffer, count, &current_section);
#else
	rc = ov_read(&priv->vf, buffer, count, vorbis_endian(), 2, 1, &current_section);
#endif

	if (ip_data->remote && current_section != priv->current_section) {
		ip_data->metadata_changed = 1;
		priv->current_section = current_section;
	}

	switch (rc) {
	case OV_HOLE:
		errno = EAGAIN;
		return -1;
	case OV_EBADLINK:
		errno = EINVAL;
		return -1;
	case OV_EINVAL:
		errno = EINVAL;
		return -1;
	case 0:
		if (errno) {
			d_print("error: %s\n", strerror(errno));
			return -1;
/* 			return -IP_ERROR_INTERNAL; */
		}
		/* EOF */
		return 0;
	default:
		if (rc < 0) {
			d_print("error: %d\n", rc);
			rc = -IP_ERROR_FILE_FORMAT;
		}
		return rc;
	}
}

static int vorbis_seek(struct input_plugin_data *ip_data, double offset)
{
	struct vorbis_private *priv;
	int rc;

	priv = ip_data->private;

#ifdef CONFIG_TREMOR
	rc = ov_time_seek(&priv->vf, offset * 1000);
#else
	rc = ov_time_seek(&priv->vf, offset);
#endif
	switch (rc) {
	case OV_ENOSEEK:
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
	case OV_EINVAL:
		return -IP_ERROR_INTERNAL;
	case OV_EREAD:
		return -IP_ERROR_INTERNAL;
	case OV_EFAULT:
		return -IP_ERROR_INTERNAL;
	case OV_EBADLINK:
		return -IP_ERROR_INTERNAL;
	}
	return 0;
}

static int vorbis_read_comments(struct input_plugin_data *ip_data,
		struct keyval **comments)
{
	GROWING_KEYVALS(c);
	struct vorbis_private *priv;
	vorbis_comment *vc;
	int i;

	priv = ip_data->private;
	vc = ov_comment(&priv->vf, -1);
	if (vc == NULL) {
		d_print("vc == NULL\n");
		*comments = keyvals_new(0);
		return 0;
	}
	for (i = 0; i < vc->comments; i++) {
		const char *str = vc->user_comments[i];
		const char *eq = strchr(str, '=');
		char *key;

		if (!eq) {
			d_print("invalid comment: '%s' ('=' expected)\n", str);
			continue;
		}

		key = xstrndup(str, eq - str);
		comments_add_const(&c, key, eq + 1);
		free(key);
	}
	keyvals_terminate(&c);
	*comments = c.keyvals;
	return 0;
}

static int vorbis_duration(struct input_plugin_data *ip_data)
{
	struct vorbis_private *priv;
	int duration;

	priv = ip_data->private;
	duration = ov_time_total(&priv->vf, -1);
	if (duration == OV_EINVAL)
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
#ifdef CONFIG_TREMOR
	duration = (duration + 500) / 1000;
#endif
	return duration;
}

static long vorbis_bitrate(struct input_plugin_data *ip_data)
{
	struct vorbis_private *priv = ip_data->private;
	long bitrate = ov_bitrate(&priv->vf, -1);
	if (bitrate == OV_EINVAL || bitrate == OV_FALSE)
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
	return bitrate;
}

static long vorbis_current_bitrate(struct input_plugin_data *ip_data)
{
	struct vorbis_private *priv = ip_data->private;
	return ov_bitrate_instant(&priv->vf);
}

static char *vorbis_codec(struct input_plugin_data *ip_data)
{
	return xstrdup("vorbis");
}

static const long rate_mapping_44[2][12] = {
	{ 32000, 48000, 60000, 70000,  80000,  86000,  96000, 110000, 120000, 140000, 160000, 239920 },
	{ 45000, 64000, 80000, 96000, 112000, 128000, 160000, 192000, 224000, 256000, 320000, 499821 }
};

static char *vorbis_codec_profile(struct input_plugin_data *ip_data)
{
	struct vorbis_private *priv = ip_data->private;
	vorbis_info *vi = ov_info(&priv->vf, -1);
	long b = vi->bitrate_nominal;
	char buf[64];

	if (b <= 0)
		return NULL;

	if (vi->channels > 2 || vi->rate < 44100) {
		sprintf(buf, "%ldkbps", b / 1000);
	} else {
		const long *map = rate_mapping_44[vi->channels - 1];
		float q;
		int i;

		for (i = 0; i < 12-1; i++) {
			if (b >= map[i] && b < map[i+1])
				break;
		}
		/* This is used even if upper / lower bitrate are set
		 * because it gives a good approximation. */
		q = (i - 1) + (float) (b - map[i]) / (map[i+1] - map[i]);
		sprintf(buf, "q%g", roundf(q * 100.f) / 100.f);
	}

	return xstrdup(buf);
}

const struct input_plugin_ops ip_ops = {
	.open = vorbis_open,
	.close = vorbis_close,
	.read = vorbis_read,
	.seek = vorbis_seek,
	.read_comments = vorbis_read_comments,
	.duration = vorbis_duration,
	.bitrate = vorbis_bitrate,
	.bitrate_current = vorbis_current_bitrate,
	.codec = vorbis_codec,
	.codec_profile = vorbis_codec_profile
};

const int ip_priority = 50;
const char * const ip_extensions[] = { "ogg", "oga", "ogx", NULL };
const char * const ip_mime_types[] = { "application/ogg", "audio/x-ogg", NULL };
const struct input_plugin_opt ip_options[] = { { NULL } };
const unsigned ip_abi_version = IP_ABI_VERSION;
