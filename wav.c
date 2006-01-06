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

#include <ip.h>
#include <file.h>
#include <xmalloc.h>
#include <debug.h>

#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <unistd.h>

struct wav_private {
	unsigned int pcm_start;
	unsigned int pcm_size;
	unsigned int pos;

	/* size of one second of data */
	unsigned int sec_size;
};

static inline unsigned short read_u2(const char *buffer)
{
	const unsigned char *buf = (const unsigned char *)buffer;

	return buf[0] + (buf[1] << 8);
}

static inline unsigned int read_u4(const char *buffer)
{
	const unsigned char *buf = (const unsigned char *)buffer;

	return buf[0] + (buf[1] << 8) + (buf[2] << 16) + (buf[3] << 24);
}

static int read_chunk_header(int fd, const char *name, unsigned int *size)
{
	int rc;
	char buf[8];

	rc = read_all(fd, buf, 8);
	if (rc == -1)
		return -IP_ERROR_ERRNO;
	if (rc != 8)
		return -IP_ERROR_FILE_FORMAT;
	*size = read_u4(buf + 4);
	if (memcmp(buf, name, 4))
		return -IP_ERROR_FILE_FORMAT;
	return 0;
}

static int find_chunk(int fd, const char *name, unsigned int *size)
{
	int rc;

	do {
		rc = read_chunk_header(fd, name, size);
		if (rc == 0)
			return 0;
		if (rc != -IP_ERROR_FILE_FORMAT)
			return rc;
		d_print("seeking %d\n", *size);
		if (lseek(fd, *size, SEEK_CUR) == -1) {
			d_print("seek failed\n");
			return -IP_ERROR_ERRNO;
		}
	} while (1);
}

static int wav_open(struct input_plugin_data *ip_data)
{
	struct wav_private *priv;
	char buf[4];
	char *fmt;
	int rc;
	unsigned int riff_size, fmt_size;
	int save;

	d_print("file: %s\n", ip_data->filename);
	priv = xnew(struct wav_private, 1);
	ip_data->private = priv;
	rc = read_chunk_header(ip_data->fd, "RIFF", &riff_size);
	if (rc)
		goto error_exit;
	rc = read_all(ip_data->fd, buf, 4);
	if (rc == -1) {
		rc = -IP_ERROR_ERRNO;
		goto error_exit;
	}
	if (rc != 4 || memcmp(buf, "WAVE", 4) != 0) {
		rc = -IP_ERROR_FILE_FORMAT;
		goto error_exit;
	}

	rc = find_chunk(ip_data->fd, "fmt ", &fmt_size);
	if (rc)
		goto error_exit;
	if (fmt_size < 16) {
		d_print("size of \"fmt \" chunk is invalid (%d)\n", fmt_size);
		rc = -IP_ERROR_FILE_FORMAT;
		goto error_exit;
	}
	fmt = xnew(char, fmt_size);
	rc = read_all(ip_data->fd, fmt, fmt_size);
	if (rc == -1) {
		save = errno;
		free(fmt);
		errno = save;
		rc = -IP_ERROR_ERRNO;
		goto error_exit;
	}
	if (rc != fmt_size) {
		save = errno;
		free(fmt);
		errno = save;
		rc = -IP_ERROR_FILE_FORMAT;
		goto error_exit;
	}
	{
		int format_tag, channels, rate, bits;

		format_tag = read_u2(fmt + 0);
		channels = read_u2(fmt + 2);
		rate = read_u4(fmt + 4);
		/* 4 bytes, bytes per second */
		/* 2 bytes, bytes per sample */
		bits = read_u2(fmt + 14);
		free(fmt);

		if (format_tag != 1) {
			d_print("invalid format tag %d, should be 1\n", format_tag);
			rc = -IP_ERROR_FILE_FORMAT;
			goto error_exit;
		}
		if ((bits != 8 && bits != 16) || channels < 1 || channels > 2) {
			rc = -IP_ERROR_SAMPLE_FORMAT;
			goto error_exit;
		}
		ip_data->sf = sf_channels(channels) | sf_rate(rate) | sf_bits(bits) |
			sf_signed(bits > 8);
	}

	rc = find_chunk(ip_data->fd, "data", &priv->pcm_size);
	if (rc)
		goto error_exit;
	rc = lseek(ip_data->fd, 0, SEEK_CUR);
	if (rc == -1) {
		rc = -IP_ERROR_ERRNO;
		goto error_exit;
	}
	priv->pcm_start = rc;

	priv->sec_size = sf_get_second_size(ip_data->sf);
	priv->pos = 0;

	d_print("pcm start: %d\n", priv->pcm_start);
	d_print("pcm size: %d\n", priv->pcm_size);
	d_print("\n");
	d_print("sr: %d, ch: %d, bits: %d, signed: %d\n", sf_get_rate(ip_data->sf),
			sf_get_channels(ip_data->sf), sf_get_bits(ip_data->sf),
			sf_get_signed(ip_data->sf));
	return 0;
error_exit:
	save = errno;
	free(priv);
	errno = save;
	return rc;
}

static int wav_close(struct input_plugin_data *ip_data)
{
	struct wav_private *priv;

	priv = ip_data->private;
	free(priv);
	ip_data->private = NULL;
	return 0;
}

static int wav_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	struct wav_private *priv;
	int rc;

	priv = ip_data->private;
	if (priv->pos == priv->pcm_size) {
		/* eof */
		return 0;
	}
	if (count > priv->pcm_size - priv->pos)
		count = priv->pcm_size - priv->pos;
	rc = read(ip_data->fd, buffer, count);
	if (rc == -1) {
		d_print("read error\n");
		return -IP_ERROR_ERRNO;
	}
	if (rc == 0) {
		d_print("eof\n");
		return 0;
	}
	priv->pos += rc;
	return rc;
}

static int wav_seek(struct input_plugin_data *ip_data, double _offset)
{
	struct wav_private *priv;
	int offset, rc;

	priv = ip_data->private;
	offset = (int)(_offset * (double)priv->sec_size + 0.5);
	/* aling to 4 bytes (2 * 16 / 8) */
	offset -= offset % 4;
	priv->pos = offset;
	rc = lseek(ip_data->fd, priv->pcm_start + offset, SEEK_SET);
	if (rc == -1)
		return -1;
	return 0;
}

static int wav_read_comments(struct input_plugin_data *ip_data,
		struct keyval **comments)
{
	*comments = xnew0(struct keyval, 1);
	return 0;
}

static int wav_duration(struct input_plugin_data *ip_data)
{
	struct wav_private *priv;
	int duration;

	priv = ip_data->private;
	duration = priv->pcm_size / priv->sec_size;
	return duration;
}

const struct input_plugin_ops ip_ops = {
	.open = wav_open,
	.close = wav_close,
	.read = wav_read,
	.seek = wav_seek,
	.read_comments = wav_read_comments,
	.duration = wav_duration
};

const char * const ip_extensions[] = { "wav", NULL };
const char * const ip_mime_types[] = { NULL };
