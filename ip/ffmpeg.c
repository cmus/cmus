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

#include "../ip.h"
#include "../xmalloc.h"
#include "../debug.h"
#include "../utils.h"
#include "../comment.h"
#ifdef HAVE_CONFIG
#include "../config/ffmpeg.h"
#endif

#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#ifndef AVUTIL_MATHEMATICS_H
#include <libavutil/mathematics.h>
#endif

#ifndef AVCODEC_MAX_AUDIO_FRAME_SIZE
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000
#endif

struct ffmpeg_input {
	AVPacket pkt;
	int curr_pkt_size;
	uint8_t *curr_pkt_buf;
	int stream_index;

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
#if LIBAVCODEC_VERSION_MAJOR >= 56
	av_packet_unref(&input->pkt);
#else
	av_free_packet(&input->pkt);
#endif
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

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 18, 100)
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
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
	AVCodecParameters *cp = NULL;
#endif
	SwrContext *swr = NULL;

	ffmpeg_init();

	err = avformat_open_input(&ic, ip_data->filename, NULL, NULL);
	if (err < 0) {
		d_print("av_open failed: %d\n", err);
		return -IP_ERROR_FILE_FORMAT;
	}

	do {
		err = avformat_find_stream_info(ic, NULL);
		if (err < 0) {
			d_print("unable to find stream info: %d\n", err);
			err = -IP_ERROR_FILE_FORMAT;
			break;
		}

		for (i = 0; i < ic->nb_streams; i++) {

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
			cp = ic->streams[i]->codecpar;
			if (cp->codec_type == AVMEDIA_TYPE_AUDIO) {
				stream_index = i;
				break;
			}
#else
			cc = ic->streams[i]->codec;
			if (cc->codec_type == AVMEDIA_TYPE_AUDIO) {
				stream_index = i;
				break;
			}
#endif
		}

		if (stream_index == -1) {
			d_print("could not find audio stream\n");
			err = -IP_ERROR_FILE_FORMAT;
			break;
		}

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
		codec = avcodec_find_decoder(cp->codec_id);
		cc = avcodec_alloc_context3(codec);
		avcodec_parameters_to_context(cc, cp);
#else
		codec = avcodec_find_decoder(cc->codec_id);
#endif
		if (!codec) {
			d_print("codec not found: %d, %s\n", cc->codec_id, avcodec_get_name(cc->codec_id));
			err = -IP_ERROR_UNSUPPORTED_FILE_TYPE;
			break;
		}

		if (codec->capabilities & AV_CODEC_CAP_TRUNCATED)
			cc->flags |= AV_CODEC_FLAG_TRUNCATED;

		if (avcodec_open2(cc, codec, NULL) < 0) {
			d_print("could not open codec: %d, %s\n", cc->codec_id, avcodec_get_name(cc->codec_id));
			err = -IP_ERROR_UNSUPPORTED_FILE_TYPE;
			break;
		}

		/* We assume below that no more errors follow. */
	} while (0);

	if (err < 0) {
		/* Clean up.  cc is never opened at this point.  (See above assumption.) */
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
		avcodec_free_context(&cc);
#endif
		avformat_close_input(&ic);
		return err;
	}

	priv = xnew(struct ffmpeg_private, 1);
	priv->codec_context = cc;
	priv->input_context = ic;
	priv->codec = codec;
	priv->input = ffmpeg_input_create();
	if (priv->input == NULL) {
		avcodec_close(cc);
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
		avcodec_free_context(&cc);
#endif
		avformat_close_input(&ic);
		free(priv);
		return -IP_ERROR_INTERNAL;
	}
	priv->input->stream_index = stream_index;
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
	ip_data->sf |= sf_host_endian();
	channel_layout = cc->channel_layout;
	channel_map_init_waveex(cc->channels, channel_layout, ip_data->channel_map);
	return 0;
}

static int ffmpeg_close(struct input_plugin_data *ip_data)
{
	struct ffmpeg_private *priv = ip_data->private;

	avcodec_close(priv->codec_context);
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
	avcodec_free_context(&priv->codec_context);
#endif
	avformat_close_input(&priv->input_context);
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
#if LIBAVCODEC_VERSION_MAJOR >= 56
	AVFrame *frame = av_frame_alloc();
#else
	AVFrame *frame = avcodec_alloc_frame();
#endif
	int got_frame;
	while (1) {
		int len;

		if (input->curr_pkt_size <= 0) {
#if LIBAVCODEC_VERSION_MAJOR >= 56
			av_packet_unref(&input->pkt);
#else
			av_free_packet(&input->pkt);
#endif
			if (av_read_frame(ic, &input->pkt) < 0) {
				/* Force EOF once we can read no longer. */
#if LIBAVCODEC_VERSION_MAJOR >= 56
				av_frame_free(&frame);
#else
				avcodec_free_frame(&frame);
#endif
				return 0;
			}
			if (input->pkt.stream_index == input->stream_index) {
				input->curr_pkt_size = input->pkt.size;
				input->curr_pkt_buf = input->pkt.data;
				input->curr_size += input->pkt.size;
				input->curr_duration += input->pkt.duration;
			}
			continue;
		}

		{
			AVPacket avpkt;
			av_new_packet(&avpkt, input->curr_pkt_size);
			memcpy(avpkt.data, input->curr_pkt_buf, input->curr_pkt_size);
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
			if (avcodec_send_packet(cc, &avpkt) == 0) {
				got_frame = !avcodec_receive_frame(cc, frame);
				if (got_frame)
					len = input->curr_pkt_size;
				else
					len = 0;
			} else {
				got_frame = 0;
				len = 0;
			}
#else
			len = avcodec_decode_audio4(cc, frame, &got_frame, &avpkt);
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 56
			av_packet_unref(&avpkt);
#else
			av_free_packet(&avpkt);
#endif
		}
		if (len < 0) {
			/* this is often reached when seeking, not sure why */
			input->curr_pkt_size = 0;
			continue;
		}
		input->curr_pkt_size -= len;
		input->curr_pkt_buf += len;
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
#if LIBAVCODEC_VERSION_MAJOR >= 56
			av_frame_free(&frame);
#else
			avcodec_free_frame(&frame);
#endif
			return output->buffer_used_len;
		}
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
	out_size = min_i(output->buffer_used_len, count);
	memcpy(buffer, output->buffer_pos, out_size);
	output->buffer_used_len -= out_size;
	output->buffer_pos += out_size;
	return out_size;
}

static int ffmpeg_seek(struct input_plugin_data *ip_data, double offset)
{
	struct ffmpeg_private *priv = ip_data->private;
	AVStream *st = priv->input_context->streams[priv->input->stream_index];
	int ret;

	int64_t pts = av_rescale_q(offset * AV_TIME_BASE, AV_TIME_BASE_Q, st->time_base);

	avcodec_flush_buffers(priv->codec_context);
	/* Force reading a new packet in next ffmpeg_fill_buffer(). */
	priv->input->curr_pkt_size = 0;

	ret = av_seek_frame(priv->input_context, priv->input->stream_index, pts, 0);

	if (ret < 0) {
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
	} else {
		ffmpeg_buffer_flush(priv->output);
		return 0;
	}
}

static void ffmpeg_read_metadata(struct growing_keyvals *c, AVDictionary *metadata)
{
	AVDictionaryEntry *tag = NULL;

	while ((tag = av_dict_get(metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
		if (tag->value[0])
			comments_add_const(c, tag->key, tag->value);
	}
}

static int ffmpeg_read_comments(struct input_plugin_data *ip_data, struct keyval **comments)
{
	struct ffmpeg_private *priv = ip_data->private;
	AVFormatContext *ic = priv->input_context;

	GROWING_KEYVALS(c);

	ffmpeg_read_metadata(&c, ic->metadata);
	for (unsigned i = 0; i < ic->nb_streams; i++) {
		ffmpeg_read_metadata(&c, ic->streams[i]->metadata);
	}

	keyvals_terminate(&c);
	*comments = c.keyvals;

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
	AVStream *st = priv->input_context->streams[priv->input->stream_index];
	long bitrate = -1;
	/* ape codec returns silly numbers */
#if LIBAVCODEC_VERSION_MAJOR >= 55
	if (priv->codec->id == AV_CODEC_ID_APE)
#else
	if (priv->codec->id == CODEC_ID_APE)
#endif
		return -1;
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

static char *ffmpeg_codec_profile(struct input_plugin_data *ip_data)
{
	struct ffmpeg_private *priv = ip_data->private;
	const char *profile;
	profile = av_get_profile_name(priv->codec, priv->codec_context->profile);
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
	"aa", "aac", "ac3", "aif", "aifc", "aiff", "ape", "au", "fla", "flac",
	"m4a", "m4b", "mka", "mkv", "mp+", "mp2", "mp3", "mp4", "mpc", "mpp",
	"ogg", "opus", "shn", "tak", "tta", "wav", "webm", "wma", "wv",
#ifdef USE_FALLBACK_IP
	"*",
#endif
	NULL
};
const char *const ip_mime_types[] = { NULL };
const struct input_plugin_opt ip_options[] = { { NULL } };
const unsigned ip_abi_version = IP_ABI_VERSION;
