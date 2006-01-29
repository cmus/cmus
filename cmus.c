/* 
 * Copyright 2005 Timo Hirvonen
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

#include <cmus.h>
#include <lib.h>
#include <pl.h>
#include <player.h>
#include <play_queue.h>
#include <worker.h>
#include <track_db.h>
#include <misc.h>
#include <file.h>
#include <utils.h>
#include <path.h>
#include <options.h>
#include <xmalloc.h>
#include <xstrjoin.h>
#include <debug.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <ctype.h>

static pthread_mutex_t track_db_mutex = CMUS_MUTEX_INITIALIZER;
static struct track_db *track_db;

#define track_db_lock() cmus_mutex_lock(&track_db_mutex)
#define track_db_unlock() cmus_mutex_unlock(&track_db_mutex)

/* jobs {{{ */

struct job_data {
	enum file_type type;
	char *name;
	add_ti_cb add;
};

static struct track_info *track_info_url_new(const char *url)
{
	struct track_info *ti = track_info_new(url);
	ti->comments = xnew0(struct keyval, 1);
	ti->duration = -1;
	ti->mtime = -1;
	return ti;
}

static void add_url(add_ti_cb add, const char *filename)
{
	struct track_info *ti;

	ti = track_info_url_new(filename);
	add(ti);
	track_info_unref(ti);
}

/* add file to the playlist
 *
 * @filename: absolute filename with extraneous slashes stripped
 */
static void add_file(add_ti_cb add, const char *filename)
{
	struct track_info *ti;

	track_db_lock();
	ti = track_db_get_track(track_db, filename);
	track_db_unlock();

	if (ti == NULL)
		return;
	add(ti);
	track_info_unref(ti);
}

static char *fullname(const char *path, const char *name)
{
	int l1, l2;
	char *full;

	l1 = strlen(path);
	l2 = strlen(name);
	if (path[l1 - 1] == '/')
		l1--;
	full = xnew(char, l1 + 1 + l2 + 1);
	memcpy(full, path, l1);
	full[l1] = '/';
	memcpy(full + l1 + 1, name, l2 + 1);
	return full;
}

static int dir_filter(const struct dirent *d)
{
	if (d->d_name[0] == '.')
		return 0;
	return 1;
}

static void add_dir(add_ti_cb add, const char *dirname);

static void handle_dentry(add_ti_cb add, const char *dirname, const char *name)
{
	struct stat s;
	char *full;

	full = fullname(dirname, name);
	/* stat follows symlinks, lstat does not */
	if (stat(full, &s) == 0) {
		if (S_ISDIR(s.st_mode)) {
			add_dir(add, full);
		} else {
			add_file(add, full);
		}
	}
	free(full);
}

static void add_dir(add_ti_cb add, const char *dirname)
{
	struct dirent **dentries;
	int num, i;

	num = scandir(dirname, &dentries, dir_filter, alphasort);
	if (num == -1) {
		d_print("error: scandir: %s\n", strerror(errno));
		return;
	}
	if (add == play_queue_prepend) {
		for (i = num - 1; i >= 0; i--) {
			if (!worker_cancelling())
				handle_dentry(add, dirname, dentries[i]->d_name);
			free(dentries[i]);
		}
	} else {
		for (i = 0; i < num; i++) {
			if (!worker_cancelling())
				handle_dentry(add, dirname, dentries[i]->d_name);
			free(dentries[i]);
		}
	}
	free(dentries);
}

static int handle_line(void *data, const char *line)
{
	add_ti_cb add = data;

	if (worker_cancelling())
		return 1;

	if (is_url(line)) {
		add_url(add, line);
	} else {
		add_file(add, line);
	}
	return 0;
}

static void add_pl(add_ti_cb add, const char *filename)
{
	char *buf;
	int size, reverse;

	buf = mmap_file(filename, &size);
	if (size == -1)
		return;

	if (buf) {
		/* beautiful hack */
		reverse = add == play_queue_prepend;

		cmus_playlist_for_each(buf, size, reverse, handle_line, add);
		munmap(buf, size);
	}
}

static void job(void *data)
{
	struct job_data *jd = data;

	switch (jd->type) {
	case FILE_TYPE_URL:
		add_url(jd->add, jd->name);
		break;
	case FILE_TYPE_PL:
		add_pl(jd->add, jd->name);
		break;
	case FILE_TYPE_DIR:
		add_dir(jd->add, jd->name);
		break;
	case FILE_TYPE_FILE:
		add_file(jd->add, jd->name);
		break;
	case FILE_TYPE_INVALID:
		break;
	}
	free(jd->name);
	free(jd);
}

struct update_data {
	size_t size;
	size_t used;
	struct track_info **ti;
};

static void update_lib_job(void *data)
{
	struct update_data *d = data;
	int i;

	for (i = 0; i < d->used; i++) {
		struct track_info *ti = d->ti[i];
		struct stat s;

		/* stat follows symlinks, lstat does not */
		if (stat(ti->filename, &s) == -1) {
			d_print("removing dead file %s\n", ti->filename);
			lib_remove(ti);
		} else if (ti->mtime != s.st_mtime) {
			d_print("mtime changed: %s\n", ti->filename);
			lib_remove(ti);
			cmus_add(lib_add_track, ti->filename, FILE_TYPE_FILE, JOB_TYPE_LIB);
		}
		track_info_unref(ti);
	}
	free(d->ti);
	free(d);
}

/* }}} */

int cmus_init(void)
{
	char *db_filename_base;

	db_filename_base = xstrjoin(cmus_config_dir, "/trackdb");
	track_db = track_db_new(db_filename_base);
	free(db_filename_base);

	worker_init();

	play_queue_init();
	return 0;
}

void cmus_exit(void)
{
	worker_remove_jobs(JOB_TYPE_ANY);
	play_queue_exit();
	worker_exit();
	if (track_db_close(track_db))
		d_print("error: %s\n", strerror(errno));
}

void cmus_next(void)
{
	struct track_info *info;

	info = play_queue_remove();
	if (info) {
		player_set_file(info->filename);
		track_info_unref(info);
		return;
	}
	if (play_library) {
		info = lib_set_next();
	} else {
		info = pl_set_next();
	}
	if (info) {
		player_set_file(info->filename);
		track_info_unref(info);
	}
}

void cmus_prev(void)
{
	struct track_info *info;

	if (play_library) {
		info = lib_set_prev();
	} else {
		info = pl_set_prev();
	}
	if (info) {
		player_set_file(info->filename);
		track_info_unref(info);
	}
}

void cmus_play_file(const char *filename)
{
	player_play_file(filename);
}

enum file_type cmus_detect_ft(const char *name, char **ret)
{
	char *absolute;
	struct stat st;

	if (is_url(name)) {
		*ret = xstrdup(name);
		return FILE_TYPE_URL;
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

void cmus_add(add_ti_cb add, const char *name, enum file_type ft, int jt)
{
	struct job_data *data = xnew(struct job_data, 1);

	data->add = add;
	data->name = xstrdup(name);
	data->type = ft;
	worker_add_job(jt, job, data);
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

int cmus_save(for_each_ti_cb for_each_ti, const char *filename)
{
	int fd, rc;

	fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (fd == -1)
		return -1;
	rc = for_each_ti(save_playlist_cb, &fd);
	close(fd);
	return rc;
}

static int update_cb(void *data, struct track_info *ti)
{
	struct update_data *d = data;

	if (is_url(ti->filename))
		return 0;

	if (d->size == d->used) {
		if (d->size == 0)
			d->size = 16;
		d->size *= 2;
		d->ti = xrealloc(d->ti, d->size * sizeof(struct track_info *));
	}
	track_info_ref(ti);
	d->ti[d->used++] = ti;
	return 0;
}

void cmus_update_lib(void)
{
	struct update_data *data;

	data = xnew(struct update_data, 1);
	data->size = 0;
	data->used = 0;
	data->ti = NULL;
	lib_for_each(update_cb, data);
	worker_add_job(JOB_TYPE_LIB, update_lib_job, data);
}

void cmus_update_selected(void)
{
	struct update_data *data;

	data = xnew(struct update_data, 1);
	data->size = 0;
	data->used = 0;
	data->ti = NULL;

	lib_lock();
	__lib_for_each_sel(update_cb, data, 0);
	lib_unlock();

	worker_add_job(JOB_TYPE_LIB, update_lib_job, data);
}

struct track_info *cmus_get_track_info(const char *name)
{
	struct track_info *ti;

	if (is_url(name))
		return track_info_url_new(name);
	track_db_lock();
	ti = track_db_get_track(track_db, name);
	track_db_unlock();
	return ti;
}

int cmus_is_playlist(const char *filename)
{
	const char *ext;

	ext = strrchr(filename, '.');
	if (ext == NULL)
		return 0;
	ext++;
	/* ugh, this extension is actually used by perl */
	if (strcasecmp(ext, "pl") == 0)
		return 1;
	if (strcasecmp(ext, "m3u") == 0)
		return 1;
	if (strcasecmp(ext, "pls") == 0)
		return 1;
	return 0;
}

struct pl_data {
	int (*cb)(void *data, const char *line);
	void *data;
};

static int pl_handle_line(void *data, const char *line)
{
	struct pl_data *d = data;
	int i = 0;

	while (isspace(line[i]))
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
