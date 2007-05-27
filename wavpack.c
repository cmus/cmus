/*
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "ip.h"
#include "ape.h"
#include "id3.h"
#include "xmalloc.h"
#include "read_wrapper.h"
#include "debug.h"
#include "buffer.h"

#include <wavpack/wavpack.h>

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#define WV_CHANNEL_MAX 2

struct wavpack_private {
	WavpackContext *wpc;
	int32_t samples[CHUNK_SIZE * WV_CHANNEL_MAX];
};

/* http://www.wavpack.com/lib_use.txt */

static int32_t read_bytes(void *data, void *ptr, int32_t count)
{
	struct input_plugin_data *ip_data = data;
	int rc;

	rc = read_wrapper(ip_data, ptr, count);
	if (rc == -1) {
		d_print("error: %s\n", strerror(errno));
		return 0;
	}
	if (rc == 0) {
		errno = 0;
		return 0;
	}
	return rc;
}

static uint32_t get_pos(void *data)
{
	struct input_plugin_data *ip_data = data;

	return lseek(ip_data->fd, 0, SEEK_CUR);
}

static int set_pos_abs(void *data, uint32_t pos)
{
	struct input_plugin_data *ip_data = data;

	lseek(ip_data->fd, pos, SEEK_SET);
	return errno;
}

static int set_pos_rel(void *data, int32_t delta, int mode)
{
	struct input_plugin_data *ip_data = data;

	lseek(ip_data->fd, delta, mode);
	return errno;
}

static int push_back_byte(void *data, int c)
{
	/* not possible? */
	d_print("NOT POSSIBLE\n");
	return EOF;
}

static uint32_t get_length(void *data)
{
	struct input_plugin_data *ip_data = data;
	struct stat statbuf;

	if (fstat(ip_data->fd, &statbuf) || !(statbuf.st_mode & S_IFREG))
		return 0;

	return statbuf.st_size;
}

static int can_seek(void *data)
{
	struct input_plugin_data *ip_data = data;
	struct stat statbuf;

	return !fstat(ip_data->fd, &statbuf) && (statbuf.st_mode & S_IFREG);
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
	char msg[80];

	priv = xnew(struct wavpack_private, 1);
	priv->wpc = NULL;
	*msg = '\0';

	priv->wpc = WavpackOpenFileInputEx(&callbacks, ip_data, NULL, msg,
			OPEN_2CH_MAX | OPEN_NORMALIZE, 0);

	if (!priv->wpc) {
		d_print("WavpackOpenFileInputEx failed: %s\n", msg);
		free(priv);
		return -IP_ERROR_FILE_FORMAT;
	}
	ip_data->private = priv;

	ip_data->sf = sf_rate(WavpackGetSampleRate(priv->wpc))
		| sf_channels(WavpackGetReducedChannels(priv->wpc))
		| sf_bits(WavpackGetBitsPerSample(priv->wpc))
		| sf_signed(1);

	return 0;
}

static int wavpack_close(struct input_plugin_data *ip_data)
{
	struct wavpack_private *priv;

	priv = ip_data->private;
	priv->wpc = WavpackCloseFile(priv->wpc);
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
	format_samples(bps, buffer, priv->samples, sample_count);

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
	ID3 *id3;
	APETAG(ape);
	GROWING_KEYVALS(c);
	int fd, rc, save, i;

	fd = open(ip_data->filename, O_RDONLY);
	if (fd == -1)
		return -1;
	d_print("filename: %s\n", ip_data->filename);

	id3 = id3_new();
	rc = id3_read_tags(id3, fd, ID3_V1);
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
		char *val = id3_get_comment(id3, i);
		if (val)
			comments_add(&c, id3_key_names[i], val);
	}

next:
	id3_free(id3);

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

	comments_terminate(&c);
	*comments = c.comments;
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

const struct input_plugin_ops ip_ops = {
	.open = wavpack_open,
	.close = wavpack_close,
	.read = wavpack_read,
	.seek = wavpack_seek,
	.read_comments = wavpack_read_comments,
	.duration = wavpack_duration
};

const char * const ip_extensions[] = { "wv", NULL };
const char * const ip_mime_types[] = { "audio/x-wavpack", NULL };
