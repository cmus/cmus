/*
 * Copyright 2006 Chun-Yu Shei <cshei AT cs.indiana.edu>
 *
 * Cleaned up by Timo Hirvonen <tihirvon@gmail.com>
 */

#include "ip.h"
#include "ape.h"
#include "comment.h"
#include "file.h"
#include "xmalloc.h"
#include "read_wrapper.h"

#include "config/mpc.h"

#if MPC_SV8
#include <mpc/mpcdec.h>
#define callback_t mpc_reader
#define get_ip_data(d) (d)->data
#else
#include <mpcdec/mpcdec.h>
#define MPC_FALSE FALSE
#define MPC_TRUE TRUE
#define callback_t void
#define get_ip_data(d) (d)
#endif

#include <inttypes.h>
#include <errno.h>

struct mpc_private {
#if MPC_SV8
	mpc_demux *decoder;
#else
	mpc_decoder decoder;
#endif
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

/* callbacks */
static mpc_int32_t read_impl(callback_t *data, void *ptr, mpc_int32_t size)
{
	struct input_plugin_data *ip_data = get_ip_data(data);
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

static mpc_bool_t seek_impl(callback_t *data, mpc_int32_t offset)
{
	struct input_plugin_data *ip_data = get_ip_data(data);
	int rc;

	rc = lseek(ip_data->fd, offset, SEEK_SET);
	if (rc == -1)
		return MPC_FALSE;
	return MPC_TRUE;
}

static mpc_int32_t tell_impl(callback_t *data)
{
	struct input_plugin_data *ip_data = get_ip_data(data);

	return lseek(ip_data->fd, 0, SEEK_CUR);
}

static mpc_int32_t get_size_impl(callback_t *data)
{
	struct input_plugin_data *ip_data = get_ip_data(data);
	struct mpc_private *priv = ip_data->private;

	return priv->file_size;
}

static mpc_bool_t canseek_impl(callback_t *data)
{
	struct input_plugin_data *ip_data = get_ip_data(data);

	return !ip_data->remote;
}

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
#if MPC_SV8
	priv->decoder = mpc_demux_init(&priv->reader);
	if (!priv->decoder) {
#else
	mpc_streaminfo_init(&priv->info);
	if (mpc_streaminfo_read(&priv->info, &priv->reader) != ERROR_CODE_OK) {
#endif
		free(priv);
		return -IP_ERROR_FILE_FORMAT;
	}

#if MPC_SV8
	mpc_demux_get_info(priv->decoder, &priv->info);
#else
	/* instantiate a decoder with our file reader */
	mpc_decoder_setup(&priv->decoder, &priv->reader);
	if (!mpc_decoder_initialize(&priv->decoder, &priv->info)) {
		free(priv);
		return -IP_ERROR_FILE_FORMAT;
	}
#endif

	priv->samples_avail = 0;
	priv->samples_pos = 0;

	ip_data->sf = sf_rate(priv->info.sample_freq) | sf_channels(priv->info.channels) |
		sf_bits(16) | sf_signed(1);
	return 0;
}

static int mpc_close(struct input_plugin_data *ip_data)
{
	struct mpc_private *priv = ip_data->private;

#if MPC_SV8
	mpc_demux_exit(priv->decoder);
#endif
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

#if MPC_SV8
	mpc_status status;
	mpc_frame_info frame;
	int samples;

	frame.buffer = priv->samples;

	while (priv->samples_avail == 0) {
		status = mpc_demux_decode(priv->decoder, &frame);

		if (status != MPC_STATUS_OK) {
			return -IP_ERROR_ERRNO;
		}
		if (frame.bits == -1) {
			/* EOF */
			return 0;
		}

		samples = frame.samples;
		priv->samples_avail = samples * priv->info.channels;
	}
#else

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
#endif

	return scale(ip_data, buffer, count);
}

static int mpc_seek(struct input_plugin_data *ip_data, double offset)
{
	struct mpc_private *priv = ip_data->private;

	priv->samples_pos = 0;
	priv->samples_avail = 0;

#if MPC_SV8
	if (mpc_demux_seek_second(priv->decoder, offset) == MPC_STATUS_OK)
#else
	if (mpc_decoder_seek_seconds(&priv->decoder, offset))
#endif
		return 0;
	return -1;
}

static const char *gain_to_str(int gain)
{
	static char buf[16];
#if MPC_SV8
	float g = MPC_OLD_GAIN_REF - gain / 256.f;
	sprintf(buf, "%.2f", g);
#else
	int b, a = gain / 100;

	if (gain < 0) {
		b = -gain % 100;
	} else {
		b = gain % 100;
	}
	sprintf(buf, "%d.%02d", a, b);
#endif
	return buf;
}

static const char *peak_to_str(unsigned int peak)
{
	static char buf[16];
#if MPC_SV8
	sprintf(buf, "%.5f", peak / 256.f / 100.f);
#else
	sprintf(buf, "%d.%05d", peak / 32767, peak % 32767);
#endif
	return buf;
}

static int mpc_read_comments(struct input_plugin_data *ip_data, struct keyval **comments)
{
	struct mpc_private *priv = ip_data->private;
	GROWING_KEYVALS(c);
	int count, i;
	APETAG(ape);

	count = ape_read_tags(&ape, ip_data->fd, 1);
	if (count < 0)
		goto out;

	for (i = 0; i < count; i++) {
		char *k, *v;
		k = ape_get_comment(&ape, &v);
		if (!k)
			break;
		comments_add(&c, k, v);
		free(k);
	}

out:
	if (priv->info.gain_title && priv->info.peak_title) {
		comments_add_const(&c, "replaygain_track_gain", gain_to_str(priv->info.gain_title));
		comments_add_const(&c, "replaygain_track_peak", peak_to_str(priv->info.peak_title));
	}
	if (priv->info.gain_album && priv->info.peak_album) {
		comments_add_const(&c, "replaygain_album_gain", gain_to_str(priv->info.gain_album));
		comments_add_const(&c, "replaygain_album_peak", peak_to_str(priv->info.peak_album));
	}
	keyvals_terminate(&c);

	*comments = c.keyvals;
	ape_free(&ape);
	return 0;
}

static int mpc_duration(struct input_plugin_data *ip_data)
{
	struct mpc_private *priv = ip_data->private;

	/* priv->info.pcm_samples seems to be number of frames
	 * priv->info.frames is _not_ pcm frames
	 */
#if MPC_SV8
	return mpc_streaminfo_get_length(&priv->info);
#else
	return priv->info.pcm_samples / priv->info.sample_freq;
#endif
}

static long mpc_bitrate(struct input_plugin_data *ip_data)
{
	struct mpc_private *priv = ip_data->private;
	if (priv->info.average_bitrate)
		return (long) (priv->info.average_bitrate + 0.5);
	if (priv->info.bitrate)
		return priv->info.bitrate;
	return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
}

static char *mpc_codec(struct input_plugin_data *ip_data)
{
	struct mpc_private *priv = ip_data->private;
	switch (priv->info.stream_version) {
	case 7:
		return xstrdup("mpc7");
	case 8:
		return xstrdup("mpc8");
	}
	return NULL;
}

const struct input_plugin_ops ip_ops = {
	.open = mpc_open,
	.close = mpc_close,
	.read = mpc_read,
	.seek = mpc_seek,
	.read_comments = mpc_read_comments,
	.duration = mpc_duration,
	.bitrate = mpc_bitrate,
	.codec = mpc_codec
};

const int ip_priority = 50;
const char *const ip_extensions[] = { "mpc", "mpp", "mp+", NULL };
const char *const ip_mime_types[] = { "audio/x-musepack", NULL };
