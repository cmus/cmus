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
 * Xing code copied from xmms-mad plugin.
 * 
 */

#include "nomad.h"
#include "id3.h"
#include "xmalloc.h"
#include "debug.h"

#include <mad.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

struct nomad {
	struct mad_stream stream;
	struct mad_frame frame;
	struct mad_synth synth;
	struct mad_header header;
	mad_timer_t timer;
	unsigned long cur_frame;
	unsigned char input_buffer[INPUT_BUFFER_SIZE];
	int i;
	unsigned int fast : 1;
	unsigned int has_xing : 1;

	struct {
		unsigned int flags;
		unsigned int nr_frames;
		unsigned int bytes;
		unsigned int scale;
		unsigned char toc[100];
	} xing;

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
	int bitlen = nomad->stream.anc_bitlen;

	nomad->has_xing = 0;
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
	nomad->has_xing = 1;
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
		if (read_size == 0)
			return 0;

		len = read_size + remaining;
#if 0
		if (len < MAD_BUFFER_GUARD) {
			memset(nomad->input_buffer + len, 0, MAD_BUFFER_GUARD - len);
			len = MAD_BUFFER_GUARD;
		}
#endif
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

/*
 * fields
 *     nomad->info.avg_bitrate and
 *     nomad->info.vbr
 * are filled only if fast = 0
 */
static int scan(struct nomad *nomad)
{
	int frame_decoded = 0;
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

		if (mad_header_decode(&nomad->header, &nomad->stream) == 0) {
			bitrate_sum += nomad->header.bitrate;
			mad_timer_add(&nomad->timer, nomad->header.duration);
			nomad->info.nr_frames++;
			if (!frame_decoded) {
				nomad->info.sample_rate = nomad->header.samplerate;
				nomad->info.channels = MAD_NCHANNELS(&nomad->header);
				nomad->info.layer = nomad->header.layer;
				nomad->info.dual_channel = nomad->header.mode == MAD_MODE_DUAL_CHANNEL;
				nomad->info.joint_stereo = nomad->header.mode == MAD_MODE_JOINT_STEREO;

				nomad->frame.header = nomad->header;
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
				frame_decoded = 1;
				xing_parse(nomad);

#if defined(DEBUG_XING)
				if (nomad->has_xing && (nomad->xing.flags & XING_FRAMES))
					d_print("xing: frames: %d (xing)\n", nomad->xing.nr_frames);
#endif

				if (nomad->fast) {
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
					break;
				}
			} else {
				if (old_bitrate != nomad->header.bitrate)
					nomad->info.vbr = 1;
			}
			old_bitrate = nomad->header.bitrate;
		} else {
			if (!MAD_RECOVERABLE(nomad->stream.error) && nomad->stream.error != MAD_ERROR_BUFLEN) {
				d_print("unrecoverable frame level error.\n");
				return -1;
			}
			if (nomad->stream.error == MAD_ERROR_LOSTSYNC)
				handle_lost_sync(nomad);
		}
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
	mad_timer_add(&nomad->timer, nomad->frame.header.duration);
	mad_synth_frame(&nomad->synth, &nomad->frame);
	return 0;
}

static void init_mad(struct nomad *nomad)
{
	mad_stream_init(&nomad->stream);
	mad_frame_init(&nomad->frame);
	mad_synth_init(&nomad->synth);
	mad_header_init(&nomad->header);
	mad_timer_reset(&nomad->timer);
	nomad->cur_frame = 0;
	nomad->i = -1;
}

static void free_mad(struct nomad *nomad)
{
	mad_stream_finish(&nomad->stream);
	mad_frame_finish(&nomad->frame);
	mad_synth_finish(nomad->synth);
	mad_header_finish(&nomad->header);
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
		nomad->info.dual_channel = nomad->header.mode == MAD_MODE_DUAL_CHANNEL;
		nomad->info.joint_stereo = nomad->header.mode == MAD_MODE_JOINT_STEREO;

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

	nomad = xnew(struct nomad, 1);
	nomad->datasource = &nomad->datasource_fd;
	nomad->datasource_fd = fd;
	nomad->cbs.read = default_read;
	nomad->cbs.lseek = default_lseek;
	nomad->cbs.close = default_close;
	*nomadp = nomad;
	/* on error do_open calls nomad_close */
	return do_open(nomad, fast);
}

int nomad_open_callbacks(struct nomad **nomadp, void *datasource, int fast, struct nomad_callbacks *cbs)
{
	struct nomad *nomad;

	nomad = xnew(struct nomad, 1);
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

		rc = decode(nomad);
		if (rc < 0)
			return rc;
		if (rc == 1)
			return 0;
		nomad->i = 0;
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

int nomad_time_seek(struct nomad *nomad, double pos)
{
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
	if (nomad->has_xing) {
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
		nomad->cbs.lseek(nomad->datasource, (nomad->xing.toc[ki] * nomad->xing.bytes) / 256, SEEK_SET);
	} else {
		nomad->cbs.lseek(nomad->datasource, 0, SEEK_SET);
	}
	while (timer_to_seconds(nomad->timer) < pos) {
		int rc;

		rc = fill_buffer(nomad);
		if (rc == -1)
			return -1;
		if (rc == 0)
			return 0;

		if (mad_header_decode(&nomad->header, &nomad->stream) == 0) {
			mad_timer_add(&nomad->timer, nomad->header.duration);
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
