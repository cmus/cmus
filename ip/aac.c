/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2006 dnk <dnk@bjum.net>
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
#include "../debug.h"
#include "../id3.h"
#include "../comment.h"
#include "../read_wrapper.h"
#include "aac.h"

#include <neaacdec.h>

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

/* FAAD_MIN_STREAMSIZE == 768, 6 == # of channels */
#define BUFFER_SIZE	(FAAD_MIN_STREAMSIZE * 6 * 4)

struct aac_private {
	char rbuf[BUFFER_SIZE];
	int rbuf_len;
	int rbuf_pos;

	unsigned char channels;
	unsigned long sample_rate;
	long bitrate;
	int object_type;

	struct {
		unsigned long samples;
		unsigned long bytes;
	} current;

	char *overflow_buf;
	int overflow_buf_len;

	NeAACDecHandle decoder;	/* typedef void * */
};

static inline int buffer_length(const struct input_plugin_data *ip_data)
{
	struct aac_private *priv = ip_data->private;

	return priv->rbuf_len - priv->rbuf_pos;
}

static inline void *buffer_data(const struct input_plugin_data *ip_data)
{
	struct aac_private *priv = ip_data->private;

	return priv->rbuf + priv->rbuf_pos;
}

static int buffer_fill(struct input_plugin_data *ip_data)
{
	struct aac_private *priv = ip_data->private;
	int32_t n;

	if (priv->rbuf_pos > 0) {
		priv->rbuf_len = buffer_length(ip_data);
		memmove(priv->rbuf, priv->rbuf + priv->rbuf_pos, priv->rbuf_len);
		priv->rbuf_pos = 0;
	}

	if (priv->rbuf_len == BUFFER_SIZE)
		return 1;

	n = read_wrapper(ip_data, priv->rbuf + priv->rbuf_len, BUFFER_SIZE - priv->rbuf_len);
	if (n == -1)
		return -1;
	if (n == 0)
		return 0;

	priv->rbuf_len += n;
	return 1;
}

static inline void buffer_consume(struct input_plugin_data *ip_data, int n)
{
	struct aac_private *priv = ip_data->private;

	BUG_ON(n > buffer_length(ip_data));

	priv->rbuf_pos += n;
}

static int buffer_fill_min(struct input_plugin_data *ip_data, int len)
{
	int rc;

	BUG_ON(len > BUFFER_SIZE);

	while (buffer_length(ip_data) < len) {
		rc = buffer_fill(ip_data);
		if (rc <= 0)
			return rc;
	}
	return 1;
}

/* 'data' must point to at least 6 bytes of data */
static inline int parse_frame(const unsigned char data[6])
{
	int len;

	/* http://www.audiocoding.com/modules/wiki/?page=ADTS */

	/* first 12 bits must be set */
	if (data[0] != 0xFF)
		return 0;
	if ((data[1] & 0xF0) != 0xF0)
		return 0;

	/* layer is always '00' */
	if ((data[1] & 0x06) != 0x00)
		return 0;

	/* frame length is stored in 13 bits */
	len  = data[3] << 11;	/* ..1100000000000 */
	len |= data[4] << 3;	/* ..xx11111111xxx */
	len |= data[5] >> 5;	/* ..xxxxxxxxxx111 */
	len &= 0x1FFF;		/* 13 bits */
	return len;
}

/* scans forward to the next aac frame and makes sure
 * the entire frame is in the buffer.
 */
static int buffer_fill_frame(struct input_plugin_data *ip_data)
{
	unsigned char *data;
	int rc, n, len;
	int max = 32768;

	while (1) {
		/* need at least 6 bytes of data */
		rc = buffer_fill_min(ip_data, 6);
		if (rc <= 0)
			return rc;

		len = buffer_length(ip_data);
		data = buffer_data(ip_data);

		/* scan for a frame */
		for (n = 0; n < len - 5; n++) {
			/* give up after 32KB */
			if (max-- == 0) {
				d_print("no frame found!\n");
				/* FIXME: set errno? */
				return -1;
			}

			/* see if there's a frame at this location */
			rc = parse_frame(data + n);
			if (rc == 0)
				continue;

			/* found a frame, consume all data up to the frame */
			buffer_consume(ip_data, n);

			/* rc == frame length */
			rc = buffer_fill_min(ip_data, rc);
			if (rc <= 0)
				return rc;

			return 1;
		}

		/* consume what we used */
		buffer_consume(ip_data, n);
	}
	/* not reached */
}

static void aac_get_channel_map(struct input_plugin_data *ip_data)
{
	struct aac_private *priv = ip_data->private;
	NeAACDecFrameInfo frame_info;
	void *buf;
	int i;

	ip_data->channel_map[0] = CHANNEL_POSITION_INVALID;

	if (buffer_fill_frame(ip_data) <= 0)
		return;

	buf = NeAACDecDecode(priv->decoder, &frame_info, buffer_data(ip_data), buffer_length(ip_data));
	if (!buf || frame_info.error != 0 || frame_info.bytesconsumed <= 0
			|| frame_info.channels > CHANNELS_MAX)
		return;

	for (i = 0; i < frame_info.channels; i++)
		ip_data->channel_map[i] = channel_position_aac(frame_info.channel_position[i]);
}

static int aac_open(struct input_plugin_data *ip_data)
{
	struct aac_private *priv;
	NeAACDecConfigurationPtr neaac_cfg;
	int ret, n;

	/* init private struct */
	const struct aac_private priv_init = {
		.decoder = NeAACDecOpen(),
		.bitrate = -1,
		.object_type = -1
	};
	priv = xnew(struct aac_private, 1);
	*priv = priv_init;
	ip_data->private = priv;

	/* set decoder config */
	neaac_cfg = NeAACDecGetCurrentConfiguration(priv->decoder);
	neaac_cfg->outputFormat = FAAD_FMT_16BIT;	/* force 16 bit audio */
	neaac_cfg->downMatrix = 0;			/* NOT 5.1 -> stereo */
	neaac_cfg->dontUpSampleImplicitSBR = 0;		/* upsample, please! */
	NeAACDecSetConfiguration(priv->decoder, neaac_cfg);

	/* find a frame */
	if (buffer_fill_frame(ip_data) <= 0) {
		ret = -IP_ERROR_FILE_FORMAT;
		goto out;
	}

	/* in case of a bug, make sure there is at least some data
	 * in the buffer for NeAACDecInit() to work with.
	 */
	if (buffer_fill_min(ip_data, 256) <= 0) {
		d_print("not enough data\n");
		ret = -IP_ERROR_FILE_FORMAT;
		goto out;
	}

	/* init decoder, returns the length of the header (if any) */
	n = NeAACDecInit(priv->decoder, buffer_data(ip_data), buffer_length(ip_data),
		&priv->sample_rate, &priv->channels);
	if (n < 0) {
		d_print("NeAACDecInit failed\n");
		ret = -IP_ERROR_FILE_FORMAT;
		goto out;
	}

	d_print("sample rate %luhz, channels %u\n", priv->sample_rate, priv->channels);
	if (!priv->sample_rate || !priv->channels) {
		ret = -IP_ERROR_FILE_FORMAT;
		goto out;
	}

	/* skip the header */
	d_print("skipping header (%d bytes)\n", n);

	buffer_consume(ip_data, n);

	/*NeAACDecInitDRM(priv->decoder, priv->sample_rate, priv->channels);*/

	ip_data->sf = sf_rate(priv->sample_rate) | sf_channels(priv->channels) | sf_bits(16) | sf_signed(1);
	ip_data->sf |= sf_host_endian();
	aac_get_channel_map(ip_data);

	return 0;
out:
	NeAACDecClose(priv->decoder);
	free(priv);
	return ret;
}

static int aac_close(struct input_plugin_data *ip_data)
{
	struct aac_private *priv = ip_data->private;

	NeAACDecClose(priv->decoder);
	free(priv);
	ip_data->private = NULL;
	return 0;
}

/* returns -1 on fatal errors
 * returns -2 on non-fatal errors
 * 0 on eof
 * number of bytes put in 'buffer' on success */
static int decode_one_frame(struct input_plugin_data *ip_data, void *buffer, int count)
{
	struct aac_private *priv = ip_data->private;
	unsigned char *aac_data;
	unsigned int aac_data_size;
	NeAACDecFrameInfo frame_info;
	char *sample_buf;
	int bytes, rc;

	rc = buffer_fill_frame(ip_data);
	if (rc <= 0)
		return rc;

	aac_data = buffer_data(ip_data);
	aac_data_size = buffer_length(ip_data);

	/* aac data -> raw pcm */
	sample_buf = NeAACDecDecode(priv->decoder, &frame_info, aac_data, aac_data_size);
	if (frame_info.error == 0 && frame_info.samples > 0) {
		priv->current.samples += frame_info.samples;
		priv->current.bytes += frame_info.bytesconsumed;
	}

	buffer_consume(ip_data, frame_info.bytesconsumed);

	if (!sample_buf || frame_info.bytesconsumed <= 0) {
		d_print("fatal error: %s\n", NeAACDecGetErrorMessage(frame_info.error));
		errno = EINVAL;
		return -1;
	}

	if (frame_info.error != 0) {
		d_print("frame error: %s\n", NeAACDecGetErrorMessage(frame_info.error));
		return -2;
	}

	if (frame_info.samples <= 0)
		return -2;

	if (frame_info.channels != priv->channels || frame_info.samplerate != priv->sample_rate) {
		d_print("invalid channel or sample_rate count\n");
		return -2;
	}

	/* 16-bit samples */
	bytes = frame_info.samples * 2;

	if (bytes > count) {
		/* decoded too much, keep overflow */
		priv->overflow_buf = sample_buf + count;
		priv->overflow_buf_len = bytes - count;
		memcpy(buffer, sample_buf, count);
		return count;
	} else {
		memcpy(buffer, sample_buf, bytes);
	}
	return bytes;
}

static int aac_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	struct aac_private *priv = ip_data->private;
	int rc;

	/* use overflow from previous call (if any) */
	if (priv->overflow_buf_len) {
		int len = priv->overflow_buf_len;

		if (len > count)
			len = count;

		memcpy(buffer, priv->overflow_buf, len);
		priv->overflow_buf += len;
		priv->overflow_buf_len -= len;
		return len;
	}

	do {
		rc = decode_one_frame(ip_data, buffer, count);
	} while (rc == -2);
	return rc;
}

static int aac_seek(struct input_plugin_data *ip_data, double offset)
{
	return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
}

static int aac_read_comments(struct input_plugin_data *ip_data,
		struct keyval **comments)
{
	GROWING_KEYVALS(c);
	struct id3tag id3;
	int rc, fd, i;

	fd = open(ip_data->filename, O_RDONLY);
	if (fd == -1)
		return -1;

	id3_init(&id3);
	rc = id3_read_tags(&id3, fd, ID3_V1 | ID3_V2);
	if (rc == -1) {
		d_print("error: %s\n", strerror(errno));
		goto out;
	}

	for (i = 0; i < NUM_ID3_KEYS; i++) {
		char *val = id3_get_comment(&id3, i);

		if (val)
			comments_add(&c, id3_key_names[i], val);
	}
out:
	close(fd);
	id3_free(&id3);
	keyvals_terminate(&c);
	*comments = c.keyvals;
	return 0;
}

static int aac_duration(struct input_plugin_data *ip_data)
{
	struct aac_private *priv = ip_data->private;
	NeAACDecFrameInfo frame_info;
	int samples = 0, bytes = 0, frames = 0;
	off_t file_size;

	file_size = lseek(ip_data->fd, 0, SEEK_END);
	if (file_size == -1)
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;

	/* Seek to the middle of the file. There is almost always silence at
	 * the beginning, which gives wrong results. */
	if (lseek(ip_data->fd, file_size/2, SEEK_SET) == -1)
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;

	priv->rbuf_pos = 0;
	priv->rbuf_len = 0;

	/* guess track length by decoding the first 10 frames */
	while (frames < 10) {
		if (buffer_fill_frame(ip_data) <= 0)
			break;

		NeAACDecDecode(priv->decoder, &frame_info,
			buffer_data(ip_data), buffer_length(ip_data));
		if (frame_info.error == 0 && frame_info.samples > 0) {
			samples += frame_info.samples;
			bytes += frame_info.bytesconsumed;
			frames++;
		}
		if (frame_info.bytesconsumed == 0)
			break;

		buffer_consume(ip_data, frame_info.bytesconsumed);
	}

	if (frames == 0)
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;

	samples /= frames;
	samples /= priv->channels;
	bytes /= frames;

	/*  8 * file_size / duration */
	priv->bitrate = (8 * bytes * priv->sample_rate) / samples;

	priv->object_type = frame_info.object_type;

	return ((file_size / bytes) * samples) / priv->sample_rate;
}

static long aac_bitrate(struct input_plugin_data *ip_data)
{
	struct aac_private *priv = ip_data->private;
	return priv->bitrate != -1 ? priv->bitrate : -IP_ERROR_FUNCTION_NOT_SUPPORTED;
}

static long aac_current_bitrate(struct input_plugin_data *ip_data)
{
	struct aac_private *priv = ip_data->private;
	long bitrate = -1;
	if (priv->current.samples > 0) {
		priv->current.samples /= priv->channels;
		bitrate = (8 * priv->current.bytes * priv->sample_rate) / priv->current.samples;
		priv->current.samples = 0;
		priv->current.bytes = 0;
	}
	return bitrate;
}

static char *aac_codec(struct input_plugin_data *ip_data)
{
	return xstrdup("aac");
}

static const char *object_type_to_str(int object_type)
{
	switch (object_type) {
	case MAIN:	return "Main";
	case LC:	return "LC";
	case SSR:	return "SSR";
	case LTP:	return "LTP";
	case HE_AAC:	return "HE";
	case ER_LC:	return "ER-LD";
	case ER_LTP:	return "ER-LTP";
	case LD:	return "LD";
	case DRM_ER_LC:	return "DRM-ER-LC";
	}
	return NULL;
}

static char *aac_codec_profile(struct input_plugin_data *ip_data)
{
	struct aac_private *priv = ip_data->private;
	const char *profile = object_type_to_str(priv->object_type);

	return profile ? xstrdup(profile) : NULL;
}

const struct input_plugin_ops ip_ops = {
	.open = aac_open,
	.close = aac_close,
	.read = aac_read,
	.seek = aac_seek,
	.read_comments = aac_read_comments,
	.duration = aac_duration,
	.bitrate = aac_bitrate,
	.bitrate_current = aac_current_bitrate,
	.codec = aac_codec,
	.codec_profile = aac_codec_profile
};

const int ip_priority = 50;
const char * const ip_extensions[] = { "aac", NULL };
const char * const ip_mime_types[] = { "audio/aac", "audio/aacp", NULL };
const struct input_plugin_opt ip_options[] = { { NULL } };
const unsigned ip_abi_version = IP_ABI_VERSION;
