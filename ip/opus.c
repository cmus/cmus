/*
 * Copyright 2008-2014 Various Authors
 * Copyright 2012 Tuncer Ayaz
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
#include "../comment.h"

#include <opusfile.h>

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define SAMPLING_RATE 48000
#define CHANNELS 2

struct opus_private {
	OggOpusFile *of;
	int current_link;
};

static int read_func(void *datasource, unsigned char *ptr, int size)
{
	struct input_plugin_data *ip_data = datasource;
	return read_wrapper(ip_data, ptr, size);
}

static int seek_func(void *datasource, opus_int64 offset, int whence)
{
	struct input_plugin_data *ip_data = datasource;
	return lseek(ip_data->fd, offset, whence);
}

static int close_func(void *datasource)
{
	struct input_plugin_data *ip_data;
	int rc;

	ip_data = datasource;
	rc = close(ip_data->fd);
	ip_data->fd = -1;
	return rc;
}

static opus_int64 tell_func(void *datasource)
{
	struct input_plugin_data *ip_data = datasource;
	return lseek(ip_data->fd, 0, SEEK_CUR);
}

static OpusFileCallbacks callbacks = {
	.read = read_func,
	.seek = seek_func,
	.tell = tell_func,
	.close = close_func
};

static int opus_open(struct input_plugin_data *ip_data)
{
	struct opus_private *priv;
	int rc;
	void *source;

	priv = xnew(struct opus_private, 1);
	priv->current_link = -1;
	priv->of = NULL;

	source = op_fdopen(&callbacks, ip_data->fd, "r");
	if (source == NULL) {
		free(priv);
		return -IP_ERROR_INTERNAL;
	}

	priv->of = op_open_callbacks(source, &callbacks, NULL, 0, &rc);
	if (rc != 0) {
		d_print("op_open_callbacks failed: %d:%s\n", rc, strerror(rc));
		free(priv);
		/* ogg is a container format, so it is likely to contain
		 * something else if it isn't opus */
		return -IP_ERROR_UNSUPPORTED_FILE_TYPE;
	}
	ip_data->private = priv;

	ip_data->sf = sf_rate(SAMPLING_RATE)
		| sf_channels(CHANNELS)
		| sf_bits(16)
		| sf_signed(1);
	ip_data->sf |= sf_host_endian();
	return 0;
}

static int opus_close(struct input_plugin_data *ip_data)
{
	struct opus_private *priv = ip_data->private;
	/* this closes ip_data->fd! */
	op_free(priv->of);
	ip_data->fd = -1;
	free(priv);
	ip_data->private = NULL;
	return 0;
}

/*
 * -n
 *     indicates error
 * 0
 *     indicates EOF
 * n
 *     indicates actual number of bytes read
 */
static int opus_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	struct opus_private *priv;
	int samples, current_link, rc;

	priv = ip_data->private;

	/* samples = number of samples read per channel */
	samples = op_read_stereo(priv->of, (void*)buffer,
							 count / sizeof(opus_int16));
	if (samples < 0) {
		switch (samples) {
		case OP_HOLE:
			errno = EAGAIN;
			rc = -1;
			break;
		case OP_EREAD:
			errno = EINVAL;
			rc = -1;
			break;
		case OP_EFAULT:
			errno = EINVAL;
			rc = -1;
			break;
		case OP_EIMPL:
			rc = -IP_ERROR_FUNCTION_NOT_SUPPORTED;
			break;
		case OP_EINVAL:
			errno = EINVAL;
			rc = -1;
			break;
		case OP_ENOTFORMAT:
			rc = -IP_ERROR_FILE_FORMAT;
			break;
		case OP_EBADHEADER:
			rc = -IP_ERROR_FILE_FORMAT;
			break;
		case OP_EVERSION:
			rc = -IP_ERROR_FILE_FORMAT;
			break;
		case OP_EBADPACKET:
			errno = EINVAL;
			rc = -1;
			break;
		case OP_EBADLINK:
			errno = EINVAL;
			rc = -1;
			break;
		case OP_EBADTIMESTAMP:
			rc = -IP_ERROR_FILE_FORMAT;
			break;
		default:
			d_print("error: %d\n", samples);
			rc = -IP_ERROR_FILE_FORMAT;
		}
	} else if (samples == 0) {
		/* EOF or buffer too small */
		rc = 0;
	} else {
		current_link = op_current_link(priv->of);
		if (current_link < 0) {
			d_print("error: %d\n", current_link);
			rc = -1;
		} else {
			if (ip_data->remote && current_link != priv->current_link) {
				ip_data->metadata_changed = 1;
				priv->current_link = current_link;
			}

			/* bytes = samples * channels * sample_size */
			rc = samples * CHANNELS * sizeof(opus_int16);
		}
	}

	return rc;
}

static int opus_seek(struct input_plugin_data *ip_data, double offset)
{
	struct opus_private *priv;
	int rc;

	priv = ip_data->private;

	rc = op_pcm_seek(priv->of, offset * SAMPLING_RATE);
	switch (rc) {
	case OP_ENOSEEK:
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
	case OP_EINVAL:
		return -IP_ERROR_INTERNAL;
	case OP_EREAD:
		return -IP_ERROR_INTERNAL;
	case OP_EFAULT:
		return -IP_ERROR_INTERNAL;
	case OP_EBADLINK:
		return -IP_ERROR_INTERNAL;
	}
	return 0;
}

static int opus_read_comments(struct input_plugin_data *ip_data,
							  struct keyval **comments)
{
	GROWING_KEYVALS(c);
	struct opus_private *priv;
	const OpusTags *ot;
	int i;

	priv = ip_data->private;

	ot = op_tags(priv->of, -1);
	if (ot == NULL) {
		d_print("ot == NULL\n");
		*comments = keyvals_new(0);
		return 0;
	}

	for (i = 0; i < ot->comments; i++) {
		const char *str = ot->user_comments[i];
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

static int opus_duration(struct input_plugin_data *ip_data)
{
	struct opus_private *priv;
	ogg_int64_t samples;

	priv = ip_data->private;

	samples = op_pcm_total(priv->of, -1);
	if (samples < 0)
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;

	return samples / SAMPLING_RATE;
}

static long opus_bitrate(struct input_plugin_data *ip_data)
{
	struct opus_private *priv;
	opus_int32 bitrate;

	priv = ip_data->private;

	bitrate = op_bitrate(priv->of, -1);
	if (bitrate < 0)
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
	else
		return bitrate;
}

static long opus_current_bitrate(struct input_plugin_data *ip_data)
{
	struct opus_private *priv;
	opus_int32 bitrate;

	priv = ip_data->private;

	bitrate = op_bitrate_instant(priv->of);
	if (bitrate < 0)
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
	else
		return bitrate;
}

static char *opus_codec(struct input_plugin_data *ip_data)
{
	return xstrdup("opus");
}

static char *opus_codec_profile(struct input_plugin_data *ip_data)
{
	return NULL;
}

const struct input_plugin_ops ip_ops = {
	.open = opus_open,
	.close = opus_close,
	.read = opus_read,
	.seek = opus_seek,
	.read_comments = opus_read_comments,
	.duration = opus_duration,
	.bitrate = opus_bitrate,
	.bitrate_current = opus_current_bitrate,
	.codec = opus_codec,
	.codec_profile = opus_codec_profile
};

const int ip_priority = 50;
const char * const ip_extensions[] = { "opus", NULL };
const char * const ip_mime_types[] = { NULL };
const struct input_plugin_opt ip_options[] = { { NULL } };
const unsigned ip_abi_version = IP_ABI_VERSION;
