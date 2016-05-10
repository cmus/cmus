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
#include <stdbool.h>

/* Minimum API versions:
 * avcodec:    54
 * avformat:   54
 * avutil:     51
 * swresample: 0
 */

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#if LIBAVUTIL_VERSION_MAJOR >= 53
#	include <libavutil/channel_layout.h>
#else
#	include <libavutil/audioconvert.h>
#endif
#ifndef AVUTIL_MATHEMATICS_H
#include <libavutil/mathematics.h>
#endif

#ifndef AVCODEC_MAX_AUDIO_FRAME_SIZE
#	define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 56
#	define ffmpeg_packet_unref(pkt) av_packet_unref(pkt)
#	define ffmpeg_frame_alloc()     av_frame_alloc()
#	define ffmpeg_frame_free(frame) av_frame_free(frame)
#	define ffmpeg_free_context(cc)  avcodec_free_context(cc)
#else
#	define ffmpeg_packet_unref(pkt) av_free_packet(pkt)
#	define ffmpeg_frame_alloc()     avcodec_alloc_frame()
#	define ffmpeg_frame_free(frame) avcodec_free_frame(frame)
#	define ffmpeg_free_context(cc)  do { avcodec_close(*cc); av_freep(cc); } while (0)
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 40, 100)
#	define ffmpeg_initialize_context(cc, stream) \
		avcodec_copy_context(cc, stream->codec)
#else
#	define ffmpeg_initialize_context(cc, stream) \
		avcodec_parameters_to_context(cc, stream->codecpar)
#	define ffmpeg_receive_frame(...) avcodec_receive_frame(__VA_ARGS__)
#	define ffmpeg_send_packet(...) avcodec_send_packet(__VA_ARGS__)
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 55
#	define FFMPEG_CODEC_ID_APE AV_CODEC_ID_APE
#else
#	define FFMPEG_CODEC_ID_APE CODEC_ID_APE
#endif

struct ffmpeg_input {
	AVPacket pkt;
	int curr_pkt_size;
	uint8_t *curr_pkt_buf;
	int stream_index;
	bool eof;

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

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 40, 100)
static int ffmpeg_receive_frame(AVCodecContext *cc, AVFrame *frame)
{
	struct ffmpeg_private *private = cc->opaque;
	struct ffmpeg_input *input = private->input;

	int len;
	int got_frame;

 	if (input->curr_pkt_size <= 0) {
		return -1;
 	}
 
 	AVPacket avpkt;
 	av_new_packet(&avpkt, input->curr_pkt_size);
 	memcpy(avpkt.data, input->curr_pkt_buf, input->curr_pkt_size);
 	len = avcodec_decode_audio4(cc, frame, &got_frame, &avpkt);
 	ffmpeg_packet_unref(&avpkt);

	if (len < 0) {
		input->curr_pkt_size = 0;
		return -1;
	}

	input->curr_pkt_size -= len;
	input->curr_pkt_buf += len;

	return !got_frame;
}

static int ffmpeg_send_packet(AVCodecContext *cc, AVPacket *pkt)
{
	struct ffmpeg_private *private = cc->opaque;
	struct ffmpeg_input *input = private->input;

	if (pkt) {
		input->curr_pkt_size = pkt->size;
		input->curr_pkt_buf = pkt->data;
		input->curr_size += pkt->size;
		input->curr_duration += pkt->duration;
	}

	return 0;
}
#endif

static struct ffmpeg_input *ffmpeg_input_create(void)
{
	struct ffmpeg_input *input = xnew(struct ffmpeg_input, 1);

	if (av_new_packet(&input->pkt, 0) != 0) {
		free(input);
		return NULL;
	}
	input->curr_pkt_size = 0;
	input->curr_pkt_buf = input->pkt.data;
	input->eof = false;
	return input;
}

static void ffmpeg_input_free(struct ffmpeg_input *input)
{
	ffmpeg_packet_unref(&input->pkt);
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

	/* We could register decoders explicitly to save memory, but we have to
	 * be careful about compatibility. */
	av_register_all();
}

static int ffmpeg_open(struct input_plugin_data *ip_data)
{
	struct ffmpeg_private *priv;
	int err = 0;
	int stream_index = -1;
	int64_t channel_layout = 0;
	AVCodec *codec;
	AVCodecContext *cc = NULL;
	AVFormatContext *ic = NULL;
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

		err = av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
		if (err < 0) {
			if (err == AVERROR_STREAM_NOT_FOUND) {
				d_print("could not find audio stream\n");
				err = -IP_ERROR_FILE_FORMAT;
			} else if (err == AVERROR_DECODER_NOT_FOUND) {
				d_print("codec not found\n");
				err = -IP_ERROR_UNSUPPORTED_FILE_TYPE;
			} else {
				err = -IP_ERROR_INTERNAL;
			}
			break;
		}
		stream_index = err;

		cc = avcodec_alloc_context3(codec);
		if (!cc) {
			d_print("could not allocate decodec context\n");
			err = -IP_ERROR_INTERNAL;
			break;
		}
		if (ffmpeg_initialize_context(cc, ic->streams[stream_index]) < 0) {
			d_print("could not initialize decodec context\n");
			err = -IP_ERROR_INTERNAL;
			break;
		}

		if (codec->capabilities & CODEC_CAP_TRUNCATED)
			cc->flags |= CODEC_FLAG_TRUNCATED;

		if (avcodec_open2(cc, codec, NULL) < 0) {
			d_print("could not open codec: %d, %s\n", cc->codec_id,
					avcodec_get_name(cc->codec_id));
			err = -IP_ERROR_UNSUPPORTED_FILE_TYPE;
			break;
		}
	} while (0);

	if (err < 0) {
		ffmpeg_free_context(&cc);
		avformat_close_input(&ic);
		return err;
	}

	priv = xnew(struct ffmpeg_private, 1);
	priv->codec_context = cc;
	priv->input_context = ic;
	priv->codec = codec;
	priv->input = ffmpeg_input_create();
	if (priv->input == NULL) {
		ffmpeg_free_context(&cc);
		avformat_close_input(&ic);
		free(priv);
		return -IP_ERROR_INTERNAL;
	}
	priv->input->stream_index = stream_index;
	priv->output = ffmpeg_output_create();

	cc->opaque = priv;

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
	channel_layout = cc->channel_layout;
	channel_map_init_waveex(cc->channels, channel_layout, ip_data->channel_map);
	return 0;
}

static int ffmpeg_close(struct input_plugin_data *ip_data)
{
	struct ffmpeg_private *priv = ip_data->private;

	ffmpeg_free_context(&priv->codec_context);
	avformat_close_input(&priv->input_context);
	swr_free(&priv->swr);
	ffmpeg_input_free(priv->input);
	ffmpeg_output_free(priv->output);
	free(priv);
	ip_data->private = NULL;
	return 0;
}

static int ffmpeg_fill_buffer(AVFormatContext *ic, AVCodecContext *cc,
		struct ffmpeg_input *input, struct ffmpeg_output *output, SwrContext *swr)
{
	AVFrame *frame = ffmpeg_frame_alloc();
	int res;

again:
	if (!ffmpeg_receive_frame(cc, frame)) {
		res = swr_convert(swr,
				  &output->buffer,
				  frame->nb_samples,
				  (const uint8_t **)frame->extended_data,
				  frame->nb_samples);
		if (res < 0) {
			res = -IP_ERROR_INTERNAL;
			goto out;
		}
		output->buffer_pos = output->buffer;
		output->buffer_used_len = res * cc->channels * sizeof(int16_t);
		res = output->buffer_used_len;
		goto out;
	}

	if (input->eof) {
		res = 0;
		goto out;
	}

	ffmpeg_packet_unref(&input->pkt);
	res = av_read_frame(ic, &input->pkt);

	if (res < 0) {
		if (res == AVERROR_EOF) {
			ffmpeg_send_packet(cc, NULL);
			input->eof = true;
			goto again;
		} else {
			res = 0;
			goto out;
		}
	}

	if (input->pkt.stream_index == input->stream_index) {
		if (ffmpeg_send_packet(cc, &input->pkt)) {
			res = -IP_ERROR_INTERNAL;
			goto out;
		}
	}

	goto again;

out:
	ffmpeg_frame_free(&frame);
	return res;
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

static int ffmpeg_read_comments(struct input_plugin_data *ip_data,
		struct keyval **comments)
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
	if (priv->codec->id == FFMPEG_CODEC_ID_APE)
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

const int ip_priority = 300;
const char *const ip_extensions[] = {
	"aa", "aac", "ac3", "aif", "aifc", "aiff", "ape", "au", "fla", "flac",
	"m4a", "m4b", "mka", "mkv", "mp+", "mp2", "mp3", "mp4", "mpc", "mpp",
	"ogg", "shn", "tak", "tta", "wav", "webm", "wma", "wv",
#ifdef USE_FALLBACK_IP
	"*",
#endif
	NULL
};
const char *const ip_mime_types[] = { NULL };
const char *const ip_options[] = { NULL };
