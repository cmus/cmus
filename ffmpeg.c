/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2007 Kevin Ko <kevin.s.ko@gmail.com>
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

#include "ip.h"
#include "xmalloc.h"
#include "debug.h"
#include "utils.h"
#include "comment.h"
#ifdef HAVE_CONFIG
#include "config/ffmpeg.h"
#endif

#include <stdio.h>
#ifdef HAVE_FFMPEG_AVCODEC_H
#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
#include <ffmpeg/avio.h>
#include <ffmpeg/swresample.h>
#include <ffmpeg/opt.h>
#include <ffmpeg/audioconvert.h>
#else
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/audioconvert.h>
#ifndef AVUTIL_MATHEMATICS_H
#include <libavutil/mathematics.h>
#endif
#endif

#if (LIBAVFORMAT_VERSION_INT < ((52<<16)+(31<<8)+0))
# define NUM_FFMPEG_KEYS 8
#endif

#if (LIBAVCODEC_VERSION_INT < ((52<<16)+(64<<8)+0))
# define AVMEDIA_TYPE_AUDIO CODEC_TYPE_AUDIO
#endif

#if (LIBAVCODEC_VERSION_INT < ((52<<16)+(94<<8)+1))
#define AV_SAMPLE_FMT_U8   SAMPLE_FMT_U8
#define AV_SAMPLE_FMT_S16  SAMPLE_FMT_S16
#define AV_SAMPLE_FMT_S32  SAMPLE_FMT_S32
#define AV_SAMPLE_FMT_FLT  SAMPLE_FMT_FLT
#if (LIBAVCODEC_VERSION_INT > ((51<<16)+(64<<8)+0))
#define AV_SAMPLE_FMT_DBL  SAMPLE_FMT_DBL
#endif
#endif

#if (LIBAVUTIL_VERSION_INT < ((51<<16)+(5<<8)+0))
#define AV_DICT_IGNORE_SUFFIX AV_METADATA_IGNORE_SUFFIX
#define av_dict_get av_metadata_get
#define AVDictionaryEntry AVMetadataTag
#endif

#ifndef AVCODEC_MAX_AUDIO_FRAME_SIZE
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000
#endif

struct ffmpeg_input {
	AVPacket pkt;
	int curr_pkt_size;
	uint8_t *curr_pkt_buf;

	unsigned long curr_size;
	unsigned long curr_duration;
};

struct ffmpeg_output {
	uint8_t *buffer;
	uint8_t *buffer_malloc;
	uint8_t *buffer_pos;	/* current buffer position */
	int buffer_used_len;
};

struct ffmpeg_private {
	AVCodecContext *codec_context;
	AVFormatContext *input_context;
	AVCodec *codec;
	SwrContext *swr;
	int stream_index;

	struct ffmpeg_input *input;
	struct ffmpeg_output *output;
};

static struct ffmpeg_input *ffmpeg_input_create(void)
{
	struct ffmpeg_input *input = xnew(struct ffmpeg_input, 1);

	if (av_new_packet(&input->pkt, 0) != 0) {
		free(input);
		return NULL;
	}
	input->curr_pkt_size = 0;
	input->curr_pkt_buf = input->pkt.data;
	return input;
}

static void ffmpeg_input_free(struct ffmpeg_input *input)
{
	av_free_packet(&input->pkt);
	free(input);
}

static struct ffmpeg_output *ffmpeg_output_create(void)
{
	struct ffmpeg_output *output = xnew(struct ffmpeg_output, 1);

	output->buffer_malloc = xnew(uint8_t, AVCODEC_MAX_AUDIO_FRAME_SIZE + 15);
	output->buffer = output->buffer_malloc;
	/* align to 16 bytes so avcodec can SSE/Altivec/etc */
	while ((intptr_t) output->buffer % 16)
		output->buffer += 1;
	output->buffer_pos = output->buffer;
	output->buffer_used_len = 0;
	return output;
}

static void ffmpeg_output_free(struct ffmpeg_output *output)
{
	free(output->buffer_malloc);
	output->buffer_malloc = NULL;
	output->buffer = NULL;
	free(output);
}

static inline void ffmpeg_buffer_flush(struct ffmpeg_output *output)
{
	output->buffer_pos = output->buffer;
	output->buffer_used_len = 0;
}

static void ffmpeg_init(void)
{
	static int inited = 0;

	if (inited != 0)
		return;
	inited = 1;

	av_log_set_level(AV_LOG_QUIET);

#if (LIBAVFORMAT_VERSION_INT <= ((50<<16) + (4<<8) + 0))
	avcodec_init();
	register_avcodec(&wmav1_decoder);
	register_avcodec(&wmav2_decoder);

	/* libavformat versions <= 50.4.0 have asf_init().  From SVN revision
	 * 5697->5707 of asf.c, this function was removed, preferring the use of
	 * explicit calls.  Note that version 50.5.0 coincides with SVN revision
	 * 5729, so there is a window of incompatibility for revisions 5707 and 5720
	 * of asf.c.
	 */
	asf_init();

	/* Uncomment this for shorten (.shn) support.
	   register_avcodec(&shorten_decoder);
	   raw_init();
	 */

	register_protocol(&file_protocol);
#else
	/* We could register decoders explicitly to save memory, but we have to
	 * be careful about compatibility. */
	av_register_all();
#endif
}

static int ffmpeg_open(struct input_plugin_data *ip_data)
{
	struct ffmpeg_private *priv;
	int err = 0;
	int i;
	int stream_index = -1;
	int64_t channel_layout = 0;
	AVCodec *codec;
	AVCodecContext *cc = NULL;
	AVFormatContext *ic = NULL;
	SwrContext *swr = NULL;

	ffmpeg_init();

#if (LIBAVFORMAT_VERSION_INT <= ((53<<16)+(2<<8)+0))
	err = av_open_input_file(&ic, ip_data->filename, NULL, 0, NULL);
#else
	err = avformat_open_input(&ic, ip_data->filename, NULL, NULL);
#endif
	if (err < 0) {
		d_print("av_open failed: %d\n", err);
		return -IP_ERROR_FILE_FORMAT;
	}

	do {
#if (LIBAVFORMAT_VERSION_INT <= ((53<<16)+(5<<8)+0))
		err = av_find_stream_info(ic);
#else
		err = avformat_find_stream_info(ic, NULL);
#endif
		if (err < 0) {
			d_print("unable to find stream info: %d\n", err);
			err = -IP_ERROR_FILE_FORMAT;
			break;
		}

		for (i = 0; i < ic->nb_streams; i++) {
			cc = ic->streams[i]->codec;
			if (cc->codec_type == AVMEDIA_TYPE_AUDIO) {
				stream_index = i;
				break;
			}
		}

		if (stream_index == -1) {
			d_print("could not find audio stream\n");
			err = -IP_ERROR_FILE_FORMAT;
			break;
		}

		codec = avcodec_find_decoder(cc->codec_id);
		if (!codec) {
			d_print("codec not found: %d, %s\n", cc->codec_id, cc->codec_name);
			err = -IP_ERROR_UNSUPPORTED_FILE_TYPE;
			break;
		}

		if (codec->capabilities & CODEC_CAP_TRUNCATED)
			cc->flags |= CODEC_FLAG_TRUNCATED;

#if (LIBAVCODEC_VERSION_INT < ((53<<16)+(8<<8)+0))
		if (avcodec_open(cc, codec) < 0) {
#else
		if (avcodec_open2(cc, codec, NULL) < 0) {
#endif
			d_print("could not open codec: %d, %s\n", cc->codec_id, cc->codec_name);
			err = -IP_ERROR_UNSUPPORTED_FILE_TYPE;
			break;
		}

		/* We assume below that no more errors follow. */
	} while (0);

	if (err < 0) {
		/* Clean up.  cc is never opened at this point.  (See above assumption.) */
#if (LIBAVCODEC_VERSION_INT < ((53<<16)+(25<<8)+0))
		av_close_input_file(ic);
#else
		avformat_close_input(&ic);
#endif
		return err;
	}

	priv = xnew(struct ffmpeg_private, 1);
	priv->codec_context = cc;
	priv->input_context = ic;
	priv->codec = codec;
	priv->stream_index = stream_index;
	priv->input = ffmpeg_input_create();
	if (priv->input == NULL) {
		avcodec_close(cc);
#if (LIBAVCODEC_VERSION_INT < ((53<<16)+(25<<8)+0))
		av_close_input_file(ic);
#else
		avformat_close_input(&ic);
#endif
		free(priv);
		return -IP_ERROR_INTERNAL;
	}
	priv->output = ffmpeg_output_create();

	/* Prepare for resampling. */
	swr = swr_alloc();
	av_opt_set_int(swr, "in_channel_layout",  av_get_default_channel_layout(cc->channels), 0);
	av_opt_set_int(swr, "out_channel_layout", av_get_default_channel_layout(cc->channels), 0);
	av_opt_set_int(swr, "in_sample_rate",     cc->sample_rate, 0);
	av_opt_set_int(swr, "out_sample_rate",    cc->sample_rate, 0);
	av_opt_set_sample_fmt(swr, "in_sample_fmt",  cc->sample_fmt, 0);
	priv->swr = swr;

	ip_data->private = priv;
	ip_data->sf = sf_rate(cc->sample_rate) | sf_channels(cc->channels);
	switch (cc->sample_fmt) {
	case AV_SAMPLE_FMT_U8:
		ip_data->sf |= sf_bits(8) | sf_signed(0);
		av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_U8,  0);
		break;
	case AV_SAMPLE_FMT_S32:
		ip_data->sf |= sf_bits(32) | sf_signed(1);
		av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_S32,  0);
		break;
	/* AV_SAMPLE_FMT_S16 */
	default:
		ip_data->sf |= sf_bits(16) | sf_signed(1);
		av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_S16,  0);
		break;
	}
	swr_init(swr);
#ifdef WORDS_BIGENDIAN
	ip_data->sf |= sf_bigendian(1);
#endif
#if (LIBAVCODEC_VERSION_INT > ((52<<16)+(1<<8)+0))
	channel_layout = cc->channel_layout;
#endif
	channel_map_init_waveex(cc->channels, channel_layout, ip_data->channel_map);
	return 0;
}

static int ffmpeg_close(struct input_plugin_data *ip_data)
{
	struct ffmpeg_private *priv = ip_data->private;

	avcodec_close(priv->codec_context);
#if (LIBAVCODEC_VERSION_INT < ((53<<16)+(25<<8)+0))
	av_close_input_file(priv->input_context);
#else
	avformat_close_input(&priv->input_context);
#endif
	swr_free(&priv->swr);
	ffmpeg_input_free(priv->input);
	ffmpeg_output_free(priv->output);
	free(priv);
	ip_data->private = NULL;
	return 0;
}

/*
 * This returns the number of bytes added to the buffer.
 * It returns < 0 on error.  0 on EOF.
 */
static int ffmpeg_fill_buffer(AVFormatContext *ic, AVCodecContext *cc, struct ffmpeg_input *input,
			      struct ffmpeg_output *output, SwrContext *swr)
{
#if (LIBAVCODEC_VERSION_INT >= ((53<<16) + (25<<8) + 0))
	AVFrame *frame = avcodec_alloc_frame();
	int got_frame;
#endif
	while (1) {
#if (LIBAVCODEC_VERSION_INT < ((53<<16) + (25<<8) + 0))
		/* frame_size specifies the size of output->buffer for
		 * avcodec_decode_audio2. */
		int frame_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
#endif
		int len;

		if (input->curr_pkt_size <= 0) {
			av_free_packet(&input->pkt);
			if (av_read_frame(ic, &input->pkt) < 0) {
				/* Force EOF once we can read no longer. */
#if (LIBAVCODEC_VERSION_INT >= ((53<<16) + (25<<8) + 0))
				avcodec_free_frame(&frame);
#endif
				return 0;
			}
			input->curr_pkt_size = input->pkt.size;
			input->curr_pkt_buf = input->pkt.data;
			input->curr_size += input->pkt.size;
			input->curr_duration += input->pkt.duration;
			continue;
		}

		/* The change to avcodec_decode_audio2 occurred between
		 * 51.28.0 and 51.29.0 */
#if (LIBAVCODEC_VERSION_INT <= ((51<<16) + (28<<8) + 0))
		len = avcodec_decode_audio(cc, (int16_t *)output->buffer, &frame_size,
				input->curr_pkt_buf, input->curr_pkt_size);
		/* The change to avcodec_decode_audio3 occurred between
		 * 52.25.0 and 52.26.0 */
#elif (LIBAVCODEC_VERSION_INT <= ((52<<16) + (25<<8) + 0))
		len = avcodec_decode_audio2(cc, (int16_t *) output->buffer, &frame_size,
				input->curr_pkt_buf, input->curr_pkt_size);
#elif (LIBAVCODEC_VERSION_INT < ((53<<16) + (25<<8) + 0))
		{
			AVPacket avpkt;
			av_init_packet(&avpkt);
			avpkt.data = input->curr_pkt_buf;
			avpkt.size = input->curr_pkt_size;
			len = avcodec_decode_audio3(cc, (int16_t *) output->buffer, &frame_size, &avpkt);
			av_free_packet(&avpkt);
		}
#else
		{
			AVPacket avpkt;
			av_new_packet(&avpkt, input->curr_pkt_size);
			memcpy(avpkt.data, input->curr_pkt_buf, input->curr_pkt_size);
			len = avcodec_decode_audio4(cc, frame, &got_frame, &avpkt);
			av_free_packet(&avpkt);
		}
#endif
		if (len < 0) {
			/* this is often reached when seeking, not sure why */
			input->curr_pkt_size = 0;
			continue;
		}
		input->curr_pkt_size -= len;
		input->curr_pkt_buf += len;
#if (LIBAVCODEC_VERSION_INT < ((53<<16) + (25<<8) + 0))
		if (frame_size > 0) {
			output->buffer_pos = output->buffer;
			output->buffer_used_len = frame_size;
			return frame_size;
		}
#else
		if (got_frame) {
			int res = swr_convert(swr,
					&output->buffer,
					frame->nb_samples,
					(const uint8_t **)frame->extended_data,
					frame->nb_samples);
			if (res < 0)
				res = 0;
			output->buffer_pos = output->buffer;
			output->buffer_used_len = res * cc->channels * sizeof(int16_t);
			avcodec_free_frame(&frame);
			return output->buffer_used_len;
		}
#endif
	}
	/* This should never get here. */
	return -IP_ERROR_INTERNAL;
}

static int ffmpeg_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	struct ffmpeg_private *priv = ip_data->private;
	struct ffmpeg_output *output = priv->output;
	int rc;
	int out_size;

	if (output->buffer_used_len == 0) {
		rc = ffmpeg_fill_buffer(priv->input_context, priv->codec_context,
				priv->input, priv->output, priv->swr);
		if (rc <= 0) {
			return rc;
		}
	}
	out_size = min(output->buffer_used_len, count);
	memcpy(buffer, output->buffer_pos, out_size);
	output->buffer_used_len -= out_size;
	output->buffer_pos += out_size;
	return out_size;
}

static int ffmpeg_seek(struct input_plugin_data *ip_data, double offset)
{
	struct ffmpeg_private *priv = ip_data->private;
	AVStream *st = priv->input_context->streams[priv->stream_index];
	int ret;

	/* There is a bug that was fixed in ffmpeg revision 5099 that affects seeking.
	 * Apparently, the stream's timebase was not used consistently in asf.c.
	 * Prior to 5099, ASF seeking assumed seconds as inputs.  There is a
	 * window of incompatibility, since avformat's version was not updated at
	 * the same time.  Instead, the transition to 50.3.0 occurred at
	 * revision 5028. */
#if (LIBAVFORMAT_VERSION_INT < ((50<<16)+(3<<8)+0))
	int64_t pts = (int64_t) offset;
#else
	int64_t pts = av_rescale_q(offset * AV_TIME_BASE, AV_TIME_BASE_Q, st->time_base);
#endif

#if (LIBAVFORMAT_VERSION_INT >= ((53<<16) + (25<<8) + 0))
	{
		avcodec_flush_buffers(priv->codec_context);
		/* Force reading a new packet in next ffmpeg_fill_buffer(). */
		priv->input->curr_pkt_size = 0;
	}
#endif

	ret = av_seek_frame(priv->input_context, priv->stream_index, pts, 0);

	if (ret < 0) {
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
	} else {
		ffmpeg_buffer_flush(priv->output);
		return 0;
	}
}

#if (LIBAVFORMAT_VERSION_INT < ((52<<16)+(31<<8)+0))
/* Return new i. */
static int set_comment(struct keyval *comment, int i, const char *key, const char *val)
{
	if (val[0] == 0) {
		return i;
	}
	comment[i].key = xstrdup(key);
	comment[i].val = xstrdup(val);
	return i + 1;
}
#endif

static int ffmpeg_read_comments(struct input_plugin_data *ip_data, struct keyval **comments)
{
	struct ffmpeg_private *priv = ip_data->private;
	AVFormatContext *ic = priv->input_context;

#if (LIBAVFORMAT_VERSION_INT < ((52<<16)+(31<<8)+0))
	char buff[16];
	int i = 0;

	*comments = keyvals_new(NUM_FFMPEG_KEYS);

	i = set_comment(*comments, i, "artist", ic->author);
	i = set_comment(*comments, i, "album", ic->album);
	i = set_comment(*comments, i, "title", ic->title);
	i = set_comment(*comments, i, "genre", ic->genre);

	if (ic->year != 0) {
		snprintf(buff, sizeof(buff), "%d", ic->year);
		i = set_comment(*comments, i, "date", buff);
	}

	if (ic->track != 0) {
		snprintf(buff, sizeof(buff), "%d", ic->track);
		i = set_comment(*comments, i, "tracknumber", buff);
	}
#else
	GROWING_KEYVALS(c);
	AVDictionaryEntry *tag = NULL;

	while ((tag = av_dict_get(ic->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
		if (tag && tag->value[0])
			comments_add_const(&c, tag->key, tag->value);
	}

	keyvals_terminate(&c);
	*comments = c.keyvals;
#endif

	return 0;
}

static int ffmpeg_duration(struct input_plugin_data *ip_data)
{
	struct ffmpeg_private *priv = ip_data->private;
	return priv->input_context->duration / AV_TIME_BASE;
}

static long ffmpeg_bitrate(struct input_plugin_data *ip_data)
{
	struct ffmpeg_private *priv = ip_data->private;
	long bitrate = priv->input_context->bit_rate;
	return bitrate ? bitrate : -IP_ERROR_FUNCTION_NOT_SUPPORTED;
}

static long ffmpeg_current_bitrate(struct input_plugin_data *ip_data)
{
	struct ffmpeg_private *priv = ip_data->private;
	AVStream *st = priv->input_context->streams[priv->stream_index];
	long bitrate = -1;
#if (LIBAVFORMAT_VERSION_INT > ((51<<16)+(43<<8)+0))
	/* ape codec returns silly numbers */
	if (priv->codec->id == CODEC_ID_APE)
		return -1;
#endif
	if (priv->input->curr_duration > 0) {
		double seconds = priv->input->curr_duration * av_q2d(st->time_base);
		bitrate = (8 * priv->input->curr_size) / seconds;
		priv->input->curr_size = 0;
		priv->input->curr_duration = 0;
	}
	return bitrate;
}

static char *ffmpeg_codec(struct input_plugin_data *ip_data)
{
	struct ffmpeg_private *priv = ip_data->private;
	return xstrdup(priv->codec->name);
}

#if (LIBAVCODEC_VERSION_INT < ((52<<16)+(104<<8)+0))
static const char *codec_profile_to_str(int profile)
{
#if (LIBAVCODEC_VERSION_INT >= ((51<<16)+(41<<8)+0))
	switch (profile) {
	case FF_PROFILE_AAC_MAIN:	return "Main";
	case FF_PROFILE_AAC_LOW:	return "LC";
	case FF_PROFILE_AAC_SSR:	return "SSR";
	case FF_PROFILE_AAC_LTP:	return "LTP";
	}
#endif
	return NULL;
}
#endif

static char *ffmpeg_codec_profile(struct input_plugin_data *ip_data)
{
	struct ffmpeg_private *priv = ip_data->private;
	const char *profile;

#if (LIBAVCODEC_VERSION_INT < ((52<<16)+(104<<8)+0))
	profile = codec_profile_to_str(priv->codec_context->profile);
#else
	profile = av_get_profile_name(priv->codec, priv->codec_context->profile);
#endif

	return profile ? xstrdup(profile) : NULL;
}

const struct input_plugin_ops ip_ops = {
	.open = ffmpeg_open,
	.close = ffmpeg_close,
	.read = ffmpeg_read,
	.seek = ffmpeg_seek,
	.read_comments = ffmpeg_read_comments,
	.duration = ffmpeg_duration,
	.bitrate = ffmpeg_bitrate,
	.bitrate_current = ffmpeg_current_bitrate,
	.codec = ffmpeg_codec,
	.codec_profile = ffmpeg_codec_profile
};

const int ip_priority = 30;
const char *const ip_extensions[] = {
	"ac3", "aif", "aifc", "aiff", "ape", "au", "mka", "shn", "tta", "wma",
	/* also supported by other plugins */
	"aac", "fla", "flac", "m4a", "m4b", "mp+", "mp2", "mp3", "mp4", "mpc",
	"mpp", "ogg", "wav", "wv",
#ifdef USE_FALLBACK_IP
	"*",
#endif
	NULL
};
const char *const ip_mime_types[] = { NULL };
const char * const ip_options[] = { NULL };
