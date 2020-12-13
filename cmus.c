/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2004 Timo Hirvonen
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

#include "cmus.h"
#include "job.h"
#include "lib.h"
#include "pl.h"
#include "player.h"
#include "input.h"
#include "play_queue.h"
#include "cache.h"
#include "misc.h"
#include "file.h"
#include "utils.h"
#include "path.h"
#include "options.h"
#include "xmalloc.h"
#include "debug.h"
#include "load_dir.h"
#include "ui_curses.h"
#include "cache.h"
#include "gbuf.h"
#include "discid.h"
#include "locking.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>

/* save_playlist_cb, save_ext_playlist_cb */
typedef int (*save_tracks_cb)(void *data, struct track_info *ti);

static char **playable_exts;
static const char * const playlist_exts[] = { "m3u", "pl", "pls", NULL };

int cmus_next_track_request_fd;
static bool play_queue_active = false;
static int cmus_next_track_request_fd_priv;
static pthread_mutex_t cmus_next_file_mutex = CMUS_MUTEX_INITIALIZER;
static pthread_cond_t cmus_next_file_cond = CMUS_COND_INITIALIZER;
static int cmus_next_file_provided;
static struct track_info *cmus_next_file;

static int x11_init_done = 0;
static void *(*x11_open)(void *) = NULL;
static int (*x11_raise)(void *, int) = NULL;
static int (*x11_close)(void *) = NULL;

int cmus_init(void)
{
	playable_exts = ip_get_supported_extensions();
	cache_init();
	job_init();
	play_queue_init();
	return 0;
}

void cmus_exit(void)
{
	job_exit();
	if (cache_close())
		d_print("error: %s\n", strerror(errno));
}

void cmus_next(void)
{
	struct track_info *info = cmus_get_next_track();
	if (info)
		player_set_file(info);
}

void cmus_prev(void)
{
	struct track_info *info;

	if (play_library) {
		info = lib_goto_prev();
	} else {
		info = pl_goto_prev();
	}

	if (info)
		player_set_file(info);
}

void cmus_play_file(const char *filename)
{
	struct track_info *ti;

	cache_lock();
	ti = cache_get_ti(filename, 0);
	cache_unlock();
	if (!ti) {
		error_msg("Couldn't get file information for %s\n", filename);
		return;
	}

	player_play_file(ti);
}

enum file_type cmus_detect_ft(const char *name, char **ret)
{
	char *absolute;
	struct stat st;

	if (is_http_url(name) || is_cue_url(name)) {
		*ret = xstrdup(name);
		return FILE_TYPE_URL;
	}

	if (is_cdda_url(name)) {
		*ret = complete_cdda_url(cdda_device, name);
		return FILE_TYPE_CDDA;
	}

	*ret = NULL;
	absolute = path_absolute(name);
	if (absolute == NULL)
		return FILE_TYPE_INVALID;

	/* stat follows symlinks, lstat does not */
	if (stat(absolute, &st) == -1) {
		free(absolute);
		return FILE_TYPE_INVALID;
	}

	if (S_ISDIR(st.st_mode)) {
		*ret = absolute;
		return FILE_TYPE_DIR;
	}
	if (!S_ISREG(st.st_mode)) {
		free(absolute);
		errno = EINVAL;
		return FILE_TYPE_INVALID;
	}

	*ret = absolute;
	if (cmus_is_playlist(absolute))
		return FILE_TYPE_PL;

	/* NOTE: it could be FILE_TYPE_PL too! */
	return FILE_TYPE_FILE;
}

void cmus_add(add_ti_cb add, const char *name, enum file_type ft, int jt, int force,
		void *opaque)
{
	struct add_data *data = xnew(struct add_data, 1);

	data->add = add;
	data->name = xstrdup(name);
	data->type = ft;
	data->force = force;
	data->opaque = opaque;

	job_schedule_add(jt, data);
}

static int save_ext_playlist_cb(void *data, struct track_info *ti)
{
	GBUF(buf);
	int fd = *(int *)data;
	int i, rc;

	gbuf_addf(&buf, "file %s\n", escape(ti->filename));
	gbuf_addf(&buf, "duration %d\n", ti->duration);
	gbuf_addf(&buf, "codec %s\n", ti->codec);
	gbuf_addf(&buf, "bitrate %ld\n", ti->bitrate);
	for (i = 0; ti->comments[i].key; i++)
		gbuf_addf(&buf, "tag %s %s\n",
				ti->comments[i].key,
				escape(ti->comments[i].val));

	rc = write_all(fd, buf.buffer, buf.len);
	gbuf_free(&buf);

	if (rc == -1)
		return -1;
	return 0;
}

static int save_playlist_cb(void *data, struct track_info *ti)
{
	int fd = *(int *)data;
	const char nl = '\n';
	int rc;

	rc = write_all(fd, ti->filename, strlen(ti->filename));
	if (rc == -1)
		return -1;
	rc = write_all(fd, &nl, 1);
	if (rc == -1)
		return -1;
	return 0;
}

static int do_cmus_save(for_each_ti_cb for_each_ti, const char *filename,
		save_tracks_cb save_tracks, void *opaque)
{
	int fd, rc;

	if (strcmp(filename, "-") == 0) {
		if (get_client_fd() == -1) {
			error_msg("saving to stdout works only remotely");
			return 0;
		}
		fd = dup(get_client_fd());
	} else
		fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (fd == -1)
		return -1;
	rc = for_each_ti(save_tracks, &fd, opaque);
	close(fd);
	return rc;
}

int cmus_save(for_each_ti_cb for_each_ti, const char *filename, void *opaque)
{
	return do_cmus_save(for_each_ti, filename, save_playlist_cb, opaque);
}

int cmus_save_ext(for_each_ti_cb for_each_ti, const char *filename,
		void *opaque)
{
	return do_cmus_save(for_each_ti, filename, save_ext_playlist_cb,
			opaque);
}

static int update_cb(void *data, struct track_info *ti)
{
	struct update_data *d = data;

	if (d->size == d->used) {
		if (d->size == 0)
			d->size = 16;
		d->size *= 2;
		d->ti = xrenew(struct track_info *, d->ti, d->size);
	}
	track_info_ref(ti);
	d->ti[d->used++] = ti;
	return 0;
}

void cmus_update_cache(int force)
{
	struct update_cache_data *data;

	data = xnew(struct update_cache_data, 1);
	data->force = force;

	job_schedule_update_cache(JOB_TYPE_LIB, data);
}

void cmus_update_lib(void)
{
	struct update_data *data;

	data = xnew0(struct update_data, 1);

	lib_for_each(update_cb, data, NULL);

	job_schedule_update(data);
}

void cmus_update_tis(struct track_info **tis, int nr, int force)
{
	struct update_data *data;

	data = xnew(struct update_data, 1);
	data->size = nr;
	data->used = nr;
	data->ti = tis;
	data->force = force;

	job_schedule_update(data);
}

static const char *get_ext(const char *filename)
{
	const char *ext = strrchr(filename, '.');

	if (ext)
		ext++;
	return ext;
}

static int str_in_array(const char *str, const char * const *array)
{
	int i;

	for (i = 0; array[i]; i++) {
		if (strcasecmp(str, array[i]) == 0)
			return 1;
	}
	return 0;
}

int cmus_is_playlist(const char *filename)
{
	const char *ext = get_ext(filename);

	return ext && str_in_array(ext, playlist_exts);
}

int cmus_is_playable(const char *filename)
{
	const char *ext = get_ext(filename);

	return ext && str_in_array(ext, (const char * const *)playable_exts);
}

int cmus_is_supported(const char *filename)
{
	const char *ext = get_ext(filename);

	return ext && (str_in_array(ext, (const char * const *)playable_exts) ||
			str_in_array(ext, playlist_exts));
}

struct pl_data {
	int (*cb)(void *data, const char *line);
	void *data;
};

static int pl_handle_line(void *data, const char *line)
{
	struct pl_data *d = data;
	int i = 0;

	while (isspace((unsigned char)line[i]))
		i++;
	if (line[i] == 0)
		return 0;

	if (line[i] == '#')
		return 0;

	return d->cb(d->data, line);
}

static int pls_handle_line(void *data, const char *line)
{
	struct pl_data *d = data;

	if (strncasecmp(line, "file", 4))
		return 0;
	line = strchr(line, '=');
	if (line == NULL)
		return 0;
	return d->cb(d->data, line + 1);
}

int cmus_playlist_for_each(const char *buf, int size, int reverse,
		int (*cb)(void *data, const char *line),
		void *data)
{
	struct pl_data d = { cb, data };
	int (*handler)(void *, const char *);

	handler = pl_handle_line;
	if (size >= 10 && strncasecmp(buf, "[playlist]", 10) == 0)
		handler = pls_handle_line;

	if (reverse) {
		buffer_for_each_line_reverse(buf, size, handler, &d);
	} else {
		buffer_for_each_line(buf, size, handler, &d);
	}
	return 0;
}

/* multi-threaded next track requests */

#define cmus_next_file_lock() cmus_mutex_lock(&cmus_next_file_mutex)
#define cmus_next_file_unlock() cmus_mutex_unlock(&cmus_next_file_mutex)

static struct track_info *cmus_get_next_from_main_thread(void)
{
	struct track_info *ti = play_queue_remove();
	if (ti) {
		play_queue_active = true;
	} else {
		if (!play_queue_active || !stop_after_queue)
			ti = play_library ? lib_goto_next() : pl_goto_next();
		play_queue_active = false;
	}
	return ti;
}

static struct track_info *cmus_get_next_from_other_thread(void)
{
	static pthread_mutex_t mutex = CMUS_MUTEX_INITIALIZER;
	cmus_mutex_lock(&mutex);

	/* only one thread may request a track at a time */

	notify_via_pipe(cmus_next_track_request_fd_priv);

	cmus_next_file_lock();
	while (!cmus_next_file_provided)
		pthread_cond_wait(&cmus_next_file_cond, &cmus_next_file_mutex);
	struct track_info *ti = cmus_next_file;
	cmus_next_file_provided = 0;
	cmus_next_file_unlock();

	cmus_mutex_unlock(&mutex);

	return ti;
}

struct track_info *cmus_get_next_track(void)
{
	pthread_t this_thread = pthread_self();
	if (pthread_equal(this_thread, main_thread))
		return cmus_get_next_from_main_thread();
	return cmus_get_next_from_other_thread();
}

void cmus_provide_next_track(void)
{
	clear_pipe(cmus_next_track_request_fd, 1);

	cmus_next_file_lock();
	cmus_next_file = cmus_get_next_from_main_thread();
	cmus_next_file_provided = 1;
	cmus_next_file_unlock();

	pthread_cond_broadcast(&cmus_next_file_cond);
}

void cmus_track_request_init(void)
{
	init_pipes(&cmus_next_track_request_fd, &cmus_next_track_request_fd_priv);
}

static int cmus_can_raise_vte_x11(void)
{
	return getenv("DISPLAY") && getenv("WINDOWID");
}

int cmus_can_raise_vte(void)
{
	return cmus_can_raise_vte_x11();
}

static int cmus_raise_vte_x11_error(void)
{
	return 0;
}

void cmus_raise_vte(void)
{
	if (cmus_can_raise_vte_x11()) {
		if (!x11_init_done) {
			void *x11;

			x11_init_done = 1;
			x11 = dlopen("libX11.so", RTLD_LAZY);

			if (x11) {
				int (*x11_error)(void *);

				x11_error = dlsym(x11, "XSetErrorHandler");
				x11_open = dlsym(x11, "XOpenDisplay");
				x11_raise = dlsym(x11, "XRaiseWindow");
				x11_close = dlsym(x11, "XCloseDisplay");

				if (x11_error) {
					x11_error(cmus_raise_vte_x11_error);
				}
			}
		}

		if (x11_open && x11_raise && x11_close) {
			char *xid_str;
			long int xid = 0;

			xid_str = getenv("WINDOWID");
			if (!str_to_int(xid_str, &xid) && xid != 0) {
				void *display;

				display = x11_open(NULL);
				if (display) {
					x11_raise(display, (int) xid);
					x11_close(display);
				}
			}
		}
	}
}
