/*
 * Copyright 2006 Chun-Yu Shei <cshei AT cs.indiana.edu>
 *
 * Cleaned up by Timo Hirvonen <tihirvon@gmail.com>
 */

#include "ip.h"
#include "comment.h"
#include "file.h"
#include "xmalloc.h"
#include "read_wrapper.h"

#include <mpcdec/mpcdec.h>
#include <inttypes.h>
#include <errno.h>

/* http://www.personal.uni-jena.de/~pfk/mpp/sv8/apetag.html */

#define PREAMBLE_SIZE (8)
static const char preamble[PREAMBLE_SIZE] = { 'A', 'P', 'E', 'T', 'A', 'G', 'E', 'X' };

/* NOTE: not sizeof(struct ape_header)! */
#define HEADER_SIZE (32)

struct ape_header {
	/* 1000 or 2000 (1.0, 2.0) */
	uint32_t version;

	/* tag size (header + tags, excluding footer) */
	uint32_t size;

	/* number of items */
	uint32_t count;

	/* global flags for each tag
	 * there are also private flags for every tag
	 * NOTE: 0 for version 1.0 (1000)
	 */
	uint32_t flags;
};

/* ape flags */
#define AF_IS_UTF8(f)		(((f) & 6) == 0)
#define AF_IS_FOOTER(f)		(((f) & (1 << 29)) == 0)

struct mpc_private {
	mpc_decoder decoder;
	mpc_reader reader;
	mpc_streaminfo info;

	off_t file_size;

	int samples_pos;
	int samples_avail;

	/* mpcdec/mpcdec.h
	 *
	 * the api doc says this is pcm samples per mpc frame
	 * but it's really pcm _frames_ per mpc frame
	 *     MPC_FRAME_LENGTH = 36 * 32 (1152)
	 *
	 * this is wrong, it should be 2 * MPC_FRAME_LENGTH (2304)
	 *     MPC_DECODER_BUFFER_LENGTH = 4 * MPC_FRAME_LENGTH (4608)
	 *
	 * use MPC_DECODER_BUFFER_LENGTH just to be sure it works
	 */
	MPC_SAMPLE_FORMAT samples[MPC_DECODER_BUFFER_LENGTH];
};

static inline uint32_t get_le32(const char *buf)
{
	const unsigned char *b = (const unsigned char *)buf;

	return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

/* callbacks {{{ */
static mpc_int32_t read_impl(void *data, void *ptr, mpc_int32_t size)
{
	struct input_plugin_data *ip_data = data;
	int rc;

	rc = read_wrapper(ip_data, ptr, size);
	if (rc == -1)
		return -1;
	if (rc == 0) {
		errno = 0;
		return 0;
	}
	return rc;
}

static mpc_bool_t seek_impl(void *data, mpc_int32_t offset)
{
	struct input_plugin_data *ip_data = data;
	int rc;

	rc = lseek(ip_data->fd, offset, SEEK_SET);
	if (rc == -1)
		return FALSE;
	return TRUE;
}

static mpc_int32_t tell_impl(void *data)
{
	struct input_plugin_data *ip_data = data;

	return lseek(ip_data->fd, 0, SEEK_CUR);
}

static mpc_int32_t get_size_impl(void *data)
{
	struct input_plugin_data *ip_data = data;
	struct mpc_private *priv = ip_data->private;

	return priv->file_size;
}

static mpc_bool_t canseek_impl(void *data)
{
	struct input_plugin_data *ip_data = data;

	return !ip_data->remote;
}
/* }}} */

/* ape {{{ */

/* returns position of APE header or -1 if not found */
static int find_ape_tag_slow(int fd)
{
	char buf[4096];
	int match = 0;
	int pos = 0;

	/* seek to start of file */
	if (lseek(fd, pos, SEEK_SET) == -1)
		return -1;

	while (1) {
		int i, got = read(fd, buf, sizeof(buf));

		if (got == -1) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			break;
		}
		if (got == 0)
			break;

		for (i = 0; i < got; i++) {
			if (buf[i] != preamble[match]) {
				match = 0;
				continue;
			}

			match++;
			if (match == PREAMBLE_SIZE)
				return pos + i + 1 - PREAMBLE_SIZE;
		}
		pos += got;
	}
	return -1;
}

static int ape_parse_header(const char *buf, struct ape_header *h)
{
	if (memcmp(buf, preamble, PREAMBLE_SIZE))
		return 0;

	h->version = get_le32(buf + 8);
	h->size = get_le32(buf + 12);
	h->count = get_le32(buf + 16);
	h->flags = get_le32(buf + 20);
	return 1;
}

static int read_header(int fd, struct ape_header *h)
{
	char buf[HEADER_SIZE];

	if (read_all(fd, buf, sizeof(buf)) != sizeof(buf))
		return 0;
	return ape_parse_header(buf, h);
}

/* sets fd right after the header and returns 1 if found,
 * otherwise returns 0
 */
static int find_ape_tag(int fd, struct ape_header *h)
{
	int pos;

	if (lseek(fd, -HEADER_SIZE, SEEK_END) == -1)
		return 0;
	if (read_header(fd, h))
		return 1;

	pos = find_ape_tag_slow(fd);
	if (pos == -1)
		return 0;
	if (lseek(fd, pos, SEEK_SET) == -1)
		return 0;
	return read_header(fd, h);
}

/*
 * All keys are ASCII and length is 2..255
 *
 * UTF-8:	Artist, Album, Title, Genre
 * Integer:	Track (N or N/M)
 * Date:	Year (release), "Record Date"
 *
 * UTF-8 strings are NOT zero terminated.
 *
 * Also support "discnumber" (vorbis) and "disc" (non-standard)
 */
static int ape_parse_one(const char *buf, int size, char **keyp, char **valp)
{
	int pos = 0;

	while (size - pos > 8) {
		uint32_t val_len, flags;
		char *key, *val;
		int max_key_len, key_len;

		val_len = get_le32(buf + pos); pos += 4;
		flags = get_le32(buf + pos); pos += 4;

		max_key_len = size - pos - val_len - 1;
		if (max_key_len < 0) {
			/* corrupt */
			break;
		}

		for (key_len = 0; key_len < max_key_len && buf[pos + key_len]; key_len++)
			; /* nothing */
		if (buf[pos + key_len]) {
			/* corrupt */
			break;
		}

		if (!AF_IS_UTF8(flags)) {
			/* ignore binary data */
			pos += key_len + 1 + val_len;
			continue;
		}

		key = xstrdup(buf + pos);
		pos += key_len + 1;

		/* should not be NUL-terminated */
		val = xstrndup(buf + pos, val_len);
		pos += val_len;

		/* could be moved to comment.c but I don't think anyone else would use it */
		if (!strcasecmp(key, "record date")) {
			free(key);
			key = xstrdup("year");
		}

		if (!strcasecmp(key, "year")) {
			/* Date format
			 *
			 * 1999-08-11 12:34:56
			 * 1999-08-11 12:34
			 * 1999-08-11
			 * 1999-08
			 * 1999
			 * 1999-W34	(week 34, totally crazy)
			 *
			 * convert to year, pl.c supports only years anyways
			 *
			 * FIXME: which one is the most common tag (year or record date)?
			 */
			if (strlen(val) > 4)
				val[4] = 0;
		}

		*keyp = key;
		*valp = val;
		return pos;
	}
	return -1;
}

/* }}} */

static int mpc_open(struct input_plugin_data *ip_data)
{
	struct mpc_private *priv;

	priv = xnew0(struct mpc_private, 1);

	priv->file_size = -1;
	if (!ip_data->remote) {
		priv->file_size = lseek(ip_data->fd, 0, SEEK_END);
		lseek(ip_data->fd, 0, SEEK_SET);
	}

	/* set up an mpc_reader linked to our function implementations */
	priv->reader.read = read_impl;
	priv->reader.seek = seek_impl;
	priv->reader.tell = tell_impl;
	priv->reader.get_size = get_size_impl;
	priv->reader.canseek = canseek_impl;
	priv->reader.data = ip_data;

	/* must be before mpc_streaminfo_read() */
	ip_data->private = priv;

	/* read file's streaminfo data */
	mpc_streaminfo_init(&priv->info);
	if (mpc_streaminfo_read(&priv->info, &priv->reader) != ERROR_CODE_OK) {
		free(priv);
		return -IP_ERROR_FILE_FORMAT;
	}

	/* instantiate a decoder with our file reader */
	mpc_decoder_setup(&priv->decoder, &priv->reader);
	if (!mpc_decoder_initialize(&priv->decoder, &priv->info)) {
		free(priv);
		return -IP_ERROR_FILE_FORMAT;
	}

	priv->samples_avail = 0;
	priv->samples_pos = 0;

	ip_data->sf = sf_rate(priv->info.sample_freq) | sf_channels(priv->info.channels) |
		sf_bits(16) | sf_signed(1);
	return 0;
}

static int mpc_close(struct input_plugin_data *ip_data)
{
	struct mpc_private *priv = ip_data->private;

	free(priv);
	ip_data->private = NULL;
	return 0;
}

static int scale(struct input_plugin_data *ip_data, char *buffer, int count)
{
	struct mpc_private *priv = ip_data->private;
	const MPC_SAMPLE_FORMAT *samples;
	const int clip_min = -1 << (16 - 1);
	const int clip_max = (1 << (16 - 1)) - 1;
	const int float_scale = 1 << (16 - 1);
	int i, sample_count;

	/* number of bytes to 16-bit samples */
	sample_count = count / 2;
	if (sample_count > priv->samples_avail)
		sample_count = priv->samples_avail;

	/* scale 32-bit samples to 16-bit */
	samples = priv->samples + priv->samples_pos;
	for (i = 0; i < sample_count; i++) {
		int val;

		val = samples[i] * float_scale;
		if (val < clip_min) {
			val = clip_min;
		} else if (val > clip_max) {
			val = clip_max;
		}

		buffer[i * 2 + 0] = val & 0xff;
		buffer[i * 2 + 1] = val >> 8;
	}

	priv->samples_pos += sample_count;
	priv->samples_avail -= sample_count;
	if (priv->samples_avail == 0)
		priv->samples_pos = 0;

	/* number of 16-bit samples to bytes */
	return sample_count * 2;
}

static int mpc_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	struct mpc_private *priv = ip_data->private;

	if (priv->samples_avail == 0) {
		uint32_t status = mpc_decoder_decode(&priv->decoder, priv->samples, NULL, NULL);

		if (status == (uint32_t)(-1)) {
			/* right ret val? */
			return -IP_ERROR_ERRNO;
		}
		if (status == 0) {
			/* EOF */
			return 0;
		}

		/* status seems to be number of _frames_
		 * the api documentation is wrong
		 */
		priv->samples_avail = status * priv->info.channels;
	}
	return scale(ip_data, buffer, count);
}

static int mpc_seek(struct input_plugin_data *ip_data, double offset)
{
	struct mpc_private *priv = ip_data->private;

	priv->samples_pos = 0;
	priv->samples_avail = 0;

	if (mpc_decoder_seek_seconds(&priv->decoder, offset))
		return 0;
	return -1;
}

static const char *gain_to_str(int gain)
{
	static char buf[16];
	int b, a = gain / 100;

	if (gain < 0) {
		b = -gain % 100;
	} else {
		b = gain % 100;
	}
	sprintf(buf, "%d.%02d", a, b);
	return buf;
}

static const char *peak_to_str(unsigned int peak)
{
	static char buf[16];
	sprintf(buf, "%d.%05d", peak / 32767, peak % 32767);
	return buf;
}

static int mpc_read_comments(struct input_plugin_data *ip_data, struct keyval **comments)
{
	struct mpc_private *priv = ip_data->private;
	int old_pos, fd = ip_data->fd;
	struct ape_header h;
	char *buf;
	GROWING_KEYVALS(c);

	/* save position */
	old_pos = lseek(fd, 0, SEEK_CUR);

	if (!find_ape_tag(fd, &h))
		goto out;

	if (AF_IS_FOOTER(h.flags)) {
		off_t file_size = get_size_impl(ip_data);
		/* seek back right after the header */
		if (lseek(fd, file_size - h.size, SEEK_SET) == -1)
			goto out;
	}

	/* ignore insane tags */
	if (h.size > 1024 * 1024)
		goto out;

	buf = xnew(char, h.size);
	if (read_all(fd, buf, h.size) == h.size) {
		int pos = 0, rc;

		while (pos < h.size) {
			char *k, *v;
			rc = ape_parse_one(buf + pos, h.size - pos, &k, &v);
			if (rc < 0)
				break;

			comments_add(&c, k, v);
			free(k);

			pos += rc;
		}
	}
	free(buf);
out:
	if (priv->info.gain_title && priv->info.peak_title) {
		comments_add_const(&c, "replaygain_track_gain", gain_to_str(priv->info.gain_title));
		comments_add_const(&c, "replaygain_track_peak", peak_to_str(priv->info.peak_title));
	}
	if (priv->info.gain_album && priv->info.peak_album) {
		comments_add_const(&c, "replaygain_album_gain", gain_to_str(priv->info.gain_album));
		comments_add_const(&c, "replaygain_album_peak", peak_to_str(priv->info.peak_album));
	}
	comments_terminate(&c);

	lseek(fd, old_pos, SEEK_SET);
	*comments = c.comments;
	return 0;
}

static int mpc_duration(struct input_plugin_data *ip_data)
{
	struct mpc_private *priv = ip_data->private;

	/* priv->info.pcm_samples seems to be number of frames
	 * priv->info.frames is _not_ pcm frames
	 */
	return priv->info.pcm_samples / priv->info.sample_freq;
}

const struct input_plugin_ops ip_ops = {
	.open = mpc_open,
	.close = mpc_close,
	.read = mpc_read,
	.seek = mpc_seek,
	.read_comments = mpc_read_comments,
	.duration = mpc_duration
};

const char *const ip_extensions[] = { "mpc", "mpp", "mp+", NULL };
const char *const ip_mime_types[] = { "audio/x-musepack", NULL };
