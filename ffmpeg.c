/*
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <stdio.h>

#ifdef HAVE_FFMPEG_AVCODEC_H
#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
#include <ffmpeg/avio.h>
#else
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#endif

#include "ip.h"
#include "xmalloc.h"
#include "debug.h"
#include "utils.h"
#include "config/ffmpeg.h"

#define NUM_FFMPEG_KEYS 8

struct ffmpeg_input {
	AVPacket pkt;
	int curr_pkt_size;
	uint8_t *curr_pkt_buf;
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
	AVCodec *codec;
	AVCodecContext *cc = NULL;
	AVFormatContext *ic;

	ffmpeg_init();

	err = av_open_input_file(&ic, ip_data->filename, NULL, 0, NULL);
	if (err < 0) {
		d_print("av_open failed: %d\n", err);
		return -IP_ERROR_FILE_FORMAT;
	}

	do {
		err = av_find_stream_info(ic);
		if (err < 0) {
			d_print("unable to find stream info: %d\n", err);
			err = -IP_ERROR_FILE_FORMAT;
			break;
		}

		for (i = 0; i < ic->nb_streams; i++) {
			cc = ic->streams[i]->codec;
			if (cc->codec_type == CODEC_TYPE_AUDIO) {
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
			err = -IP_ERROR_FILE_FORMAT;
			break;
		}

		if (codec->capabilities & CODEC_CAP_TRUNCATED)
			cc->flags |= CODEC_FLAG_TRUNCATED;

		if (avcodec_open(cc, codec) < 0) {
			d_print("could not open codec: %d, %s\n", cc->codec_id, cc->codec_name);
			err = -IP_ERROR_FILE_FORMAT;
			break;
		}
		/* We assume below that no more errors follow. */
	} while (0);

	if (err < 0) {
		/* Clean up.  cc is never opened at this point.  (See above assumption.) */
		av_close_input_file(ic);
		return err;
	}

	priv = xnew(struct ffmpeg_private, 1);
	priv->codec_context = cc;
	priv->input_context = ic;
	priv->stream_index = stream_index;
	priv->input = ffmpeg_input_create();
	if (priv->input == NULL) {
		avcodec_close(cc);
		av_close_input_file(ic);
		free(priv);
		return -IP_ERROR_INTERNAL;
	}
	priv->output = ffmpeg_output_create();

	ip_data->private = priv;
	ip_data->sf = sf_rate(cc->sample_rate) | sf_channels(cc->channels) | sf_bits(16) | sf_signed(1);
	return 0;
}

static int ffmpeg_close(struct input_plugin_data *ip_data)
{
	struct ffmpeg_private *priv = ip_data->private;

	avcodec_close(priv->codec_context);
	av_close_input_file(priv->input_context);
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
			      struct ffmpeg_output *output)
{
	/* frame_size specifies the size of output->buffer for
	 * avcodec_decode_audio2. */
	int frame_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
	int len;

	while (1) {
		if (input->curr_pkt_size <= 0) {
			av_free_packet(&input->pkt);
			if (av_read_frame(ic, &input->pkt) < 0) {
				/* Force EOF once we can read no longer. */
				return 0;
			}
			input->curr_pkt_size = input->pkt.size;
			input->curr_pkt_buf = input->pkt.data;
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
#else
		{
			AVPacket avpkt;
			av_init_packet(&avpkt);
			avpkt.data = input->curr_pkt_buf;
			avpkt.size = input->curr_pkt_size;
			len = avcodec_decode_audio3(cc, (int16_t *) output->buffer, &frame_size, &avpkt);
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
		if (frame_size > 0) {
			output->buffer_pos = output->buffer;
			output->buffer_used_len = frame_size;
			return frame_size;
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
		rc = ffmpeg_fill_buffer(priv->input_context, priv->codec_context, priv->input, priv->output);
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
	/* time base is 1/framerate */
	int64_t pts = (int64_t) offset * st->time_base.den;
#endif

	ret = av_seek_frame(priv->input_context, priv->stream_index, pts, 0);

	if (ret < 0) {
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
	} else {
		ffmpeg_buffer_flush(priv->output);
		return 0;
	}
}

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

static int ffmpeg_read_comments(struct input_plugin_data *ip_data, struct keyval **comments)
{
	char buff[16];
	struct ffmpeg_private *priv = ip_data->private;
	AVFormatContext *ic = priv->input_context;
	int i = 0;

	*comments = xnew0(struct keyval, NUM_FFMPEG_KEYS + 1);

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

	return 0;
}

static int ffmpeg_duration(struct input_plugin_data *ip_data)
{
	struct ffmpeg_private *priv = ip_data->private;
	return priv->input_context->duration / 1000000L;
}

const struct input_plugin_ops ip_ops = {
	.open = ffmpeg_open,
	.close = ffmpeg_close,
	.read = ffmpeg_read,
	.seek = ffmpeg_seek,
	.read_comments = ffmpeg_read_comments,
	.duration = ffmpeg_duration
};

#ifdef USE_FALLBACK_IP
const char *const ip_extensions[] = { "any", NULL };
#else
const char *const ip_extensions[] = { "ape", "wma", NULL };
#endif
const char *const ip_mime_types[] = { NULL };
