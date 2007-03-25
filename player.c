/* 
 * Copyright 2004-2006 Timo Hirvonen
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

#include "player.h"
#include "buffer.h"
#include "input.h"
#include "output.h"
#include "sf.h"
#include "utils.h"
#include "xmalloc.h"
#include "debug.h"
#include "compiler.h"

#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <stdarg.h>
#include <math.h>

enum producer_status {
	PS_UNLOADED,
	PS_STOPPED,
	PS_PLAYING,
	PS_PAUSED
};

enum consumer_status {
	CS_STOPPED,
	CS_PLAYING,
	CS_PAUSED
};

struct player_info player_info = {
	.mutex = CMUS_MUTEX_INITIALIZER,
	.ti = NULL,
	.metadata = { 0, },
	.status = PLAYER_STATUS_STOPPED,
	.pos = 0,
	.vol_left = 0,
	.vol_right = 0,
	.buffer_fill = 0,
	.buffer_size = 0,
	.error_msg = NULL,
	.file_changed = 0,
	.metadata_changed = 0,
	.status_changed = 0,
	.position_changed = 0,
	.buffer_fill_changed = 0,
	.vol_changed = 0,
};

/* continue playing after track is finished? */
int player_cont = 1;

enum replaygain replaygain;
int replaygain_limit = 1;
double replaygain_preamp = 6.0;

static const struct player_callbacks *player_cbs = NULL;

static sample_format_t buffer_sf;

static pthread_t producer_thread;
static pthread_mutex_t producer_mutex = CMUS_MUTEX_INITIALIZER;
static int producer_running = 1;
static enum producer_status producer_status = PS_UNLOADED;
static struct input_plugin *ip = NULL;

static pthread_t consumer_thread;
static pthread_mutex_t consumer_mutex = CMUS_MUTEX_INITIALIZER;
static int consumer_running = 1;
static enum consumer_status consumer_status = CS_STOPPED;
static unsigned int consumer_pos = 0;

/* for replay gain and soft vol
 * usually same as consumer_pos, sometimes less than consumer_pos
 */
static unsigned int scale_pos;
static double replaygain_scale = 1.0;

/* locking {{{ */

#define producer_lock() cmus_mutex_lock(&producer_mutex)
#define producer_unlock() cmus_mutex_unlock(&producer_mutex)

#define consumer_lock() cmus_mutex_lock(&consumer_mutex)
#define consumer_unlock() cmus_mutex_unlock(&consumer_mutex)

#define player_lock() \
	do { \
		consumer_lock(); \
		producer_lock(); \
	} while (0)

#define player_unlock() \
	do { \
		producer_unlock(); \
		consumer_unlock(); \
	} while (0)

/* locking }}} */

static void reset_buffer(void)
{
	buffer_reset();
	consumer_pos = 0;
	scale_pos = 0;
}

static void set_buffer_sf(sample_format_t sf)
{
	buffer_sf = sf;

	/* ip_read converts samples to this format */
	if (sf_get_channels(buffer_sf) <= 2 && sf_get_bits(buffer_sf) <= 16) {
		buffer_sf &= SF_RATE_MASK;
		buffer_sf |= sf_channels(2) | sf_bits(16) | sf_signed(1);
	}
}

#define SOFT_VOL_SCALE 65536

/* coefficients for volumes 0..99, for 100 65536 is used
 * data copied from alsa-lib src/pcm/pcm_softvol.c
 */
static const unsigned short soft_vol_db[100] = {
	0x0000, 0x0110, 0x011c, 0x012f, 0x013d, 0x0152, 0x0161, 0x0179,
	0x018a, 0x01a5, 0x01c1, 0x01d5, 0x01f5, 0x020b, 0x022e, 0x0247,
	0x026e, 0x028a, 0x02b6, 0x02d5, 0x0306, 0x033a, 0x035f, 0x0399,
	0x03c2, 0x0403, 0x0431, 0x0479, 0x04ac, 0x04fd, 0x0553, 0x058f,
	0x05ef, 0x0633, 0x069e, 0x06ea, 0x0761, 0x07b5, 0x083a, 0x0898,
	0x092c, 0x09cb, 0x0a3a, 0x0aeb, 0x0b67, 0x0c2c, 0x0cb6, 0x0d92,
	0x0e2d, 0x0f21, 0x1027, 0x10de, 0x1202, 0x12cf, 0x1414, 0x14f8,
	0x1662, 0x1761, 0x18f5, 0x1a11, 0x1bd3, 0x1db4, 0x1f06, 0x211d,
	0x2297, 0x24ec, 0x2690, 0x292a, 0x2aff, 0x2de5, 0x30fe, 0x332b,
	0x369f, 0x390d, 0x3ce6, 0x3f9b, 0x43e6, 0x46eb, 0x4bb3, 0x4f11,
	0x5466, 0x5a18, 0x5e19, 0x6472, 0x68ea, 0x6ffd, 0x74f8, 0x7cdc,
	0x826a, 0x8b35, 0x9499, 0x9b35, 0xa5ad, 0xad0b, 0xb8b7, 0xc0ee,
	0xcdf1, 0xd71a, 0xe59c, 0xefd3
};

static inline void scale_sample(signed short *buf, int i, int vol)
{
	int sample = buf[i];

	if (sample < 0) {
		sample = (sample * vol - SOFT_VOL_SCALE / 2) / SOFT_VOL_SCALE;
		if (sample < -32768)
			sample = -32768;
	} else {
		sample = (sample * vol + SOFT_VOL_SCALE / 2) / SOFT_VOL_SCALE;
		if (sample > 32767)
			sample = 32767;
	}
	buf[i] = sample;
}

static void scale_samples(char *buffer, unsigned int *countp)
{
	signed short *buf;
	unsigned int count = *countp;
	int ch, bits, l, r, i;

	BUG_ON(scale_pos < consumer_pos);

	if (consumer_pos != scale_pos) {
		unsigned int offs = scale_pos - consumer_pos;

		if (offs >= count)
			return;
		buffer += offs;
		count -= offs;
	}
	scale_pos += count;
	buf = (signed short *)buffer;

	if (replaygain_scale == 1.0 && soft_vol_l == 100 && soft_vol_r == 100)
		return;

	ch = sf_get_channels(buffer_sf);
	bits = sf_get_bits(buffer_sf);
	if (ch != 2 || bits != 16)
		return;

	l = SOFT_VOL_SCALE;
	r = SOFT_VOL_SCALE;
	if (soft_vol_l != 100)
		l = soft_vol_db[soft_vol_l];
	if (soft_vol_r != 100)
		r = soft_vol_db[soft_vol_r];

	l *= replaygain_scale;
	r *= replaygain_scale;

	for (i = 0; i < count / 4; i++) {
		scale_sample(buf, i * 2, l);
		scale_sample(buf, i * 2 + 1, r);
	}
}

static int parse_double(const char *str, double *val)
{
	char *end;

	*val = strtod(str, &end);
	return str == end;
}

static void update_rg_scale(void)
{
	const char *g, *p;
	double gain, peak, db, scale, limit;

	replaygain_scale = 1.0;
	if (!player_info.ti || !replaygain)
		return;

	if (replaygain == RG_TRACK) {
		g = comments_get_val(player_info.ti->comments, "replaygain_track_gain");
		p = comments_get_val(player_info.ti->comments, "replaygain_track_peak");
	} else {
		g = comments_get_val(player_info.ti->comments, "replaygain_album_gain");
		p = comments_get_val(player_info.ti->comments, "replaygain_album_peak");
	}

	if (!g || !p) {
		d_print("gain or peak not available\n");
		return;
	}
	if (parse_double(g, &gain) || parse_double(p, &peak)) {
		d_print("could not parse gain (%s) or peak (%s)\n", g, p);
		return;
	}
	if (peak < 0.05) {
		d_print("peak (%g) is too small\n", peak);
		return;
	}

	db = replaygain_preamp + gain;

	scale = pow(10.0, db / 20.0);
	replaygain_scale = scale;
	limit = 1.0 / peak;
	if (replaygain_limit && replaygain_scale > limit)
		replaygain_scale = limit;

	d_print("gain = %f, peak = %f, db = %f, scale = %f, limit = %f, replaygain_scale = %f\n",
			gain, peak, db, scale, limit, replaygain_scale);
}

static inline unsigned int buffer_second_size(void)
{
	return sf_get_second_size(buffer_sf);
}

static inline int get_next(struct track_info **ti)
{
	return player_cbs->get_next(ti);
}

/* updating player status {{{ */

static inline void file_changed(struct track_info *ti)
{
	player_info_lock();
	if (player_info.ti)
		track_info_unref(player_info.ti);

	player_info.ti = ti;
	if (ti) {
		d_print("file: %s\n", ti->filename);
	} else {
		d_print("unloaded\n");
	}
	update_rg_scale();
	player_info.metadata[0] = 0;
	player_info.file_changed = 1;
	player_info_unlock();
}

static inline void metadata_changed(void)
{
	player_info_lock();
	d_print("metadata changed: %s\n", ip_get_metadata(ip));
	memcpy(player_info.metadata, ip_get_metadata(ip), 255 * 16 + 1);
	player_info.metadata_changed = 1;
	player_info_unlock();
}

static inline void volume_update(int left, int right)
{
	if (player_info.vol_left == left && player_info.vol_right == right)
		return;

	player_info_lock();
	player_info.vol_left = left;
	player_info.vol_right = right;
	player_info.vol_changed = 1;
	player_info_unlock();
}

static void player_error(const char *msg)
{
	player_info_lock();
	player_info.status = consumer_status;
	player_info.pos = 0;
	player_info.buffer_fill = buffer_get_filled_chunks();
	player_info.buffer_size = buffer_nr_chunks;
	player_info.status_changed = 1;

	free(player_info.error_msg);
	player_info.error_msg = xstrdup(msg);
	player_info_unlock();

	d_print("ERROR: '%s'\n", msg);
}

static void __FORMAT(2, 3) player_ip_error(int rc, const char *format, ...)
{
	char buffer[1024];
	va_list ap;
	char *msg;
	int save = errno;

	va_start(ap, format);
	vsnprintf(buffer, sizeof(buffer), format, ap);
	va_end(ap);

	errno = save;
	msg = ip_get_error_msg(ip, rc, buffer);
	player_error(msg);
	free(msg);
}

static void __FORMAT(2, 3) player_op_error(int rc, const char *format, ...)
{
	char buffer[1024];
	va_list ap;
	char *msg;
	int save = errno;

	va_start(ap, format);
	vsnprintf(buffer, sizeof(buffer), format, ap);
	va_end(ap);

	errno = save;
	msg = op_get_error_msg(rc, buffer);
	player_error(msg);
	free(msg);
}

/* FIXME: don't poll */
static void mixer_check(void)
{
	static struct timeval old_t = { 0L, 0L };
	struct timeval t;
	long usec, sec;
	int l, r;

	gettimeofday(&t, NULL);
	usec = t.tv_usec - old_t.tv_usec;
	sec = t.tv_sec - old_t.tv_sec;
	if (sec) {
		/* multiplying sec with 1e6 can overflow */
		usec += 1e6L;
	}
	if (usec < 300e3)
		return;

	old_t = t;
	if (!op_get_volume(&l, &r))
		volume_update(l, r);
}

/*
 * buffer-fill changed
 */
static void __producer_buffer_fill_update(void)
{
	int fill;

	player_info_lock();
	fill = buffer_get_filled_chunks();
	if (fill != player_info.buffer_fill) {
/* 		d_print("\n"); */
		player_info.buffer_fill = fill;
		player_info.buffer_fill_changed = 1;
	}
	player_info_unlock();
}

/*
 * playing position changed
 */
static void __consumer_position_update(void)
{
	static unsigned int old_pos = -1;
	unsigned int pos = 0;
	
	if (consumer_status == CS_PLAYING || consumer_status == CS_PAUSED)
		pos = consumer_pos / buffer_second_size();
	if (pos != old_pos) {
/* 		d_print("\n"); */
		old_pos = pos;

		player_info_lock();
		player_info.pos = pos;
		player_info.position_changed = 1;
		player_info_unlock();
	}
}

/*
 * something big happened (stopped/paused/unpaused...)
 */
static void __player_status_changed(void)
{
	unsigned int pos = 0;

/* 	d_print("\n"); */
	if (consumer_status == CS_PLAYING || consumer_status == CS_PAUSED)
		pos = consumer_pos / buffer_second_size();

	player_info_lock();
	player_info.status = consumer_status;
	player_info.pos = pos;
	player_info.buffer_fill = buffer_get_filled_chunks();
	player_info.buffer_size = buffer_nr_chunks;
	player_info.status_changed = 1;
	player_info_unlock();
}

/* updating player status }}} */

static void __prebuffer(void)
{
	int limit_chunks;

	BUG_ON(producer_status != PS_PLAYING);
	if (ip_is_remote(ip)) {
		limit_chunks = buffer_nr_chunks;
	} else {
		int limit_ms, limit_size;

		limit_ms = 250;
		limit_size = limit_ms * buffer_second_size() / 1000;
		limit_chunks = limit_size / CHUNK_SIZE;
		if (limit_chunks < 1)
			limit_chunks = 1;
	}
	while (1) {
		int nr_read, size, filled;
		char *wpos;

		filled = buffer_get_filled_chunks();
/* 		d_print("PREBUF: %2d / %2d\n", filled, limit_chunks); */

		/* not fatal */
		//BUG_ON(filled > limit_chunks);

		if (filled >= limit_chunks)
			break;

		size = buffer_get_wpos(&wpos);
		nr_read = ip_read(ip, wpos, size);
		if (nr_read < 0) {
			if (nr_read == -1 && errno == EAGAIN)
				continue;
			player_ip_error(nr_read, "reading file %s", ip_get_filename(ip));
			/* ip_read sets eof */
			nr_read = 0;
		}
		if (ip_metadata_changed(ip))
			metadata_changed();

		/* buffer_fill with 0 count marks current chunk filled */
		buffer_fill(nr_read);

		__producer_buffer_fill_update();
		if (nr_read == 0) {
			/* EOF */
			break;
		}
	}
}

/* setting producer status {{{ */

static void __producer_play(void)
{
	if (producer_status == PS_UNLOADED) {
		struct track_info *ti;

		if (get_next(&ti) == 0) {
			int rc;

			ip = ip_new(ti->filename);
			rc = ip_open(ip);
			if (rc) {
				player_ip_error(rc, "opening file `%s'", ti->filename);
				ip_delete(ip);
				track_info_unref(ti);
				file_changed(NULL);
			} else {
				ip_setup(ip);
				producer_status = PS_PLAYING;
				file_changed(ti);
			}
		}
	} else if (producer_status == PS_PLAYING) {
		if (ip_seek(ip, 0.0) == 0) {
			reset_buffer();
		}
	} else if (producer_status == PS_STOPPED) {
		int rc;

		rc = ip_open(ip);
		if (rc) {
			player_ip_error(rc, "opening file `%s'", ip_get_filename(ip));
			ip_delete(ip);
			producer_status = PS_UNLOADED;
		} else {
			ip_setup(ip);
			producer_status = PS_PLAYING;
		}
	} else if (producer_status == PS_PAUSED) {
		producer_status = PS_PLAYING;
	}
}

static void __producer_stop(void)
{
	if (producer_status == PS_PLAYING || producer_status == PS_PAUSED) {
		ip_close(ip);
		producer_status = PS_STOPPED;
		reset_buffer();
	}
}

static void __producer_unload(void)
{
	__producer_stop();
	if (producer_status == PS_STOPPED) {
		ip_delete(ip);
		producer_status = PS_UNLOADED;
	}
}

static void __producer_pause(void)
{
	if (producer_status == PS_PLAYING) {
		producer_status = PS_PAUSED;
	} else if (producer_status == PS_PAUSED) {
		producer_status = PS_PLAYING;
	}
}

static void __producer_set_file(struct track_info *ti)
{
	__producer_unload();
	ip = ip_new(ti->filename);
	producer_status = PS_STOPPED;
	file_changed(ti);
}

/* setting producer status }}} */

/* setting consumer status {{{ */

static void __consumer_play(void)
{
	if (consumer_status == CS_PLAYING) {
		op_drop();
	} else if (consumer_status == CS_STOPPED) {
		int rc;

		set_buffer_sf(ip_get_sf(ip));
		rc = op_open(buffer_sf);
		if (rc) {
			player_op_error(rc, "opening audio device");
		} else {
			consumer_status = CS_PLAYING;
		}
	} else if (consumer_status == CS_PAUSED) {
		op_unpause();
		consumer_status = CS_PLAYING;
	}
}

static void __consumer_drain_and_stop(void)
{
	if (consumer_status == CS_PLAYING || consumer_status == CS_PAUSED) {
		op_close();
		consumer_status = CS_STOPPED;
	}
}

static void __consumer_stop(void)
{
	if (consumer_status == CS_PLAYING || consumer_status == CS_PAUSED) {
		op_drop();
		op_close();
		consumer_status = CS_STOPPED;
	}
}

static void __consumer_pause(void)
{
	if (consumer_status == CS_PLAYING) {
		op_pause();
		consumer_status = CS_PAUSED;
	} else if (consumer_status == CS_PAUSED) {
		op_unpause();
		consumer_status = CS_PLAYING;
	}
}

/* setting consumer status }}} */

static int change_sf(sample_format_t sf, int drop)
{
	int old_sf = buffer_sf;

	set_buffer_sf(sf);
	if (buffer_sf != old_sf) {
		/* reopen */
		int rc;

		if (drop)
			op_drop();
		op_close();
		rc = op_open(buffer_sf);
		if (rc) {
			player_op_error(rc, "opening audio device");
			consumer_status = CS_STOPPED;
			__producer_stop();
			return rc;
		}
	} else if (consumer_status == CS_PAUSED) {
		op_drop();
		op_unpause();
	}
	consumer_status = CS_PLAYING;
	return 0;
}

static void __consumer_handle_eof(void)
{
	struct track_info *ti;

	if (ip_is_remote(ip)) {
		__producer_stop();
		__consumer_drain_and_stop();
		player_error("lost connection");
		return;
	}

	if (get_next(&ti) == 0) {
		__producer_unload();
		ip = ip_new(ti->filename);
		producer_status = PS_STOPPED;
		/* PS_STOPPED, CS_PLAYING */
		if (player_cont) {
			__producer_play();
			if (producer_status == PS_UNLOADED) {
				__consumer_stop();
				track_info_unref(ti);
				file_changed(NULL);
			} else {
				/* PS_PLAYING */
				file_changed(ti);
				if (!change_sf(ip_get_sf(ip), 0))
					__prebuffer();
			}
		} else {
			__consumer_drain_and_stop();
			file_changed(ti);
		}
	} else {
		__producer_unload();
		__consumer_drain_and_stop();
		file_changed(NULL);
	}
	__player_status_changed();
}

static void *consumer_loop(void *arg)
{
	while (1) {
		int rc, space;
		int size;
		char *rpos;

		consumer_lock();
		if (!consumer_running)
			break;

		if (consumer_status == CS_PAUSED || consumer_status == CS_STOPPED) {
			mixer_check();
			consumer_unlock();
			ms_sleep(50);
			continue;
		}
		space = op_buffer_space();
		if (space == -1) {
			/* busy */
			__consumer_position_update();
			consumer_unlock();
			ms_sleep(50);
			continue;
		}
/* 		d_print("BS: %6d %3d\n", space, space * 1000 / (44100 * 2 * 2)); */

		while (1) {
			/* 25 ms is 4410 B */
			if (space < 4096) {
				__consumer_position_update();
				mixer_check();
				consumer_unlock();
				ms_sleep(25);
				break;
			}
			size = buffer_get_rpos(&rpos);
			if (size == 0) {
				producer_lock();
				if (producer_status != PS_PLAYING) {
					producer_unlock();
					consumer_unlock();
					break;
				}
				/* must recheck rpos */
				size = buffer_get_rpos(&rpos);
				if (size == 0) {
					/* OK. now it's safe to check if we are at EOF */
					if (ip_eof(ip)) {
						/* EOF */
						__consumer_handle_eof();
						producer_unlock();
						consumer_unlock();
						break;
					} else {
						/* possible underrun */
						producer_unlock();
						__consumer_position_update();
						consumer_unlock();
/* 						d_print("possible underrun\n"); */
						ms_sleep(10);
						break;
					}
				}

				/* player_buffer and ip.eof were inconsistent */
				producer_unlock();
			}
			if (size > space)
				size = space;
			if (soft_vol || replaygain)
				scale_samples(rpos, &size);
			rc = op_write(rpos, size);
			if (rc < 0) {
				d_print("op_write returned %d %s\n", rc,
						rc == -1 ? strerror(errno) : "");

				/* try to reopen */
				op_close();
				consumer_status = CS_STOPPED;
				__consumer_play();

				consumer_unlock();
				break;
			}
			buffer_consume(rc);
			consumer_pos += rc;
			space -= rc;
		}
	}
	__consumer_stop();
	consumer_unlock();
	return NULL;
}

static void *producer_loop(void *arg)
{
	while (1) {
		/* number of chunks to fill
		 * too big   => seeking is slow
		 * too small => underruns?
		 */
		const int chunks = 1;
		int size, nr_read, i;
		char *wpos;

		producer_lock();
		if (!producer_running)
			break;

		if (producer_status == PS_UNLOADED ||
		    producer_status == PS_PAUSED ||
		    producer_status == PS_STOPPED || ip_eof(ip)) {
			producer_unlock();
			ms_sleep(50);
			continue;
		}
		for (i = 0; ; i++) {
			size = buffer_get_wpos(&wpos);
			if (size == 0) {
				/* buffer is full */
				producer_unlock();
				ms_sleep(50);
				break;
			}
			nr_read = ip_read(ip, wpos, size);
			if (nr_read < 0) {
				if (nr_read != -1 || errno != EAGAIN) {
					player_ip_error(nr_read, "reading file %s",
							ip_get_filename(ip));
					/* ip_read sets eof */
					nr_read = 0;
				} else {
					producer_unlock();
					ms_sleep(50);
					break;
				}
			}
			if (ip_metadata_changed(ip))
				metadata_changed();

			/* buffer_fill with 0 count marks current chunk filled */
			buffer_fill(nr_read);
			if (nr_read == 0) {
				/* consumer handles EOF */
				producer_unlock();
				ms_sleep(50);
				break;
			}
			if (i == chunks) {
				producer_unlock();
				/* don't sleep! */
				break;
			}
		}
		__producer_buffer_fill_update();
	}
	__producer_unload();
	producer_unlock();
	return NULL;
}

void player_load_plugins(void)
{
	ip_load_plugins();
	op_load_plugins();
}

void player_init(const struct player_callbacks *callbacks)
{
	int rc;
#if defined(__linux__) || defined(__FreeBSD__)
	pthread_attr_t attr;
#endif
	pthread_attr_t *attrp = NULL;

	/*  1 s is 176400 B (0.168 MB)
	 * 10 s is 1.68 MB
	 */
	buffer_nr_chunks = 10 * 44100 * 16 / 8 * 2 / CHUNK_SIZE;
	buffer_init();

	player_cbs = callbacks;

#if defined(__linux__) || defined(__FreeBSD__)
	rc = pthread_attr_init(&attr);
	BUG_ON(rc);
	rc = pthread_attr_setschedpolicy(&attr, SCHED_RR);
	if (rc) {
		d_print("could not set real-time scheduling priority: %s\n", strerror(rc));
	} else {
		struct sched_param param;

		d_print("using real-time scheduling\n");
		param.sched_priority = sched_get_priority_max(SCHED_RR);
		d_print("setting priority to %d\n", param.sched_priority);
		rc = pthread_attr_setschedparam(&attr, &param);
		BUG_ON(rc);
		attrp = &attr;
	}
#endif

	rc = pthread_create(&producer_thread, NULL, producer_loop, NULL);
	BUG_ON(rc);

	rc = pthread_create(&consumer_thread, attrp, consumer_loop, NULL);
	if (rc && attrp) {
		d_print("could not create thread using real-time scheduling: %s\n", strerror(rc));
		rc = pthread_create(&consumer_thread, NULL, consumer_loop, NULL);
	}
	BUG_ON(rc);

	/* update player_info.cont etc. */
	player_lock();
	__player_status_changed();
	player_unlock();
}

void player_exit(void)
{
	int rc;

	player_lock();
	consumer_running = 0;
	producer_running = 0;
	player_unlock();
	
	rc = pthread_join(consumer_thread, NULL);
	BUG_ON(rc);
	rc = pthread_join(producer_thread, NULL);
	BUG_ON(rc);

	op_exit_plugins();
}

void player_stop(void)
{
	player_lock();
	__consumer_stop();
	__producer_stop();
	__player_status_changed();
	player_unlock();
}

void player_play(void)
{
	int prebuffer;

	player_lock();
	if (producer_status == PS_PLAYING && ip_is_remote(ip)) {
		/* seeking not allowed */
		player_unlock();
		return;
	}
	prebuffer = consumer_status == CS_STOPPED;
	__producer_play();
	if (producer_status == PS_PLAYING) {
		__consumer_play();
		if (consumer_status != CS_PLAYING)
			__producer_stop();
	} else {
		__consumer_stop();
	}
	__player_status_changed();
	if (consumer_status == CS_PLAYING && prebuffer)
		__prebuffer();
	player_unlock();
}

void player_pause(void)
{
	player_lock();

	if (consumer_status == CS_STOPPED) {
		__producer_play();
		if (producer_status == PS_PLAYING) {
			__consumer_play();
			if (consumer_status != CS_PLAYING)
				__producer_stop();
		}
		__player_status_changed();
		if (consumer_status == CS_PLAYING)
			__prebuffer();
		player_unlock();
		return;
	}

	if (ip && ip_is_remote(ip)) {
		/* pausing not allowed */
		player_unlock();
		return;
	}
	__producer_pause();
	__consumer_pause();
	__player_status_changed();
	player_unlock();
}

void player_set_file(struct track_info *ti)
{
	player_lock();
	__producer_set_file(ti);
	if (producer_status == PS_UNLOADED) {
		__consumer_stop();
		goto out;
	}

	/* PS_STOPPED */
	if (consumer_status == CS_PLAYING || consumer_status == CS_PAUSED) {
		__producer_play();
		if (producer_status == PS_UNLOADED) {
			__consumer_stop();
			goto out;
		}
		change_sf(ip_get_sf(ip), 1);
	}
out:
	__player_status_changed();
	if (producer_status == PS_PLAYING)
		__prebuffer();
	player_unlock();
}

void player_play_file(struct track_info *ti)
{
	player_lock();
	__producer_set_file(ti);
	if (producer_status == PS_UNLOADED) {
		__consumer_stop();
		goto out;
	}

	/* PS_STOPPED */
	__producer_play();

	/* PS_UNLOADED,PS_PLAYING */
	if (producer_status == PS_UNLOADED) {
		__consumer_stop();
		goto out;
	}

	/* PS_PLAYING */
	if (consumer_status == CS_STOPPED) {
		__consumer_play();
		if (consumer_status == CS_STOPPED)
			__producer_stop();
	} else {
		change_sf(ip_get_sf(ip), 1);
	}
out:
	__player_status_changed();
	if (producer_status == PS_PLAYING)
		__prebuffer();
	player_unlock();
}

void player_seek(double offset, int relative)
{
	player_lock();
	if (consumer_status == CS_PLAYING || consumer_status == CS_PAUSED) {
		double pos, duration, new_pos;
		int rc;

		pos = (double)consumer_pos / (double)buffer_second_size();
		duration = ip_duration(ip);
		if (duration < 0) {
			/* can't seek */
			d_print("can't seek\n");
			player_unlock();
			return;
		}
		if (relative) {
			new_pos = pos + offset;
			if (new_pos < 0.0)
				new_pos = 0.0;
			if (offset > 0.0) {
				/* seeking forward */
				if (new_pos > duration - 5.0)
					new_pos = duration - 5.0;
				if (new_pos < 0.0)
					new_pos = 0.0;
				if (new_pos < pos - 0.5) {
					/* must seek at least 0.5s */
					d_print("must seek at least 0.5s\n");
					player_unlock();
					return;
				}
			}
		} else {
			new_pos = offset;
			if (new_pos < 0.0) {
				d_print("seek offset negative\n");
				player_unlock();
				return;
			}
			if (new_pos > duration) {
				d_print("seek offset too large\n");
				player_unlock();
				return;
			}
		}
/* 		d_print("seeking %g/%g (%g from eof)\n", new_pos, duration, duration - new_pos); */
		rc = ip_seek(ip, new_pos);
		if (rc == 0) {
/* 			d_print("doing op_drop after seek\n"); */
			op_drop();
			reset_buffer();
			consumer_pos = new_pos * buffer_second_size();
			scale_pos = consumer_pos;
			__consumer_position_update();
		} else {
			d_print("error: ip_seek returned %d\n", rc);
		}
	}
	player_unlock();
}

/*
 * change output plugin without stopping playback
 */
void player_set_op(const char *name)
{
	int rc, l, r;

	player_lock();

	/* drop needed because close drains the buffer */
	if (consumer_status == CS_PAUSED)
		op_drop();

	if (consumer_status == CS_PLAYING || consumer_status == CS_PAUSED)
		op_close();

	if (name) {
		d_print("setting op to '%s'\n", name);
		rc = op_select(name);
	} else {
		/* first initialized plugin */
		d_print("selecting first initialized op\n");
		rc = op_select_any();
	}
	if (rc) {
		consumer_status = CS_STOPPED;

		__producer_stop();
		player_op_error(rc, "selecting output plugin '%s'", name);
		player_unlock();
		return;
	}

	if (consumer_status == CS_PLAYING || consumer_status == CS_PAUSED) {
		set_buffer_sf(ip_get_sf(ip));
		rc = op_open(buffer_sf);
		if (rc) {
			consumer_status = CS_STOPPED;
			__producer_stop();
			player_op_error(rc, "opening audio device");
			player_unlock();
			return;
		}
		if (consumer_status == CS_PAUSED)
			op_pause();
	}

	if (!op_get_volume(&l, &r))
		volume_update(l, r);

	player_unlock();
}

char *player_get_op(void)
{
	return op_get_current();
}

void player_set_buffer_chunks(unsigned int nr_chunks)
{
	if (nr_chunks < 3)
		nr_chunks = 3;
	if (nr_chunks > 30)
		nr_chunks = 30;

	player_lock();
	__producer_stop();
	__consumer_stop();

	buffer_nr_chunks = nr_chunks;
	buffer_init();

	__player_status_changed();
	player_unlock();
}

int player_get_buffer_chunks(void)
{
	return buffer_nr_chunks;
}

int player_get_fileinfo(const char *filename, int *duration,
		struct keyval **comments)
{
	struct input_plugin *plug;
	int rc;

	*comments = NULL;
	*duration = -1;
	plug = ip_new(filename);
	if (ip_is_remote(plug)) {
		*comments = xnew0(struct keyval, 1);
		ip_delete(plug);
		return 0;
	}
	rc = ip_open(plug);
	if (rc) {
		int save = errno;

		ip_delete(plug);
		errno = save;
		if (rc != -1)
			rc = -PLAYER_ERROR_NOT_SUPPORTED;
		return rc;
	}
	*duration = ip_duration(plug);
	rc = ip_read_comments(plug, comments);
	ip_delete(plug);
	return rc;
}

int player_get_volume(int *left, int *right)
{
	int rc;

	consumer_lock();
	rc = op_get_volume(left, right);
	consumer_unlock();
	return rc;
}

int player_set_volume(int left, int right)
{
	int rc;

	consumer_lock();
	rc = op_set_volume(left, right);
	if (!rc)
		volume_update(left, right);
	consumer_unlock();
	return rc;
}

void player_set_soft_vol(int soft)
{
	int l, r;

	consumer_lock();
	/* don't mess with scale_pos if soft_vol or replaygain is already enabled */
	if (!soft_vol && !replaygain)
		scale_pos = consumer_pos;
	op_set_soft_vol(soft);
	if (!op_get_volume(&l, &r))
		volume_update(l, r);
	consumer_unlock();
}

void player_set_rg(enum replaygain rg)
{
	player_lock();
	/* don't mess with scale_pos if soft_vol or replaygain is already enabled */
	if (!soft_vol && !replaygain)
		scale_pos = consumer_pos;
	replaygain = rg;

	player_info_lock();
	update_rg_scale();
	player_info_unlock();

	player_unlock();
}

void player_set_rg_limit(int limit)
{
	player_lock();
	replaygain_limit = limit;

	player_info_lock();
	update_rg_scale();
	player_info_unlock();

	player_unlock();
}

void player_set_rg_preamp(double db)
{
	player_lock();
	replaygain_preamp = db;

	player_info_lock();
	update_rg_scale();
	player_info_unlock();

	player_unlock();
}

int player_set_op_option(unsigned int id, const char *val)
{
	int rc;

	player_lock();
	__consumer_stop();
	__producer_stop();
	rc = op_set_option(id, val);
	__player_status_changed();
	player_unlock();
	return rc;
}

int player_get_op_option(unsigned int id, char **val)
{
	int rc;

	player_lock();
	rc = op_get_option(id, val);
	player_unlock();
	return rc;
}

int player_for_each_op_option(void (*callback)(unsigned int id, const char *key))
{
	player_lock();
	__consumer_stop();
	__producer_stop();
	op_for_each_option(callback);
	__player_status_changed();
	player_unlock();
	return 0;
}

void player_dump_plugins(void)
{
	ip_dump_plugins();
	op_dump_plugins();
}
