/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2007 Johannes Weißl
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
#include "../ape.h"
#include "../id3.h"
#include "../xmalloc.h"
#include "../read_wrapper.h"
#include "../debug.h"
#include "../buffer.h"
#include "../comment.h"

#include <wavpack/wavpack.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#define WV_CHANNEL_MAX 2

struct wavpack_file {
	int fd;
	off_t len;
	int push_back_byte;
};

struct wavpack_private {
	WavpackContext *wpc;
	int32_t samples[CHUNK_SIZE * WV_CHANNEL_MAX];
	struct wavpack_file wv_file;
	struct wavpack_file wvc_file;
	unsigned int has_wvc : 1;
};

/* http://www.wavpack.com/lib_use.txt */

static int32_t read_bytes(void *data, void *ptr, int32_t count)
{
	struct wavpack_file *file = data;
	int32_t rc, n = 0;

	if (file->push_back_byte != EOF) {
		char *p = ptr;
		*p = (char) file->push_back_byte;
		ptr = p + 1;
		file->push_back_byte = EOF;
		count--;
		n++;
	}

	rc = read(file->fd, ptr, count);
	if (rc == -1) {
		d_print("error: %s\n", strerror(errno));
		return 0;
	}
	if (rc == 0) {
		errno = 0;
		return 0;
	}
	return rc + n;
}

static uint32_t get_pos(void *data)
{
	struct wavpack_file *file = data;

	return lseek(file->fd, 0, SEEK_CUR);
}

static int set_pos_rel(void *data, int32_t delta, int mode)
{
	struct wavpack_file *file = data;

	if (lseek(file->fd, delta, mode) == -1)
		return -1;

	file->push_back_byte = EOF;
	return 0;
}

static int set_pos_abs(void *data, uint32_t pos)
{
	return set_pos_rel(data, pos, SEEK_SET);
}

static int push_back_byte(void *data, int c)
{
	struct wavpack_file *file = data;

	if (file->push_back_byte != EOF) {
		d_print("error: only one byte push back possible!\n");
		return EOF;
	}
	file->push_back_byte = c;
	return c;
}

static uint32_t get_length(void *data)
{
	struct wavpack_file *file = data;
	return file->len;
}

static int can_seek(void *data)
{
	struct wavpack_file *file = data;
	return file->len != -1;
}

static int32_t write_bytes(void *data, void *ptr, int32_t count)
{
	/* we shall not write any bytes */
	return 0;
}


/*
 * typedef struct {
 *     int32_t (*read_bytes)(void *id, void *data, int32_t bcount);
 *     uint32_t (*get_pos)(void *id);
 *     int (*set_pos_abs)(void *id, uint32_t pos);
 *     int (*set_pos_rel)(void *id, int32_t delta, int mode);
 *     int (*push_back_byte)(void *id, int c);
 *     uint32_t (*get_length)(void *id);
 *     int (*can_seek)(void *id);
 *
 *     // this callback is for writing edited tags only
 *     int32_t (*write_bytes)(void *id, void *data, int32_t bcount);
 * } WavpackStreamReader;
 */
static WavpackStreamReader callbacks = {
	.read_bytes = read_bytes,
	.get_pos = get_pos,
	.set_pos_abs = set_pos_abs,
	.set_pos_rel = set_pos_rel,
	.push_back_byte = push_back_byte,
	.get_length = get_length,
	.can_seek = can_seek,
	.write_bytes = write_bytes
};

static int wavpack_open(struct input_plugin_data *ip_data)
{
	struct wavpack_private *priv;
	struct stat st;
	char msg[80];
	int channel_mask = 0;

	const struct wavpack_private priv_init = {
		.wv_file = {
			.fd = ip_data->fd,
			.push_back_byte = EOF
		}
	};

	priv = xnew(struct wavpack_private, 1);
	*priv = priv_init;
	if (!ip_data->remote && fstat(ip_data->fd, &st) == 0) {
		char *filename_wvc;

		priv->wv_file.len = st.st_size;

		filename_wvc = xnew(char, strlen(ip_data->filename) + 2);
		sprintf(filename_wvc, "%sc", ip_data->filename);
		if (stat(filename_wvc, &st) == 0) {
			priv->wvc_file.fd = open(filename_wvc, O_RDONLY);
			if (priv->wvc_file.fd != -1) {
				priv->wvc_file.len = st.st_size;
				priv->wvc_file.push_back_byte = EOF;
				priv->has_wvc = 1;
				d_print("use correction file: %s\n", filename_wvc);
			}
		}
		free(filename_wvc);
	} else
		priv->wv_file.len = -1;
	ip_data->private = priv;

	*msg = '\0';

	priv->wpc = WavpackOpenFileInputEx(&callbacks, &priv->wv_file,
			priv->has_wvc ? &priv->wvc_file : NULL, msg,
			OPEN_NORMALIZE, 0);

	if (!priv->wpc) {
		d_print("WavpackOpenFileInputEx failed: %s\n", msg);
		free(priv);
		return -IP_ERROR_FILE_FORMAT;
	}

	ip_data->sf = sf_rate(WavpackGetSampleRate(priv->wpc))
		| sf_channels(WavpackGetReducedChannels(priv->wpc))
		| sf_bits(WavpackGetBitsPerSample(priv->wpc))
		| sf_signed(1);
	channel_mask = WavpackGetChannelMask(priv->wpc);
	channel_map_init_waveex(sf_get_channels(ip_data->sf), channel_mask, ip_data->channel_map);
	return 0;
}

static int wavpack_close(struct input_plugin_data *ip_data)
{
	struct wavpack_private *priv;

	priv = ip_data->private;
	priv->wpc = WavpackCloseFile(priv->wpc);
	if (priv->has_wvc)
		close(priv->wvc_file.fd);
	free(priv);
	ip_data->private = NULL;
	return 0;
}

/* from wv_engine.cpp (C) 2006 by Peter Lemenkov <lemenkov@newmail.ru> */
static char *format_samples(int bps, char *dst, int32_t *src, uint32_t count)
{
	int32_t temp;

	switch (bps) {
	case 1:
		while (count--)
			*dst++ = *src++ + 128;
		break;
	case 2:
		while (count--) {
			*dst++ = (char) (temp = *src++);
			*dst++ = (char) (temp >> 8);
		}
		break;
	case 3:
		while (count--) {
			*dst++ = (char) (temp = *src++);
			*dst++ = (char) (temp >> 8);
			*dst++ = (char) (temp >> 16);
		}
		break;
	case 4:
		while (count--) {
			*dst++ = (char) (temp = *src++);
			*dst++ = (char) (temp >> 8);
			*dst++ = (char) (temp >> 16);
			*dst++ = (char) (temp >> 24);
		}
		break;
	}

	return dst;
}

static int wavpack_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	struct wavpack_private *priv;
	int rc, bps, sample_count, channels;

	priv = ip_data->private;
	channels = sf_get_channels(ip_data->sf);
	bps = WavpackGetBytesPerSample(priv->wpc);

	sample_count = count / bps;

	rc = WavpackUnpackSamples(priv->wpc, priv->samples, sample_count / channels);
	format_samples(bps, buffer, priv->samples, rc * channels);
	return rc * channels * bps;
}

static int wavpack_seek(struct input_plugin_data *ip_data, double offset)
{
	struct wavpack_private *priv = ip_data->private;

	if (!WavpackSeekSample(priv->wpc, WavpackGetSampleRate(priv->wpc) * offset))
		return -IP_ERROR_INTERNAL;
	return 0;
}

static int wavpack_read_comments(struct input_plugin_data *ip_data,
		struct keyval **comments)
{
	struct id3tag id3;
	APETAG(ape);
	GROWING_KEYVALS(c);
	int fd, rc, save, i;

	fd = open(ip_data->filename, O_RDONLY);
	if (fd == -1)
		return -1;
	d_print("filename: %s\n", ip_data->filename);

	id3_init(&id3);
	rc = id3_read_tags(&id3, fd, ID3_V1);
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

	rc = ape_read_tags(&ape, ip_data->fd, 1);
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

	keyvals_terminate(&c);
	*comments = c.keyvals;
	return 0;
}

static int wavpack_duration(struct input_plugin_data *ip_data)
{
	struct wavpack_private *priv;
	int duration;

	priv = ip_data->private;
	duration = WavpackGetNumSamples(priv->wpc) /
		WavpackGetSampleRate(priv->wpc);

	return duration;
}

static long wavpack_bitrate(struct input_plugin_data *ip_data)
{
	struct wavpack_private *priv = ip_data->private;
	double bitrate = WavpackGetAverageBitrate(priv->wpc, 1);
	if (!bitrate)
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
	return (long) (bitrate + 0.5);
}

static long wavpack_current_bitrate(struct input_plugin_data *ip_data)
{
	struct wavpack_private *priv = ip_data->private;
	return WavpackGetInstantBitrate(priv->wpc);
}

static char *wavpack_codec(struct input_plugin_data *ip_data)
{
	return xstrdup("wavpack");
}

static char *wavpack_codec_profile(struct input_plugin_data *ip_data)
{
	struct wavpack_private *priv = ip_data->private;
	int m = WavpackGetMode(priv->wpc);
	char buf[32];

	buf[0] = '\0';

	if (m & MODE_FAST)
		strcat(buf, "fast");
#ifdef MODE_VERY_HIGH
	else if (m & MODE_VERY_HIGH)
		strcat(buf, "very high");
#endif
	else if (m & MODE_HIGH)
		strcat(buf, "high");
	else
		strcat(buf, "normal");

	if (m & MODE_HYBRID)
		strcat(buf, " hybrid");

#ifdef MODE_XMODE
	if ((m & MODE_EXTRA) && (m & MODE_XMODE)) {
		char xmode[] = " x0";
		xmode[2] = ((m & MODE_XMODE) >> 12) + '0';
		strcat(buf, xmode);
	}
#endif

	return xstrdup(buf);
}

const struct input_plugin_ops ip_ops = {
	.open = wavpack_open,
	.close = wavpack_close,
	.read = wavpack_read,
	.seek = wavpack_seek,
	.read_comments = wavpack_read_comments,
	.duration = wavpack_duration,
	.bitrate = wavpack_bitrate,
	.bitrate_current = wavpack_current_bitrate,
	.codec = wavpack_codec,
	.codec_profile = wavpack_codec_profile
};

const int ip_priority = 50;
const char * const ip_extensions[] = { "wv", NULL };
const char * const ip_mime_types[] = { "audio/x-wavpack", NULL };
const struct input_plugin_opt ip_options[] = { { NULL } };
const unsigned ip_abi_version = IP_ABI_VERSION;
