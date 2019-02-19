/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2005 Timo Hirvonen
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
#include "../comment.h"
#include "../xmalloc.h"
#include "../debug.h"
#include "../utils.h"

#include <FLAC/export.h>
#include <FLAC/stream_decoder.h>
#include <FLAC/metadata.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#ifndef UINT64_MAX
#define UINT64_MAX ((uint64_t)-1)
#endif

/* Reduce typing.  Namespaces are nice but FLAC API is fscking ridiculous.  */

/* functions, types, enums */
#define F(s) FLAC__stream_decoder_ ## s
#define T(s) FLAC__StreamDecoder ## s
#define Dec FLAC__StreamDecoder
#define E(s) FLAC__STREAM_DECODER_ ## s

struct flac_private {
	/* file/stream position and length */
	uint64_t pos;
	uint64_t len;

	Dec *dec;

	/* PCM data */
	char *buf;
	unsigned int buf_size;
	unsigned int buf_wpos;
	unsigned int buf_rpos;

	struct keyval *comments;
	double duration;
	long bitrate;
	int bps;
};

static T(ReadStatus) read_cb(const Dec *dec, unsigned char *buf, size_t *size, void *data)
{
	struct input_plugin_data *ip_data = data;
	struct flac_private *priv = ip_data->private;
	int rc;

	if (priv->pos == priv->len) {
		*size = 0;
		return E(READ_STATUS_END_OF_STREAM);
	}
	if (*size == 0)
		return E(READ_STATUS_CONTINUE);

	rc = read(ip_data->fd, buf, *size);
	if (rc == -1) {
		*size = 0;
		if (errno == EINTR || errno == EAGAIN) {
			/* FIXME: not sure how the flac decoder handles this */
			d_print("interrupted\n");
			return E(READ_STATUS_CONTINUE);
		}
		return E(READ_STATUS_ABORT);
	}

	priv->pos += rc;
	*size = rc;
	if (rc == 0) {
		/* should not happen */
		return E(READ_STATUS_END_OF_STREAM);
	}
	return E(READ_STATUS_CONTINUE);
}

static T(SeekStatus) seek_cb(const Dec *dec, uint64_t offset, void *data)
{
	struct input_plugin_data *ip_data = data;
	struct flac_private *priv = ip_data->private;
	off_t off;

	if (priv->len == UINT64_MAX)
		return E(SEEK_STATUS_ERROR);
	off = lseek(ip_data->fd, offset, SEEK_SET);
	if (off == -1) {
		return E(SEEK_STATUS_ERROR);
	}
	priv->pos = off;
	return E(SEEK_STATUS_OK);
}

static T(TellStatus) tell_cb(const Dec *dec, uint64_t *offset, void *data)
{
	struct input_plugin_data *ip_data = data;
	struct flac_private *priv = ip_data->private;

	d_print("\n");
	*offset = priv->pos;
	return E(TELL_STATUS_OK);
}

static T(LengthStatus) length_cb(const Dec *dec, uint64_t *len, void *data)
{
	struct input_plugin_data *ip_data = data;
	struct flac_private *priv = ip_data->private;

	d_print("\n");
	if (ip_data->remote) {
		return E(LENGTH_STATUS_ERROR);
	}
	*len = priv->len;
	return E(LENGTH_STATUS_OK);
}

static int eof_cb(const Dec *dec, void *data)
{
	struct input_plugin_data *ip_data = data;
	struct flac_private *priv = ip_data->private;

	return priv->pos == priv->len;;
}

#if defined(WORDS_BIGENDIAN)

#define LE32(x) swap_uint32(x)

#else

#define LE32(x)	(x)

#endif

static FLAC__StreamDecoderWriteStatus write_cb(const Dec *dec, const FLAC__Frame *frame,
		const int32_t * const *buf, void *data)
{
	struct input_plugin_data *ip_data = data;
	struct flac_private *priv = ip_data->private;
	int frames, bytes, size, channels, bits, depth;
	int ch, nch, i = 0;
	char *dest; int32_t src;

	frames = frame->header.blocksize;
	channels = sf_get_channels(ip_data->sf);
	bits = sf_get_bits(ip_data->sf);
	bytes = frames * bits / 8 * channels;
	size = priv->buf_size;

	if (size - priv->buf_wpos < bytes) {
		if (size < bytes)
			size = bytes;
		size *= 2;
		priv->buf = xrenew(char, priv->buf, size);
		priv->buf_size = size;
	}

	depth = frame->header.bits_per_sample;
	if (!depth)
		depth = priv->bps;
	nch = frame->header.channels;
	dest = priv->buf + priv->buf_wpos;
	for (i = 0; i < frames; i++) {
		for (ch = 0; ch < channels; ch++) {
			src = LE32(buf[ch % nch][i] << (bits - depth));
			memcpy(dest, &src, bits / 8);
			dest += bits / 8;
		}
	}

	priv->buf_wpos += bytes;
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

/* You should make a copy of metadata with FLAC__metadata_object_clone() if you will
 * need it elsewhere. Since metadata blocks can potentially be large, by
 * default the decoder only calls the metadata callback for the STREAMINFO
 * block; you can instruct the decoder to pass or filter other blocks with
 * FLAC__stream_decoder_set_metadata_*() calls.
 */
static void metadata_cb(const Dec *dec, const FLAC__StreamMetadata *metadata, void *data)
{
	struct input_plugin_data *ip_data = data;
	struct flac_private *priv = ip_data->private;

	switch (metadata->type) {
	case FLAC__METADATA_TYPE_STREAMINFO:
		{
			const FLAC__StreamMetadata_StreamInfo *si = &metadata->data.stream_info;
			int bits = 0;

			if (si->bits_per_sample >= 4 && si->bits_per_sample <= 32) {
				bits = priv->bps = si->bits_per_sample;
				bits = 8 * ((bits + 7) / 8);
			}

			ip_data->sf = sf_rate(si->sample_rate) |
				sf_bits(bits) |
				sf_signed(1) |
				sf_channels(si->channels);
			if (!ip_data->remote && si->total_samples) {
				priv->duration = (double) si->total_samples / si->sample_rate;
				if (priv->duration >= 1 && priv->len >= 1)
					priv->bitrate = priv->len * 8 / priv->duration;
			}
		}
		break;
	case FLAC__METADATA_TYPE_VORBIS_COMMENT:
		d_print("VORBISCOMMENT\n");
		if (priv->comments) {
			d_print("Ignoring\n");
		} else {
			GROWING_KEYVALS(c);
			int i, nr;

			nr = metadata->data.vorbis_comment.num_comments;
			for (i = 0; i < nr; i++) {
				const char *str = (const char *)metadata->data.vorbis_comment.comments[i].entry;
				char *key, *val;

				val = strchr(str, '=');
				if (!val)
					continue;
				key = xstrndup(str, val - str);
				val = xstrdup(val + 1);
				comments_add(&c, key, val);
				free(key);
			}
			keyvals_terminate(&c);
			priv->comments = c.keyvals;
		}
		break;
	default:
		d_print("something else\n");
		break;
	}
}

static void error_cb(const Dec *dec, FLAC__StreamDecoderErrorStatus status, void *data)
{
	d_print("FLAC error: %s\n", FLAC__StreamDecoderErrorStatusString[status]);
}

static void free_priv(struct input_plugin_data *ip_data)
{
	struct flac_private *priv = ip_data->private;
	int save = errno;

	F(finish)(priv->dec);
	F(delete)(priv->dec);
	if (priv->comments)
		keyvals_free(priv->comments);
	free(priv->buf);
	free(priv);
	ip_data->private = NULL;
	errno = save;
}

/* http://flac.sourceforge.net/format.html#frame_header */
static void channel_map_init_flac(int channels, channel_position_t *map)
{
	unsigned int mask = 0;
	if (channels == 4)
		mask = 0x33; // 0b110011, without center and lfe
	else if (channels == 5)
		mask = 0x37; // 0b110111, without lfe
	channel_map_init_waveex(channels, mask, map);
}

static int flac_open(struct input_plugin_data *ip_data)
{
	struct flac_private *priv;

	Dec *dec = F(new)();

	const struct flac_private priv_init = {
		.dec      = dec,
		.duration = -1,
		.bitrate  = -1,
		.bps      = 0
	};

	if (!dec)
		return -IP_ERROR_INTERNAL;

	priv = xnew(struct flac_private, 1);
	*priv = priv_init;
	if (ip_data->remote) {
		priv->len = UINT64_MAX;
	} else {
		off_t off = lseek(ip_data->fd, 0, SEEK_END);

		if (off == -1 || lseek(ip_data->fd, 0, SEEK_SET) == -1) {
			int save = errno;

			F(delete)(dec);
			free(priv);
			errno = save;
			return -IP_ERROR_ERRNO;
		}
		priv->len = off;
	}
	ip_data->private = priv;

	FLAC__stream_decoder_set_metadata_respond_all(dec);
	if (FLAC__stream_decoder_init_stream(dec, read_cb, seek_cb, tell_cb,
				length_cb, eof_cb, write_cb, metadata_cb,
				error_cb, ip_data) != E(INIT_STATUS_OK)) {
		int save = errno;

		d_print("init failed\n");
		F(delete)(priv->dec);
		free(priv);
		ip_data->private = NULL;
		errno = save;
		return -IP_ERROR_ERRNO;
	}

	ip_data->sf = 0;
	if (!F(process_until_end_of_metadata)(priv->dec)) {
		free_priv(ip_data);
		return -IP_ERROR_ERRNO;
	}

	if (!ip_data->sf) {
		free_priv(ip_data);
		return -IP_ERROR_FILE_FORMAT;
	}
	if (!sf_get_bits(ip_data->sf)) {
		free_priv(ip_data);
		return -IP_ERROR_SAMPLE_FORMAT;
	}

	channel_map_init_flac(sf_get_channels(ip_data->sf), ip_data->channel_map);
	d_print("sr: %d, ch: %d, bits: %d\n",
			sf_get_rate(ip_data->sf),
			sf_get_channels(ip_data->sf),
			sf_get_bits(ip_data->sf));
	return 0;
}

static int flac_close(struct input_plugin_data *ip_data)
{
	free_priv(ip_data);
	return 0;
}

static int flac_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	struct flac_private *priv = ip_data->private;
	int avail;

	while (1) {
		avail = priv->buf_wpos - priv->buf_rpos;
		BUG_ON(avail < 0);
		if (avail > 0)
			break;
		FLAC__bool internal_error = !F(process_single)(priv->dec);
		FLAC__StreamDecoderState state = F(get_state)(priv->dec);
		if (state == E(END_OF_STREAM))
			return 0;
		if (state == E(ABORTED) || state == E(OGG_ERROR) || internal_error) {
			d_print("process_single failed\n");
			return -1;
		}
	}
	if (count > avail)
		count = avail;
	memcpy(buffer, priv->buf + priv->buf_rpos, count);
	priv->buf_rpos += count;
	BUG_ON(priv->buf_rpos > priv->buf_wpos);
	if (priv->buf_rpos == priv->buf_wpos) {
		priv->buf_rpos = 0;
		priv->buf_wpos = 0;
	}
	return count;
}

/* Flush the input and seek to an absolute sample. Decoding will resume at the
 * given sample.
 */
static int flac_seek(struct input_plugin_data *ip_data, double offset)
{
	struct flac_private *priv = ip_data->private;
	priv->buf_rpos = 0;
	priv->buf_wpos = 0;
	uint64_t sample;

	sample = (uint64_t)(offset * (double)sf_get_rate(ip_data->sf) + 0.5);
	if (!F(seek_absolute)(priv->dec, sample)) {
		if (F(get_state(priv->dec)) == FLAC__STREAM_DECODER_SEEK_ERROR) {
			if (!F(flush)(priv->dec))
				d_print("failed to flush\n");
		}
		return -IP_ERROR_ERRNO;
	}
	return 0;
}

static int flac_read_comments(struct input_plugin_data *ip_data, struct keyval **comments)
{
	struct flac_private *priv = ip_data->private;

	if (priv->comments) {
		*comments = keyvals_dup(priv->comments);
	} else {
		*comments = keyvals_new(0);
	}
	return 0;
}

static int flac_duration(struct input_plugin_data *ip_data)
{
	struct flac_private *priv = ip_data->private;

	return priv->duration;
}

static long flac_bitrate(struct input_plugin_data *ip_data)
{
	struct flac_private *priv = ip_data->private;
	return priv->bitrate;
}

static char *flac_codec(struct input_plugin_data *ip_data)
{
	return xstrdup("flac");
}

static char *flac_codec_profile(struct input_plugin_data *ip_data)
{
	/* maybe identify compression-level over min/max blocksize/framesize */
	return NULL;
}

const struct input_plugin_ops ip_ops = {
	.open = flac_open,
	.close = flac_close,
	.read = flac_read,
	.seek = flac_seek,
	.read_comments = flac_read_comments,
	.duration = flac_duration,
	.bitrate = flac_bitrate,
	.bitrate_current = flac_bitrate,
	.codec = flac_codec,
	.codec_profile = flac_codec_profile
};

const int ip_priority = 50;
const char * const ip_extensions[] = { "flac", "fla", NULL };
const char * const ip_mime_types[] = { NULL };
const struct input_plugin_opt ip_options[] = { { NULL } };
const unsigned ip_abi_version = IP_ABI_VERSION;
