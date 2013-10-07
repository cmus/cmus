/*
 * Copyright 2008-2013 Various Authors
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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
#include "misc.h"

#include <mad.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define INPUT_BUFFER_SIZE	(5 * 8192)
#define SEEK_IDX_INTERVAL	15

/* the number of samples of silence the decoder inserts at start */
#define DECODERDELAY		529

#define XING_MAGIC (('X' << 24) | ('i' << 16) | ('n' << 8) | 'g')
#define INFO_MAGIC (('I' << 24) | ('n' << 16) | ('f' << 8) | 'o')

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
	unsigned int has_xing : 1;
	unsigned int has_lame : 1;
	unsigned int seen_first_frame : 1;
	unsigned int readEOF : 1;
	int start_drop_frames;
	int start_drop_samples;
	int end_drop_samples;
	int end_drop_frames;

	struct nomad_xing xing;
	struct nomad_lame lame;

	struct {
		int size;
		struct seek_idx_entry *table;
	} seek_idx;

	struct {
		unsigned long long int bitrate_sum;
		unsigned long nr_frames;
	} current;

	struct nomad_info info;
	void *datasource;
	int datasource_fd;
	struct nomad_callbacks cbs;
};

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
	int i, adj = 0;
	unsigned int version_major, version_minor;
	float val;

	/* Unlike the xing header, the lame tag has a fixed length.  Fail if
	 * not all 36 bytes (288 bits) are there. */
	if (bitlen < 288) return 0;

	for (i = 0; i < 9; i++) nomad->lame.encoder[i] = (char)mad_bit_read(&ptr, 8);
	nomad->lame.encoder[9] = '\0';

	/* This is technically incorrect, since the encoder might not be lame.
	 * But there's no other way to determine if this is a lame tag, and we
	 * wouldn't want to go reading a tag that's not there. */
	if (strncmp(nomad->lame.encoder, "LAME", 4) != 0) return 0;

	if (sscanf(nomad->lame.encoder + 4, "%u.%u", &version_major, &version_minor) != 2)
		return 0;

#if defined(DEBUG_LAME)
	d_print("detected LAME version %s\n", nomad->lame.encoder + 4);
#endif

	i = mad_bit_read(&ptr, 4);
#if defined(DEBUG_LAME)
	d_print("LAME tag revision: %d\n", i);
#endif
	nomad->lame.vbr_method = mad_bit_read(&ptr, 4);

	/* ReplayGain in LAME tag was added in 3.94 */
	if (version_major > 3 || (version_major == 3 && version_minor >= 94)) {
		/* lowpass */
		mad_bit_read(&ptr, 8);

		/* The reference volume was changed from the 83dB used in the
		 * ReplayGain spec to 89dB in lame 3.95.1.  Bump the gain for older
		 * versions, since everyone else uses 89dB instead of 83dB.
		 * Unfortunately, lame didn't differentiate between 3.95 and 3.95.1, so
		 * it's impossible to make the proper adjustment for 3.95.
		 * Fortunately, 3.95 was only out for about a day before 3.95.1 was
		 * released. -- tmz */
		if (version_major < 3 || (version_major == 3 && version_minor < 95))
			adj = 6;

		val = mad_bit_read(&ptr, 32) / (float) (1 << 23);
		/* peak value of 0.0 means lame didn't calculate the peak at all
		 * (--replaygain-fast), even silence has a value > 0.0 */
		if (val)
			nomad->lame.peak = val;
		for (i = 0; i < 2; i++) {
			int gain, gain_type;
			gain_type = replaygain_decode(mad_bit_read(&ptr, 16), &gain);
			val = gain / 10.f + adj;
			if (gain_type == 1)
				nomad->lame.trackGain = val;
			/* LAME currently doesn't store any album gain!
			else if (gain_type == 2)
				nomad->lame.albumGain = val;
			*/
		}

		/*
		 * 4 encoding flags
		 * 4 ATH type
		 * 8 minimal bitrate (if ABR -> specified bitrate)
		 */
		mad_bit_read(&ptr, 16);
	} else
		mad_bit_read(&ptr, 88);

	nomad->lame.encoderDelay = mad_bit_read(&ptr, 12);
	nomad->lame.encoderPadding = mad_bit_read(&ptr, 12);
#if defined(DEBUG_LAME)
	if (adj > 0)
		d_print("adjusted gains by %+d dB (old LAME)\n", adj);
	if (!isnan(nomad->lame.peak))
		d_print("peak: %f\n", nomad->lame.peak);
	if (!isnan(nomad->lame.trackGain))
		d_print("trackGain: %+.1f dB\n", nomad->lame.trackGain);
	if (!isnan(nomad->lame.albumGain))
		d_print("albumGain: %+.1f dB\n", nomad->lame.albumGain);
	d_print("encoderDelay: %d, encoderPadding: %d\n", nomad->lame.encoderDelay, nomad->lame.encoderPadding);
#endif

	mad_bit_read(&ptr, 96);

	nomad->start_drop_frames = 1;	/* XING/LAME header is an empty frame */
	nomad->start_drop_samples = nomad->lame.encoderDelay + DECODERDELAY;
	nomad->end_drop_samples = nomad->lame.encoderPadding - DECODERDELAY;

	nomad->has_lame = 1;

	return 1;
}

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
	struct mad_bitptr start = ptr;
	int oldbitlen = nomad->stream.anc_bitlen;
	int bitlen = nomad->stream.anc_bitlen;
	int bitsleft;
	unsigned xing_id;

	nomad->has_xing = 0;
	nomad->has_lame = 0;
	if (bitlen < 64)
		return -1;
	xing_id = mad_bit_read(&ptr, 32);
	if (xing_id != XING_MAGIC && xing_id != INFO_MAGIC) {
		/*
		 * Due to an unfortunate historical accident, a Xing VBR tag
		 * may be misplaced in a stream with CRC protection. We check
		 * for this by assuming the tag began two octets prior and the
		 * high bits of the following flags field are always zero.
		 */
		if (xing_id != (((XING_MAGIC+0UL) << 16) & 0xffffffffL) &&
				xing_id != (((INFO_MAGIC+0UL) << 16) & 0xffffffffL))
			return -1;
		xing_id >>= 16;
		ptr = start;
		mad_bit_skip(&ptr, 16);
		bitlen += 16;
	}
	nomad->xing.is_info = ((xing_id & 0x0000ffffL) == (INFO_MAGIC & 0x0000ffffL));
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

static void calc_frames_fast(struct nomad *nomad)
{
	if (nomad->has_xing && (nomad->xing.flags & XING_FRAMES) && nomad->xing.nr_frames) {
		nomad->info.nr_frames = nomad->xing.nr_frames;
		mad_timer_multiply(&nomad->timer, nomad->info.nr_frames);
	} else {
		nomad->info.nr_frames = nomad->info.filesize /
			(nomad->stream.next_frame - nomad->stream.this_frame);
		mad_timer_multiply(&nomad->timer, nomad->info.nr_frames);
	}
}

static void calc_bitrate_fast(struct nomad *nomad)
{
	nomad->info.vbr = nomad->has_xing ? !nomad->xing.is_info : 0;

	if (nomad->has_lame && nomad->lame.vbr_method == 1)
		nomad->info.vbr = 0;

	if (nomad->has_xing && (nomad->xing.flags & XING_BYTES) && nomad->xing.bytes)
		nomad->info.avg_bitrate = (nomad->xing.bytes * 8.0) / nomad->info.duration;
	else
		nomad->info.avg_bitrate = nomad->frame.header.bitrate;
}

/*
 * fields
 *     nomad->info.avg_bitrate and
 *     nomad->info.vbr
 * are only estimated
 */
static int scan(struct nomad *nomad)
{
	struct mad_header *header = &nomad->frame.header;

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

		// first valid frame
		nomad->info.sample_rate = header->samplerate;
		nomad->info.channels = MAD_NCHANNELS(header);
		nomad->info.layer = header->layer;
		nomad->info.dual_channel = header->mode == MAD_MODE_DUAL_CHANNEL;
		nomad->info.joint_stereo = header->mode == MAD_MODE_JOINT_STEREO;

		xing_parse(nomad);
		calc_frames_fast(nomad);
		break;
	}
	if (nomad->info.nr_frames == 0) {
		d_print("error: not an mp3 file!\n");
		return -NOMAD_ERROR_FILE_FORMAT;
	}
	nomad->info.duration = timer_to_seconds(nomad->timer);
	calc_bitrate_fast(nomad);
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
	nomad->current.bitrate_sum += nomad->frame.header.bitrate;
	nomad->current.nr_frames++;
	if (nomad->info.filesize != -1) {
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
	nomad->stream.options |= MAD_OPTION_IGNORECRC;
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

static int do_open(struct nomad *nomad)
{
	int rc;

	init_mad(nomad);
	nomad->info.filesize = nomad->cbs.lseek(nomad->datasource, 0, SEEK_END);
	if (nomad->info.filesize != -1)
		nomad->cbs.lseek(nomad->datasource, 0, SEEK_SET);
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

int nomad_open_callbacks(struct nomad **nomadp, void *datasource, struct nomad_callbacks *cbs)
{
	struct nomad *nomad;

	const struct nomad nomad_init = {
		.datasource = datasource,
		.cbs = {
			.read  = cbs->read,
			.lseek = cbs->lseek,
			.close = cbs->close
		}
	};

	nomad = xnew(struct nomad, 1);
	*nomad = nomad_init;
	nomad->lame.peak = nomad->lame.trackGain = nomad->lame.albumGain = strtof("NAN", NULL);
	*nomadp = nomad;
	/* on error do_open calls nomad_close */
	return do_open(nomad);
}

void nomad_close(struct nomad *nomad)
{
	free_mad(nomad);
	nomad->cbs.close(nomad->datasource);
	free(nomad->seek_idx.table);
	free(nomad);
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
	if (nomad->cbs.lseek(nomad->datasource, 0, SEEK_SET) == -1)
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
	if (nomad->cbs.lseek(nomad->datasource, offset, SEEK_SET) == -1)
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

const struct nomad_xing *nomad_xing(struct nomad *nomad)
{
	return nomad->has_xing ? &nomad->xing : NULL;
}

const struct nomad_lame *nomad_lame(struct nomad *nomad)
{
	return nomad->has_lame ? &nomad->lame : NULL;
}

const struct nomad_info *nomad_info(struct nomad *nomad)
{
	return &nomad->info;
}

long nomad_current_bitrate(struct nomad *nomad)
{
	long bitrate = -1;
	if (nomad->current.nr_frames > 0) {
		bitrate = nomad->current.bitrate_sum / nomad->current.nr_frames;
		nomad->current.bitrate_sum = 0;
		nomad->current.nr_frames = 0;
	}
	return bitrate;
}
