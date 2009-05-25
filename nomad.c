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

/*
 * Gapless decoding added by Chun-Yu Shei <cshei AT cs.indiana.edu>
 */

/*
 * Xing code copied from xmms-mad plugin.
 * Lame code copied from mpd
 */

#include "nomad.h"
#include "id3.h"
#include "xmalloc.h"
#include "debug.h"

#include <mad.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/* the number of samples of silence the decoder inserts at start */
#define DECODERDELAY 529


struct seek_idx_entry {
	off_t offset;
	mad_timer_t timer;
};

struct nomad {
	struct mad_stream stream;
	struct mad_frame frame;
	struct mad_synth synth;
	mad_timer_t timer;
	unsigned long cur_frame;
	off_t input_offset;
	/* MAD_BUFFER_GUARD zeros are required at the end of the stream to decode the last frame
	   ref: http://www.mars.org/mailman/public/mad-dev/2001-May/000262.html */
	unsigned char input_buffer[INPUT_BUFFER_SIZE + MAD_BUFFER_GUARD];
	int i;
	unsigned int fast : 1;
	unsigned int has_xing : 1;
	unsigned int has_lame : 1;
	unsigned int seen_first_frame : 1;
	unsigned int readEOF : 1;
	int start_drop_frames;
	int start_drop_samples;
	int end_drop_samples;
	int end_drop_frames;

	struct {
		unsigned int flags;
		unsigned int nr_frames;
		unsigned int bytes;
		unsigned int scale;
		unsigned char toc[100];
	} xing;

	struct {
		char encoder[10];   /* 9 byte encoder name/version ("LAME3.97b") */
#if 0
		/* See related comment in parse_lame() */
		float peak;         /* replaygain peak */
		float trackGain;    /* replaygain track gain */
		float albumGain;    /* replaygain album gain */
#endif
		int encoderDelay;   /* # of added samples at start of mp3 */
		int encoderPadding; /* # of added samples at end of mp3 */
	} lame;

	struct {
		int size;
		struct seek_idx_entry *table;
	} seek_idx;

	struct nomad_info info;
	void *datasource;
	int datasource_fd;
	struct nomad_callbacks cbs;
};

/* ------------------------------------------------------------------------- */

static ssize_t default_read(void *datasource, void *buffer, size_t count)
{
	int fd = *(int *)datasource;

	return read(fd, buffer, count);
}

static off_t default_lseek(void *datasource, off_t offset, int whence)
{
	int fd = *(int *)datasource;

	return lseek(fd, offset, whence);
}

static int default_close(void *datasource)
{
	int fd = *(int *)datasource;

	return close(fd);
}

/* ------------------------------------------------------------------------- */

static inline int scale(mad_fixed_t sample)
{
	sample += 1L << (MAD_F_FRACBITS - 16);
	if (sample >= MAD_F_ONE) {
		sample = MAD_F_ONE - 1;
	} else if (sample < -MAD_F_ONE) {
		sample = -MAD_F_ONE;
	}
	return sample >> (MAD_F_FRACBITS - 15);
}

static inline double timer_to_seconds(mad_timer_t timer)
{
	signed long ms;
	
	ms = mad_timer_count(timer, MAD_UNITS_MILLISECONDS);
	return (double)ms / 1000.0;
}

static int parse_lame(struct nomad *nomad, struct mad_bitptr ptr, int bitlen)
{
	int i;

	/* Unlike the xing header, the lame tag has a fixed length.  Fail if
	 * not all 36 bytes (288 bits) are there. */
	if (bitlen < 288) return 0;

	for (i = 0; i < 9; i++) nomad->lame.encoder[i] = (char)mad_bit_read(&ptr, 8);
	nomad->lame.encoder[9] = '\0';

	/* This is technically incorrect, since the encoder might not be lame.
	 * But there's no other way to determine if this is a lame tag, and we
	 * wouldn't want to go reading a tag that's not there. */
	if (strncmp(nomad->lame.encoder, "LAME", 4) != 0) return 0;

#if 0
	/* Apparently lame versions <3.97b1 do not calculate replaygain.  I'm
	 * using lame 3.97b2, and while it does calculate replaygain, it's
	 * setting the values to 0.  Using --replaygain-(fast|accurate) doesn't
	 * make any difference.  Leaving this code unused until we have a way
	 * of testing it. -- jat */

	mad_bit_read(&ptr, 16);

	mad_bit_read(&ptr, 32); /* peak */

	mad_bit_read(&ptr, 6); /* header */
	bits = mad_bit_read(&ptr, 1); /* sign bit */
	nomad->lame.trackGain = mad_bit_read(&ptr, 9); /* gain*10 */
	nomad->lame.trackGain = (&bits ? -nomad->lame.trackGain : nomad->lame.trackGain) / 10;

	mad_bit_read(&ptr, 6); /* header */
	bits = mad_bit_read(&ptr, 1); /* sign bit */
	nomad->lame.albumGain = mad_bit_read(&ptr, 9); /* gain*10 */
	nomad->lame.albumGain = (bits ? -nomad->lame.albumGain : nomad->lame.albumGain) / 10;

	mad_bit_read(&ptr, 16);
#else
	mad_bit_read(&ptr, 96);
#endif

	nomad->lame.encoderDelay = mad_bit_read(&ptr, 12);
	nomad->lame.encoderPadding = mad_bit_read(&ptr, 12);
#if defined(DEBUG_LAME)
	d_print("encoderDelay: %d, encoderPadding: %d\n", nomad->lame.encoderDelay, nomad->lame.encoderPadding);
#endif

	mad_bit_read(&ptr, 96);

	bitlen -= 288;

	nomad->start_drop_frames = 1;	/* XING/LAME header is an empty frame */
	nomad->start_drop_samples = nomad->lame.encoderDelay + DECODERDELAY;
	nomad->end_drop_samples = nomad->lame.encoderPadding - DECODERDELAY;

	nomad->has_lame = 1;

	return 1;
}

enum {
	XING_FRAMES = 0x00000001L,
	XING_BYTES  = 0x00000002L,
	XING_TOC    = 0x00000004L,
	XING_SCALE  = 0x00000008L
};

/*
 * format:
 *
 *   4 "Xing"
 *   4 flags
 *   4 frames (optional)
 *   4 bytes  (optional)
 * 100 TOC    (optional)
 *   4 scale  (optional)
 */
static int xing_parse(struct nomad *nomad)
{
	struct mad_bitptr ptr = nomad->stream.anc_ptr;
	int oldbitlen = nomad->stream.anc_bitlen;
	int bitlen = nomad->stream.anc_bitlen;
	int bitsleft;

	nomad->has_xing = 0;
	nomad->has_lame = 0;
	if (bitlen < 64)
		return -1;
	if (mad_bit_read(&ptr, 32) != (('X' << 24) | ('i' << 16) | ('n' << 8) | 'g'))
		return -1;
	nomad->xing.flags = mad_bit_read(&ptr, 32);
	bitlen -= 64;
	if (nomad->xing.flags & XING_FRAMES) {
		if (bitlen < 32)
			return -1;
		nomad->xing.nr_frames = mad_bit_read(&ptr, 32);
		bitlen -= 32;
	}
	if (nomad->xing.flags & XING_BYTES) {
		if (bitlen < 32)
			return -1;
		nomad->xing.bytes = mad_bit_read(&ptr, 32);
		bitlen -= 32;
	}
	if (nomad->xing.flags & XING_TOC) {
		int i;

		if (bitlen < 800)
			return -1;
		for (i = 0; i < 100; i++)
			nomad->xing.toc[i] = mad_bit_read(&ptr, 8);
		bitlen -= 800;
	}
	if (nomad->xing.flags & XING_SCALE) {
		if (bitlen < 32)
			return -1;
		nomad->xing.scale = mad_bit_read(&ptr, 32);
		bitlen -= 32;
	}

	/* Make sure we consume no less than 120 bytes (960 bits) in hopes that
	 * the LAME tag is found there, and not right after the Xing header */
	bitsleft = 960 - (oldbitlen - bitlen);
	if (bitsleft < 0) return -1;
	else if (bitsleft > 0) {
		mad_bit_read(&ptr, bitsleft);
		bitlen -= bitsleft;
	}

	nomad->has_xing = 1;
#if defined(DEBUG_XING)
	if (nomad->xing.flags & XING_FRAMES)
		d_print("frames: %d (xing)\n", nomad->xing.nr_frames);
#endif

	parse_lame(nomad, ptr, bitlen);

	return 0;
}

/*
 * returns:
 *    0: eof
 *   -1: error
 *   >0: ok
 */
static int fill_buffer(struct nomad *nomad)
{
	if (nomad->stream.buffer == NULL || nomad->stream.error == MAD_ERROR_BUFLEN) {
		ssize_t read_size, remaining, len;
		unsigned char *read_start;

		if (nomad->stream.next_frame != NULL) {
			remaining = nomad->stream.bufend - nomad->stream.next_frame;
			memmove(nomad->input_buffer, nomad->stream.next_frame, remaining);
			read_start = nomad->input_buffer + remaining;
			read_size = INPUT_BUFFER_SIZE - remaining;
		} else {
			read_size = INPUT_BUFFER_SIZE;
			read_start = nomad->input_buffer;
			remaining = 0;
		}
		read_size = nomad->cbs.read(nomad->datasource, read_start, read_size);
		if (read_size == -1) {
			if (errno != EAGAIN)
				d_print("read error on bitstream (%d:%s)\n", errno, strerror(errno));
			return -1;
		}
		if (read_size == 0) {
			if (!nomad->readEOF) {
				memset(nomad->input_buffer + remaining, 0, MAD_BUFFER_GUARD);
				remaining += MAD_BUFFER_GUARD;
				d_print("hit end of stream, appended MAD_BUFFER_GUARD zeros\n");
				nomad->readEOF = 1;
			}
			else return 0;
		}

		len = read_size + remaining;

		nomad->input_offset += read_size;

		mad_stream_buffer(&nomad->stream, nomad->input_buffer, len);
		nomad->stream.error = 0;
	}
	return 1;
}

static void handle_lost_sync(struct nomad *nomad)
{
	unsigned long frame;
	int size;

	frame = nomad->cur_frame;
	if (frame == 0) {
		/* cur_frame is not set when scanning file */
		frame = nomad->info.nr_frames;
	}

	size = id3_tag_size((const char *)nomad->stream.this_frame,
			nomad->stream.bufend - nomad->stream.this_frame);
	if (size > 0) {
		d_print("frame %ld, skipping ID3 tag (%d bytes)\n", frame, size);
		mad_stream_skip(&nomad->stream, size);
	} else {
		d_print("frame %ld\n", frame);
	}
}


/* Builds a seek index as the file is decoded
 * NOTE: increases nomad->timer (current position)
 */
static void build_seek_index(struct nomad *nomad)
{
	mad_timer_t timer_now = nomad->timer;
	off_t offset;
	int idx;

	mad_timer_add(&nomad->timer, nomad->frame.header.duration);

	if (nomad->has_xing)
		return;

	if (nomad->timer.seconds < (nomad->seek_idx.size + 1) * SEEK_IDX_INTERVAL)
		return;

	/* offset = ftell() */
	offset = nomad->input_offset;
	/* subtract by buffer length to get offset to start of buffer */
	offset -= (nomad->stream.bufend - nomad->input_buffer);
	/* then add offset to the current frame */
	offset += (nomad->stream.this_frame - nomad->input_buffer);

	idx = nomad->seek_idx.size;

	nomad->seek_idx.table = xrenew(struct seek_idx_entry, nomad->seek_idx.table, idx + 1);
	nomad->seek_idx.table[idx].offset = offset;
	nomad->seek_idx.table[idx].timer = timer_now;

	nomad->seek_idx.size++;
}

static void calc_fast(struct nomad *nomad)
{
	nomad->info.avg_bitrate = -1;
	nomad->info.vbr = -1;
	if (nomad->has_xing && (nomad->xing.flags & XING_FRAMES)) {
		nomad->info.nr_frames = nomad->xing.nr_frames;
		mad_timer_multiply(&nomad->timer, nomad->info.nr_frames);
	} else {
		nomad->info.nr_frames = nomad->info.filesize /
			(nomad->stream.next_frame - nomad->stream.this_frame);
		mad_timer_multiply(&nomad->timer, nomad->info.nr_frames);
	}
}

/*
 * fields
 *     nomad->info.avg_bitrate and
 *     nomad->info.vbr
 * are filled only if fast = 0
 */
static int scan(struct nomad *nomad)
{
	struct mad_header *header = &nomad->frame.header;
	int old_bitrate = 0;
	unsigned long long int bitrate_sum = 0;

	nomad->info.nr_frames = 0;
	nomad->info.vbr = 0;
	while (1) {
		int rc;

		rc = fill_buffer(nomad);
		if (rc == -1)
			return -1;
		if (rc == 0)
			break;

		if (mad_frame_decode(&nomad->frame, &nomad->stream) == -1) {
			if (nomad->stream.error == MAD_ERROR_BUFLEN)
				continue;
			if (!MAD_RECOVERABLE(nomad->stream.error)) {
				d_print("unrecoverable frame level error.\n");
				return -1;
			}
			if (nomad->stream.error == MAD_ERROR_LOSTSYNC)
				handle_lost_sync(nomad);
			continue;
		}

		build_seek_index(nomad);
		bitrate_sum += header->bitrate;
		nomad->info.nr_frames++;

		if (nomad->info.nr_frames == 1) {
			// first valid frame
			nomad->info.sample_rate = header->samplerate;
			nomad->info.channels = MAD_NCHANNELS(header);
			nomad->info.layer = header->layer;
			nomad->info.dual_channel = header->mode == MAD_MODE_DUAL_CHANNEL;
			nomad->info.joint_stereo = header->mode == MAD_MODE_JOINT_STEREO;

			xing_parse(nomad);

			if (nomad->fast) {
				calc_fast(nomad);
				break;
			}
		} else {
			if (old_bitrate != header->bitrate)
				nomad->info.vbr = 1;
		}
		old_bitrate = header->bitrate;
	}
	if (nomad->info.nr_frames == 0) {
		d_print("error: not an mp3 file!\n");
		return -NOMAD_ERROR_FILE_FORMAT;
	}
	nomad->info.duration = timer_to_seconds(nomad->timer);
	if (!nomad->fast)
		nomad->info.avg_bitrate = bitrate_sum / nomad->info.nr_frames;
	nomad->cur_frame = 0;
	nomad->cbs.lseek(nomad->datasource, 0, SEEK_SET);
	nomad->input_offset = 0;
	return 0;
}

static int decode(struct nomad *nomad)
{
	int rc;

start:
	rc = fill_buffer(nomad);
	if (rc == -1)
		return -1;
	if (rc == 0)
		return 1;

	if (mad_frame_decode(&nomad->frame, &nomad->stream)) {
		if (nomad->stream.error == MAD_ERROR_BUFLEN)
			goto start;
		if (!MAD_RECOVERABLE(nomad->stream.error)) {
			d_print("unrecoverable frame level error.\n");
			return -1;
		}
		if (nomad->stream.error == MAD_ERROR_LOSTSYNC)
			handle_lost_sync(nomad);
		goto start;
	}
	nomad->cur_frame++;
	if (nomad->info.filesize > 0) {
		build_seek_index(nomad);
	} else {
		mad_timer_add(&nomad->timer, nomad->frame.header.duration);
	}
	mad_synth_frame(&nomad->synth, &nomad->frame);
	return 0;
}

static void init_mad(struct nomad *nomad)
{
	mad_stream_init(&nomad->stream);
	mad_frame_init(&nomad->frame);
	mad_synth_init(&nomad->synth);
	mad_timer_reset(&nomad->timer);
	nomad->cur_frame = 0;
	nomad->i = -1;
	nomad->input_offset = 0;
	nomad->seen_first_frame = 0;
	nomad->readEOF = 0;
}

static void free_mad(struct nomad *nomad)
{
	mad_stream_finish(&nomad->stream);
	mad_frame_finish(&nomad->frame);
	mad_synth_finish(nomad->synth);
}

static int do_open(struct nomad *nomad, int fast)
{
	int rc;

	init_mad(nomad);
	nomad->info.filesize = nomad->cbs.lseek(nomad->datasource, 0, SEEK_END);
	if (nomad->info.filesize == -1) {
		nomad->fast = 1;
	} else {
		nomad->fast = fast != 0;
		nomad->cbs.lseek(nomad->datasource, 0, SEEK_SET);
	}
	if (nomad->info.filesize == -1) {
		rc = decode(nomad);
		if (rc < 0)
			goto error;
		if (rc == 1)
			goto eof;
		nomad->info.sample_rate = nomad->frame.header.samplerate;
		nomad->info.channels = MAD_NCHANNELS(&nomad->frame.header);
		nomad->info.layer = nomad->frame.header.layer;
		nomad->info.dual_channel = nomad->frame.header.mode == MAD_MODE_DUAL_CHANNEL;
		nomad->info.joint_stereo = nomad->frame.header.mode == MAD_MODE_JOINT_STEREO;

		/* unknown */
		nomad->info.duration = -1.0;
		nomad->info.nr_frames = -1;
		nomad->info.vbr = -1;
		nomad->info.avg_bitrate = -1;
	} else {
		rc = scan(nomad);
		if (rc < 0)
			goto error;
		if (rc == 1)
			goto eof;
		free_mad(nomad);
		init_mad(nomad);
	}
	d_print("\n  frames: %d, br: %d b/s, sr: %d Hz, ch: %d, layer: %d, joint stereo: %d\n"
		"  dual channel: %d, vbr: %d, duration: %g s, xing: %d\n",
			nomad->info.nr_frames, nomad->info.avg_bitrate,
			nomad->info.sample_rate, nomad->info.channels,
			nomad->info.layer, nomad->info.joint_stereo,
			nomad->info.dual_channel, nomad->info.vbr,
			nomad->info.duration,
			nomad->has_xing);
#if defined(DEBUG_XING)
	if (nomad->has_xing)
		d_print("xing: flags: 0x%x, frames: %d, bytes: %d, scale: %d\n",
			nomad->xing.flags,
			nomad->xing.nr_frames,
			nomad->xing.bytes,
			nomad->xing.scale);
#endif
	return 0;
error:
	nomad_close(nomad);
	return rc;
eof:
	nomad_close(nomad);
	return -NOMAD_ERROR_FILE_FORMAT;
}

int nomad_open(struct nomad **nomadp, int fd, int fast)
{
	struct nomad *nomad;

	nomad = xnew0(struct nomad, 1);
	nomad->datasource = &nomad->datasource_fd;
	nomad->datasource_fd = fd;
	nomad->cbs.read = default_read;
	nomad->cbs.lseek = default_lseek;
	nomad->cbs.close = default_close;
	nomad->start_drop_samples = 0;
	nomad->end_drop_samples = 0;
	*nomadp = nomad;
	/* on error do_open calls nomad_close */
	return do_open(nomad, fast);
}

int nomad_open_callbacks(struct nomad **nomadp, void *datasource, int fast, struct nomad_callbacks *cbs)
{
	struct nomad *nomad;

	nomad = xnew0(struct nomad, 1);
	nomad->datasource = datasource;
	nomad->cbs = *cbs;
	*nomadp = nomad;
	/* on error do_open calls nomad_close */
	return do_open(nomad, fast);
}

void nomad_close(struct nomad *nomad)
{
	free_mad(nomad);
	nomad->cbs.close(nomad->datasource);
	free(nomad->seek_idx.table);
	free(nomad);
}

void nomad_info(struct nomad *nomad, struct nomad_info *info)
{
	*info = nomad->info;
}

int nomad_read(struct nomad *nomad, char *buffer, int count)
{
	int i, j, size, psize, to;
	
	if (nomad->i == -1) {
		int rc;

next_frame:
		rc = decode(nomad);
		if (rc < 0)
			return rc;
		if (rc == 1)
			return 0;
		nomad->i = 0;
	}

	if (nomad->has_lame) {
		/* skip samples at start for gapless playback */
		if (nomad->start_drop_frames) {
			nomad->start_drop_frames--;
			/* XING header is an empty frame we want to skip */
			if (!nomad->seen_first_frame) {
				nomad->cur_frame--;
				nomad->seen_first_frame = 1;
			}
#if defined(DEBUG_LAME)
			d_print("skipped a frame at start\n");
#endif
			goto next_frame;
		}
		if (nomad->start_drop_samples) {
			if (nomad->start_drop_samples < nomad->synth.pcm.length) {
				nomad->i += nomad->start_drop_samples;
				nomad->start_drop_samples = 0;
				/* Take advantage of the fact that this block is only executed once per file, and
				   calculate the # of samples/frames to skip at the end.  Note that synth.pcm.length
				   is needed for the calculation. */
				nomad->end_drop_frames = nomad->end_drop_samples / nomad->synth.pcm.length;
				nomad->end_drop_samples = nomad->end_drop_samples % nomad->synth.pcm.length;
#if defined(DEBUG_LAME)
				d_print("skipped %d samples at start\n", nomad->i);
				d_print("will skip %d samples and %d frame(s) at end\n",
					nomad->end_drop_samples, nomad->end_drop_frames);
#endif
			}
			else {
				nomad->start_drop_samples -= nomad->synth.pcm.length;
#if defined(DEBUG_LAME)
				d_print("skipped %d samples at start and moving to next frame\n", nomad->synth.pcm.length);
#endif
				goto next_frame;
			}
		}
		/* skip samples/frames at end for gapless playback */
		if (nomad->cur_frame == (nomad->xing.nr_frames + 1 - nomad->end_drop_frames)) {
#if defined(DEBUG_LAME)
				d_print("skipped %d frame(s) at end\n", nomad->end_drop_frames);
#endif
			return 0;
		}
	}

	psize = nomad->info.channels * 16 / 8;
	size = (nomad->synth.pcm.length - nomad->i) * psize;

	if (size > count) {
		to = nomad->i + count / psize;
	} else {
		to = nomad->synth.pcm.length;
	}
	j = 0;
	for (i = nomad->i; i < to; i++) {
		short sample;

		/* skip samples/frames at end for gapless playback */
		if (nomad->has_lame
		    && nomad->end_drop_samples
		    && (nomad->cur_frame == (nomad->xing.nr_frames - nomad->end_drop_frames))
		    && i == (nomad->synth.pcm.length - nomad->end_drop_samples)) {
			nomad->i = -1;
#if defined(DEBUG_LAME)
			d_print("skipped %d samples at end of frame %d\n", nomad->end_drop_samples, (int)nomad->cur_frame);
#endif
			return j;
		}
		sample = scale(nomad->synth.pcm.samples[0][i]);
		buffer[j++] = (sample >> 0) & 0xff;
		buffer[j++] = (sample >> 8) & 0xff;

		if (nomad->info.channels == 2) {
			sample = scale(nomad->synth.pcm.samples[1][i]);
			buffer[j++] = (sample >> 0) & 0xff;
			buffer[j++] = (sample >> 8) & 0xff;
		}
	}
	if (to != nomad->synth.pcm.length) {
		nomad->i = i;
	} else {
		nomad->i = -1;
	}
	return j;
}

static int nomad_time_seek_accurate(struct nomad *nomad, double pos)
{
	int rc;

	/* seek to beginning of file and search frame-by-frame */
	if (nomad->cbs.lseek(nomad->datasource, 0, SEEK_SET) < 0)
		return -1;

	/* XING header should NOT be counted - if we're here, we know it's present */
	nomad->cur_frame = -1;

	while (timer_to_seconds(nomad->timer) < pos) {
		rc = fill_buffer(nomad);
		if (rc == -1)
			return -1;
		if (rc == 0)
			return 1;

		if (mad_header_decode(&nomad->frame.header, &nomad->stream)) {
			if (nomad->stream.error == MAD_ERROR_BUFLEN)
				continue;
			if (!MAD_RECOVERABLE(nomad->stream.error)) {
				d_print("unrecoverable frame level error.\n");
				return -1;
			}
			if (nomad->stream.error == MAD_ERROR_LOSTSYNC)
				handle_lost_sync(nomad);
			continue;
		}
		nomad->cur_frame++;
		mad_timer_add(&nomad->timer, nomad->frame.header.duration);
	}
#if defined(DEBUG_LAME)
		d_print("seeked to %g = %g\n", pos, timer_to_seconds(nomad->timer));
#endif
	return 0;
}

int nomad_time_seek(struct nomad *nomad, double pos)
{
	off_t offset = 0;

	if (pos < 0.0 || pos > nomad->info.duration) {
		errno = EINVAL;
		return -1;
	}
	if (nomad->info.filesize == -1) {
		errno = ESPIPE;
		return -1;
	}
	free_mad(nomad);
	init_mad(nomad);

	/* if file has a LAME header, perform frame-accurate seek for gapless playback */
	if (nomad->has_lame) {
		return nomad_time_seek_accurate(nomad, pos);
	} else if (nomad->has_xing) {
		/* calculate seek offset */
		/* seek to truncate(pos / duration * 100) / 100 * duration */
		double k, tmp_pos;
		int ki;

		k = pos / nomad->info.duration * 100.0;
		ki = k;
		tmp_pos = ((double)ki) / 100.0 * nomad->info.duration;
		nomad->timer.seconds = (signed int)tmp_pos;
		nomad->timer.fraction = (tmp_pos - (double)nomad->timer.seconds) * MAD_TIMER_RESOLUTION;
#if defined(DEBUG_XING)
		d_print("seeking to %g = %g %d%%\n",
				pos,
				timer_to_seconds(nomad->timer),
				ki);
#endif
		offset = ((unsigned long long)nomad->xing.toc[ki] * nomad->xing.bytes) / 256;
	} else if (nomad->seek_idx.size > 0) {
		int idx = (int)(pos / SEEK_IDX_INTERVAL) - 1;

		if (idx > nomad->seek_idx.size - 1)
			idx = nomad->seek_idx.size - 1;

		if (idx >= 0) {
			offset = nomad->seek_idx.table[idx].offset;
			nomad->timer = nomad->seek_idx.table[idx].timer;
		}
	}
	if (nomad->cbs.lseek(nomad->datasource, offset, SEEK_SET) < 0)
		return -1;

	nomad->input_offset = offset;
	while (timer_to_seconds(nomad->timer) < pos) {
		int rc;

		rc = fill_buffer(nomad);
		if (rc == -1)
			return -1;
		if (rc == 0)
			return 0;

		if (mad_header_decode(&nomad->frame.header, &nomad->stream) == 0) {
			build_seek_index(nomad);
		} else {
			if (!MAD_RECOVERABLE(nomad->stream.error) && nomad->stream.error != MAD_ERROR_BUFLEN) {
				d_print("unrecoverable frame level error.\n");
				return -1;
			}
			if (nomad->stream.error == MAD_ERROR_LOSTSYNC)
				handle_lost_sync(nomad);
		}
	}
#if defined(DEBUG_XING)
	if (nomad->has_xing)
		d_print("seeked to %g = %g\n", pos, timer_to_seconds(nomad->timer));
#endif
	return 0;
}

double nomad_time_tell(struct nomad *nomad)
{
	return timer_to_seconds(nomad->timer);
}

double nomad_time_total(struct nomad *nomad)
{
	return nomad->info.duration;
}
