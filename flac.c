#include "ip.h"
#include "comment.h"
#include "xmalloc.h"
#include "debug.h"

#include <FLAC/export.h>

#ifdef FLAC_API_VERSION_CURRENT
/* flac 1.1.3 */
#define FLAC_NEW_API 1
#endif

#ifdef FLAC_NEW_API
#include <FLAC/stream_decoder.h>
#else
#include <FLAC/seekable_stream_decoder.h>
#endif

#include <FLAC/metadata.h>
#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#ifndef UINT64_MAX
#define UINT64_MAX ((uint64_t)-1)
#endif

/* Reduce typing.  Namespaces are nice but FLAC API is fscking ridiculous.  */

/* functions, types, enums */
#ifdef FLAC_NEW_API
#define F(s) FLAC__stream_decoder_ ## s
#define T(s) FLAC__StreamDecoder ## s
#define Dec FLAC__StreamDecoder
#define E(s) FLAC__STREAM_DECODER_ ## s
#else
#define F(s) FLAC__seekable_stream_decoder_ ## s
#define T(s) FLAC__SeekableStreamDecoder ## s
#define Dec FLAC__SeekableStreamDecoder
#define E(s) FLAC__SEEKABLE_STREAM_DECODER_ ## s
#endif

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
	int duration;

	unsigned int ignore_next_write : 1;
};

#ifdef FLAC_NEW_API
static T(ReadStatus) read_cb(const Dec *dec, unsigned char *buf, size_t *size, void *data)
#else
static T(ReadStatus) read_cb(const Dec *dec, unsigned char *buf, unsigned *size, void *data)
#endif
{
	struct input_plugin_data *ip_data = data;
	struct flac_private *priv = ip_data->private;
	int rc;

	if (priv->pos == priv->len) {
		*size = 0;
#ifdef FLAC_NEW_API
		return E(READ_STATUS_END_OF_STREAM);
#else
		return E(READ_STATUS_OK);
#endif
	}
	if (*size == 0)
#ifdef FLAC_NEW_API
		return E(READ_STATUS_CONTINUE);
#else
		return E(READ_STATUS_OK);
#endif

	rc = read(ip_data->fd, buf, *size);
	if (rc == -1) {
		*size = 0;
		if (errno == EINTR || errno == EAGAIN) {
			/* FIXME: not sure how the flac decoder handles this */
			d_print("interrupted\n");
#ifdef FLAC_NEW_API
			return E(READ_STATUS_CONTINUE);
#else
			return E(READ_STATUS_OK);
#endif
		}
#ifdef FLAC_NEW_API
		return E(READ_STATUS_ABORT);
#else
		return E(READ_STATUS_ERROR);
#endif
	}

	priv->pos += rc;
	*size = rc;
	if (rc == 0) {
		/* should not happen */
#ifdef FLAC_NEW_API
		return E(READ_STATUS_END_OF_STREAM);
#else
		return E(READ_STATUS_OK);
#endif
	}
#ifdef FLAC_NEW_API
	return E(READ_STATUS_CONTINUE);
#else
	return E(READ_STATUS_OK);
#endif
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

static inline uint16_t LE16(uint16_t x)
{
	return (x >> 8) | (x << 8);
}

static inline uint32_t LE32(uint32_t x)
{
	uint32_t x3 = x << 24;
	uint32_t x0 = x >> 24;
	uint32_t x2 = (x & 0xff00) << 8;
	uint32_t x1 = (x >> 8) & 0xff00;
	return x3 | x2 | x1 | x0;
}

#else

#define LE16(x)	(x)
#define LE32(x)	(x)

#endif

static FLAC__StreamDecoderWriteStatus write_cb(const Dec *dec, const FLAC__Frame *frame,
		const int32_t * const *buf, void *data)
{
	struct input_plugin_data *ip_data = data;
	struct flac_private *priv = ip_data->private;
	int frames, bytes, size, channels, bits, depth;
	int ch, nch, i, j = 0;

	if (ip_data->sf == 0) {
		return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
	}

	if (priv->ignore_next_write) {
		priv->ignore_next_write = 0;
		return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
	}

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
		depth = bits;
	nch = frame->header.channels;
	if (depth == 8) {
		char *b = priv->buf + priv->buf_wpos;

		for (i = 0; i < frames; i++) {
			for (ch = 0; ch < channels; ch++)
				b[j++] = buf[ch % nch][i];
		}
	} else if (depth == 16) {
		int16_t *b = (int16_t *)(priv->buf + priv->buf_wpos);

		for (i = 0; i < frames; i++) {
			for (ch = 0; ch < channels; ch++)
				b[j++] = LE16(buf[ch % nch][i]);
		}
	} else if (depth == 32) {
		int32_t *b = (int32_t *)(priv->buf + priv->buf_wpos);

		for (i = 0; i < frames; i++) {
			for (ch = 0; ch < channels; ch++)
				b[j++] = LE32(buf[ch % nch][i]);
		}
	} else if (depth == 12) { /* -> 16 */
		int16_t *b = (int16_t *)(priv->buf + priv->buf_wpos);

		for (i = 0; i < frames; i++) {
			for (ch = 0; ch < channels; ch++)
				b[j++] = LE16(buf[ch % nch][i] << 4);
		}
	} else if (depth == 20) { /* -> 32 */
		int32_t *b = (int32_t *)(priv->buf + priv->buf_wpos);

		for (i = 0; i < frames; i++) {
			for (ch = 0; ch < channels; ch++)
				b[j++] = LE32(buf[ch % nch][i] << 12);
		}
	} else if (depth == 24) { /* -> 32 */
		int32_t *b = (int32_t *)(priv->buf + priv->buf_wpos);

		for (i = 0; i < frames; i++) {
			for (ch = 0; ch < channels; ch++)
				b[j++] = LE32(buf[ch % nch][i] << 8);
		}
	} else {
		d_print("bits per sample changed to %d\n", depth);
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

			switch (si->bits_per_sample) {
			case 8:
			case 16:
			case 32:
				bits = si->bits_per_sample;
				break;
			case 12:
				bits = 16;
				break;
			case 20:
			case 24:
				bits = 32;
				break;
			}

			ip_data->sf = sf_rate(si->sample_rate) |
				sf_bits(bits) |
				sf_signed(1) |
				sf_channels(si->channels);
			if (!ip_data->remote && si->total_samples)
				priv->duration = si->total_samples / si->sample_rate;
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
				const char *str = metadata->data.vorbis_comment.comments[i].entry;
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

static int flac_open(struct input_plugin_data *ip_data)
{
	struct flac_private *priv;
	Dec *dec;

	dec = F(new)();
	if (dec == NULL)
		return -IP_ERROR_INTERNAL;

	priv = xnew0(struct flac_private, 1);
	priv->dec = dec;
	priv->duration = -1;
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

#ifndef FLAC_NEW_API
	F(set_read_callback)(dec, read_cb);
	F(set_seek_callback)(dec, seek_cb);
	F(set_tell_callback)(dec, tell_cb);
	F(set_length_callback)(dec, length_cb);
	F(set_eof_callback)(dec, eof_cb);
	F(set_write_callback)(dec, write_cb);
	F(set_metadata_callback)(dec, metadata_cb);
	F(set_error_callback)(dec, error_cb);
	F(set_client_data)(dec, ip_data);
#endif

#ifdef FLAC_NEW_API
	FLAC__stream_decoder_set_metadata_respond_all(dec);
	if (FLAC__stream_decoder_init_stream(dec, read_cb, seek_cb, tell_cb,
				length_cb, eof_cb, write_cb, metadata_cb,
				error_cb, ip_data) != E(INIT_STATUS_OK)) {
#else
	/* FLAC__METADATA_TYPE_STREAMINFO already accepted */
	F(set_metadata_respond)(dec, FLAC__METADATA_TYPE_VORBIS_COMMENT);

	if (F(init)(dec) != E(OK)) {
#endif
		int save = errno;

		d_print("init failed\n");
		F(delete)(priv->dec);
		free(priv);
		ip_data->private = NULL;
		errno = save;
		return -IP_ERROR_ERRNO;
	}

	ip_data->sf = 0;
	while (priv->buf_wpos == 0 && priv->pos < priv->len) {
		if (!F(process_single)(priv->dec)) {
			free_priv(ip_data);
			return -IP_ERROR_ERRNO;
		}
	}

	if (!ip_data->sf) {
		free_priv(ip_data);
		return -IP_ERROR_FILE_FORMAT;
	}
	if (!sf_get_bits(ip_data->sf)) {
		free_priv(ip_data);
		return -IP_ERROR_SAMPLE_FORMAT;
	}

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
		if (priv->pos == priv->len)
			return 0;
		if (!F(process_single)(priv->dec)) {
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
 * given sample. Note that because of this, the next write callback may contain
 * a partial block.
 */
static int flac_seek(struct input_plugin_data *ip_data, double offset)
{
	struct flac_private *priv = ip_data->private;
	uint64_t sample;

	sample = (uint64_t)(offset * (double)sf_get_rate(ip_data->sf) + 0.5);
	if (!F(seek_absolute)(priv->dec, sample)) {
		return -IP_ERROR_ERRNO;
	}
	priv->ignore_next_write = 1;
	priv->buf_rpos = 0;
	priv->buf_wpos = 0;
	return 0;
}

static int flac_read_comments(struct input_plugin_data *ip_data, struct keyval **comments)
{
	struct flac_private *priv = ip_data->private;

	if (priv->comments) {
		*comments = keyvals_dup(priv->comments);
	} else {
		*comments = xnew0(struct keyval, 1);
	}
	return 0;
}

static int flac_duration(struct input_plugin_data *ip_data)
{
	struct flac_private *priv = ip_data->private;

	return priv->duration;
}

const struct input_plugin_ops ip_ops = {
	.open = flac_open,
	.close = flac_close,
	.read = flac_read,
	.seek = flac_seek,
	.read_comments = flac_read_comments,
	.duration = flac_duration
};

const char * const ip_extensions[] = { "flac", "fla", NULL };
const char * const ip_mime_types[] = { NULL };
