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

#include <player.h>
#include <buffer.h>
#include <input.h>
#include <output.h>
#include <sf.h>
#include <utils.h>
#include <xmalloc.h>
#include <debug.h>
#include <compiler.h>

#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <stdarg.h>

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
	.filename = { 0, },
	.metadata = { 0, },
	.status = PLAYER_STATUS_STOPPED,
	.pos = 0,
	.cont = 0,
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
	.volume_changed = 0,
};

static const struct player_callbacks *player_cbs = NULL;

/* continue playing after track is finished? */
static int player_cont = 1;

static struct buffer player_buffer;
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
static int consumer_pos = 0;

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
	buffer_reset(&player_buffer);
	consumer_pos = 0;
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

static inline int buffer_second_size(void)
{
	return sf_get_second_size(buffer_sf);
}

static inline int get_next(char **filename)
{
	return player_cbs->get_next(filename);
}

/* updating player status {{{ */

static inline void file_changed(void)
{
	player_info_lock();
	if (producer_status == PS_UNLOADED) {
		player_info.filename[0] = 0;
	} else {
		strncpy(player_info.filename, ip_get_filename(ip), sizeof(player_info.filename));
		player_info.filename[sizeof(player_info.filename) - 1] = 0;
	}
	d_print("%s\n", player_info.filename);
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

static inline void volume_changed(int left, int right)
{
	player_info_lock();
	player_info.vol_left = left;
	player_info.vol_right = right;
	player_info.volume_changed = 1;
	player_info_unlock();
}

static void player_error(const char *msg)
{
	player_info_lock();
	player_info.status = consumer_status;
	player_info.pos = 0;
	player_info.buffer_fill = buffer_get_filled_chunks(&player_buffer);
	player_info.buffer_size = buffer_get_nr_chunks(&player_buffer);
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
	static struct timeval old_st = { 0L, 0L };
	struct timeval st;
	int l, r, rc;

	gettimeofday(&st, NULL);
	if (st.tv_sec == old_st.tv_sec) {
		unsigned long usecs = st.tv_sec - old_st.tv_sec;

		if (usecs < 300e6)
			return;
	}
	old_st = st;
	rc = op_volume_changed(&l, &r);
	if (rc == 1)
		volume_changed(l, r);
}

/*
 * buffer-fill changed
 */
static void __producer_buffer_fill_update(void)
{
	int fill;

	player_info_lock();
	fill = buffer_get_filled_chunks(&player_buffer);
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
	static int old_pos = -1;
	int pos;
	
	pos = 0;
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

static void __player_cont_update(void)
{
	player_info_lock();
	if (player_info.cont != player_cont) {
/* 		d_print("\n"); */
		player_info.cont = player_cont;
		// FIXME
		player_info.status_changed = 1;
	}
	player_info_unlock();
}

/*
 * something big happened (stopped/paused/unpaused...)
 */
static void __player_status_changed(void)
{
	int pos = 0;

/* 	d_print("\n"); */
	if (consumer_status == CS_PLAYING || consumer_status == CS_PAUSED)
		pos = consumer_pos / buffer_second_size();

	player_info_lock();
	player_info.status = consumer_status;
	player_info.pos = pos;
	player_info.cont = player_cont;
	player_info.buffer_fill = buffer_get_filled_chunks(&player_buffer);
	player_info.buffer_size = buffer_get_nr_chunks(&player_buffer);
	player_info.status_changed = 1;
	player_info_unlock();
}

/* updating player status }}} */

static void __prebuffer(void)
{
	int limit_chunks;

	BUG_ON(producer_status != PS_PLAYING);
	if (ip_is_remote(ip)) {
		limit_chunks = buffer_get_nr_chunks(&player_buffer);
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

/* 		buffer_debug(&player_buffer); */
		filled = buffer_get_filled_chunks(&player_buffer);
/* 		d_print("PREBUF: %2d / %2d\n", filled, limit_chunks); */

		/* not fatal */
		//BUG_ON(filled > limit_chunks);

		if (filled >= limit_chunks)
			break;

		buffer_get_wpos(&player_buffer, &wpos, &size);
		nr_read = ip_read(ip, wpos, size);
		if (nr_read < 0) {
			if (nr_read == -1 && errno == EAGAIN)
				continue;
			player_ip_error(nr_read, "reading file %s", ip_get_filename(ip));
			ip_delete(ip);
			producer_status = PS_UNLOADED;
			return;
		}
		if (ip_metadata_changed(ip))
			metadata_changed();

		/* buffer_fill with 0 count marks current chunk filled */
		buffer_fill(&player_buffer, nr_read);

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
		char *filename;

		if (get_next(&filename) == 0) {
			int rc;

			ip = ip_new(filename);
			rc = ip_open(ip);
			if (rc) {
				player_ip_error(rc, "opening file `%s'", filename);
				ip_delete(ip);
			} else {
				producer_status = PS_PLAYING;
			}
			free(filename);
			file_changed();
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

static void __producer_set_file(const char *filename)
{
	__producer_unload();
	ip = ip_new(filename);
	producer_status = PS_STOPPED;
	file_changed();
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

static void __consumer_handle_eof(void)
{
	char *filename;

	if (ip_is_remote(ip)) {
		__producer_stop();
		__consumer_drain_and_stop();
		player_error("lost connection");
		return;
	}

	if (get_next(&filename) == 0) {
		int rc;

		__producer_unload();
		ip = ip_new(filename);
		producer_status = PS_STOPPED;
		/* PS_STOPPED, CS_PLAYING */
		if (player_cont) {
			__producer_play();
			if (producer_status == PS_UNLOADED) {
				__consumer_stop();
				file_changed();
			} else {
				/* PS_PLAYING */
				set_buffer_sf(ip_get_sf(ip));
				rc = op_set_sf(buffer_sf);
				if (rc < 0) {
					__producer_stop();
					consumer_status = CS_STOPPED;
					player_op_error(rc, "setting sample format");
					file_changed();
				} else {
					file_changed();
					__prebuffer();
				}
			}
		} else {
			__consumer_drain_and_stop();
			file_changed();
		}
		free(filename);
	} else {
		__producer_unload();
		__consumer_drain_and_stop();
		file_changed();
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
			buffer_get_rpos(&player_buffer, &rpos, &size);
			if (size == 0) {
				producer_lock();
				if (producer_status != PS_PLAYING) {
					producer_unlock();
					consumer_unlock();
					break;
				}
				/* must recheck rpos */
				buffer_get_rpos(&player_buffer, &rpos, &size);
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
			rc = op_write(rpos, size);
			if (rc < 0) {
				d_print("op_write returned %d %s\n", rc, rc == -1 ? strerror(errno) : "");
				consumer_unlock();
				break;
			}
			buffer_consume(&player_buffer, rc);
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
			buffer_get_wpos(&player_buffer, &wpos, &size);
			if (size == 0) {
				/* buffer is full */
				producer_unlock();
				ms_sleep(50);
				break;
			}
			nr_read = ip_read(ip, wpos, size);
			if (nr_read < 0) {
				if (nr_read != -1 || errno != EAGAIN) {
					player_ip_error(nr_read, "reading file %s", ip_get_filename(ip));
					ip_delete(ip);
					producer_status = PS_UNLOADED;
				}
				producer_unlock();
				ms_sleep(50);
				break;
			}
			if (ip_metadata_changed(ip))
				metadata_changed();

			/* buffer_fill with 0 count marks current chunk filled */
			buffer_fill(&player_buffer, nr_read);
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
	__producer_stop();
	if (producer_status != PS_UNLOADED) {
		ip_delete(ip);
		producer_status = PS_UNLOADED;
	}
	producer_unlock();
	return NULL;
}

void player_init_plugins(void)
{
	ip_init_plugins();
	op_init_plugins();
}

int player_init(const struct player_callbacks *callbacks)
{
	int rc, nr_chunks;
	pthread_attr_t attr, *attrp;

	/*  1 s is 176400 B (0.168 MB)
	 * 10 s is 1.68 MB
	 */
	nr_chunks = 10 * 44100 * 16 / 8 * 2 / CHUNK_SIZE;
	buffer_init(&player_buffer, nr_chunks);

	player_cbs = callbacks;
	rc = op_init();
	if (rc) {
		int save;

		save = errno;
		buffer_free(&player_buffer);
		errno = save;
		return rc;
	}

	rc = pthread_attr_init(&attr);
	BUG_ON(rc);
	rc = pthread_attr_setschedpolicy(&attr, SCHED_RR);
	if (rc) {
		d_print("could not set real-time scheduling priority: %s\n", strerror(rc));
		attrp = NULL;
	} else {
		struct sched_param param;

		d_print("using real-time scheduling\n");
		param.sched_priority = sched_get_priority_max(SCHED_RR);
		d_print("setting priority to %d\n", param.sched_priority);
		rc = pthread_attr_setschedparam(&attr, &param);
		BUG_ON(rc);
		attrp = &attr;
	}

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
	return 0;
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

	op_exit();
	buffer_free(&player_buffer);
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
	if (ip_is_remote(ip)) {
		/* pausing not allowed */
		player_unlock();
		return;
	}
	__producer_pause();
	__consumer_pause();
	__player_status_changed();
	player_unlock();
}

void player_set_file(const char *filename)
{
	int rc;

	player_lock();
	__producer_set_file(filename);
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

		/* must do op_drop() here because op_set_sf()
		 * might call op_close() which calls drain()
		 */
		op_drop();

		set_buffer_sf(ip_get_sf(ip));
		rc = op_set_sf(buffer_sf);
		if (rc == 0) {
			/* device wasn't reopened */
			if (consumer_status == CS_PAUSED) {
				/* status was paused => need to unpause */
				rc = op_unpause();
				d_print("op_unpause: %d\n", rc);
			}
			consumer_status = CS_PLAYING;
		} else if (rc == 1) {
			/* device was reopened */
			consumer_status = CS_PLAYING;
		} else {
			__producer_stop();
			player_op_error(rc, "setting sample format");
			consumer_status = CS_STOPPED;
		}
	}
out:
	__player_status_changed();
	if (producer_status == PS_PLAYING)
		__prebuffer();
	player_unlock();
}

void player_play_file(const char *filename)
{
	int rc;

	player_lock();
	__producer_set_file(filename);
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
		/* PS_PLAYING, CS_PLAYING,CS_PAUSED */
		/* must do op_drop() here because op_set_sf()
		 * might call op_close() which calls drain()
		 */
		op_drop();

		set_buffer_sf(ip_get_sf(ip));
		rc = op_set_sf(buffer_sf);
		if (rc == 0) {
			/* device wasn't reopened */
			if (consumer_status == CS_PAUSED) {
				/* status was paused => need to unpause */
				rc = op_unpause();
				d_print("op_unpause: %d\n", rc);
			}
			consumer_status = CS_PLAYING;
		} else if (rc == 1) {
			/* device was reopened */
			consumer_status = CS_PLAYING;
		} else {
			__producer_stop();
			player_op_error(rc, "setting sample format");
			consumer_status = CS_STOPPED;
		}
	}
out:
	__player_status_changed();
	if (producer_status == PS_PLAYING)
		__prebuffer();
	player_unlock();
}

void player_seek(double offset, int whence)
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
		if (whence == SEEK_CUR) {
			/* relative to current position */
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
			/* absolute position */
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
int player_set_op(const char *name)
{
	int rc;

	d_print("setting op to '%s'\n", name);
	player_lock();

	/* drop needed because close drains the buffer */
	if (consumer_status == CS_PAUSED)
		op_drop();

	if (consumer_status == CS_PLAYING || consumer_status == CS_PAUSED)
		op_close();

	rc = op_select(name);
	if (rc) {
		consumer_status = CS_STOPPED;

		__producer_stop();
		player_op_error(rc, "selecting output plugin '%s'", name);
		player_unlock();
		return rc;
	}

	if (consumer_status == CS_PLAYING || consumer_status == CS_PAUSED) {
		set_buffer_sf(ip_get_sf(ip));
		rc = op_open(buffer_sf);
		if (rc) {
			consumer_status = CS_STOPPED;
			__producer_stop();
			player_op_error(rc, "opening audio device");
			player_unlock();
			return rc;
		}
		if (consumer_status == CS_PAUSED)
			op_pause();
	}
	__player_status_changed();
	player_unlock();
	d_print("rc = %d\n", rc);
	return rc;
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
	buffer_resize(&player_buffer, nr_chunks);
	__player_status_changed();
	player_unlock();
}

int player_get_buffer_chunks(void)
{
	int nr_chunks;

	player_lock();
	nr_chunks = player_buffer.nr_chunks;
	player_unlock();
	return nr_chunks;
}

void player_toggle_cont(void)
{
	player_lock();
	player_cont = player_cont ^ 1;
	__player_cont_update();
	player_unlock();
}

void player_set_cont(int value)
{
	player_lock();
	player_cont = value;
	__player_cont_update();
	player_unlock();
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
	int l, r, rc;

	consumer_lock();
	rc = op_get_volume(&l, &r);
	consumer_unlock();
	if (left)
		*left = l;
	if (right)
		*right = r;
	return rc;
}

int player_set_volume(int left, int right)
{
	int rc;

	left = clamp(left, 0, 100);
	right = clamp(right, 0, 100);
	consumer_lock();
	rc = op_set_volume(&left, &right);
	consumer_unlock();
	if (rc == 0)
		volume_changed(left, right);
	return rc;
}

int player_add_volume(int left, int right)
{
	int rc;

	consumer_lock();
	rc = op_add_volume(&left, &right);
	consumer_unlock();
	if (rc)
		d_print("rc = %d\n", rc);
	volume_changed(left, right);
	return rc;
}

int player_set_op_option(const char *key, const char *val)
{
	int rc;

	player_lock();
	__consumer_stop();
	__producer_stop();
	rc = op_set_option(key, val);
	__player_status_changed();
	player_unlock();
	return rc;
}

int player_get_op_option(const char *key, char **val)
{
	int rc;

	player_lock();
	rc = op_get_option(key, val);
	player_unlock();
	return rc;
}

int player_for_each_op_option(void (*callback)(void *data, const char *key), void *data)
{
	player_lock();
	__consumer_stop();
	__producer_stop();
	op_for_each_option(callback, data);
	__player_status_changed();
	player_unlock();
	return 0;
}

char **player_get_supported_extensions(void)
{
	return ip_get_supported_extensions();
}

void player_dump_plugins(void)
{
	ip_dump_plugins();
	op_dump_plugins();
}
