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
#include <pl.h>
#include <player.h>
#include <play_queue.h>
#include <worker.h>
#include <track_db.h>
#include <misc.h>
#include <file.h>
#include <pls.h>
#include <utils.h>
#include <path.h>
#include <xmalloc.h>
#include <xstrjoin.h>
#include <debug.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>

#define WORKER_TYPE_PLAYLIST  (1U)
#define WORKER_TYPE_PLAYQUEUE (2U)
#define WORKER_TYPE_UPDATE    (3U)

static pthread_mutex_t track_db_mutex = CMUS_MUTEX_INITIALIZER;
static struct track_db *track_db;

#define track_db_lock() cmus_mutex_lock(&track_db_mutex)
#define track_db_unlock() cmus_mutex_unlock(&track_db_mutex)

/* jobs {{{ */

#define JOB_FLAG_ENQUEUE (1 << 0)
#define JOB_FLAG_PREPEND (1 << 1)

struct job_data {
	enum { JOB_URL, JOB_PL, JOB_DIR, JOB_FILE } type;
	char *name;
	unsigned int flags;
};

static struct job_data *job_data_new(const char *name)
{
	struct job_data *data;

	data = xnew(struct job_data, 1);
	if (is_url(name)) {
		data->type = JOB_URL;
		data->name = xstrdup(name);
	} else {
		struct stat s;

		data->name = path_absolute(name);
		if (data->name == NULL) {
			free(data);
			return NULL;
		}

		/* stat follows symlinks, lstat does not */
		if (stat(data->name, &s) == -1) {
			free(data->name);
			free(data);
			return NULL;
		}
		if (S_ISDIR(s.st_mode)) {
			data->type = JOB_DIR;
		} else if (cmus_is_playlist(data->name)) {
			data->type = JOB_PL;
		} else {
			data->type = JOB_FILE;
		}
	}
	return data;
}

static void add_url(unsigned int flags, const char *filename)
{
	struct track_info *ti;

	ti = xnew(struct track_info, 1);
	ti->ref = 1;
	ti->filename = xstrdup(filename);
	ti->comments = xnew0(struct comment, 1);
	ti->duration = -1;
	ti->mtime = -1;

	if (flags & JOB_FLAG_ENQUEUE) {
		if (flags & JOB_FLAG_PREPEND) {
			play_queue_prepend(ti);
		} else {
			play_queue_append(ti);
		}
	} else {
		pl_add_track(ti);
	}
	track_info_unref(ti);
}

/* add file to the playlist
 *
 * @filename: absolute filename with extraneous slashes stripped
 */
static void add_file(unsigned int flags, const char *filename)
{
	struct track_info *ti;

	track_db_lock();
	ti = track_db_get_track(track_db, filename);
	track_db_unlock();
	if (ti == NULL)
		return;

	if (flags & JOB_FLAG_ENQUEUE) {
		if (flags & JOB_FLAG_PREPEND) {
			play_queue_prepend(ti);
		} else {
			play_queue_append(ti);
		}
	} else {
		pl_add_track(ti);
	}
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

static void add_dir(unsigned int flags, const char *dirname);

static void handle_dentry(unsigned int flags, const char *dirname, const char *name)
{
	struct stat s;
	char *full;

	full = fullname(dirname, name);
	/* stat follows symlinks, lstat does not */
	if (stat(full, &s) == 0) {
		if (S_ISDIR(s.st_mode)) {
			add_dir(flags, full);
		} else {
			add_file(flags, full);
		}
	}
	free(full);
}

static void add_dir(unsigned int flags, const char *dirname)
{
	struct dirent **dentries;
	int num, i;

	num = scandir(dirname, &dentries, dir_filter, alphasort);
	if (num == -1) {
		d_print("error: scandir: %s\n", strerror(errno));
		return;
	}
	if (flags & JOB_FLAG_ENQUEUE && flags & JOB_FLAG_PREPEND) {
		for (i = num - 1; i >= 0; i--) {
			if (!worker_cancelling())
				handle_dentry(flags, dirname, dentries[i]->d_name);
			free(dentries[i]);
		}
	} else {
		for (i = 0; i < num; i++) {
			if (!worker_cancelling())
				handle_dentry(flags, dirname, dentries[i]->d_name);
			free(dentries[i]);
		}
	}
	free(dentries);
}

static void handle_line(unsigned int flags, const char *line)
{
	if (*line == '#')
		return;
	if (is_url(line)) {
		add_url(flags, line);
	} else {
		add_file(flags, line);
	}
}

static void add_files(unsigned int flags, char **files)
{
	int i;

	if (flags & JOB_FLAG_ENQUEUE && flags & JOB_FLAG_PREPEND) {
		for (i = 0; files[i]; i++) {
		}
		i--;
		while (i >= 0) {
			if (worker_cancelling())
				break;
			handle_line(flags, files[i]);
			i--;
		}
	} else {
		for (i = 0; files[i]; i++) {
			if (worker_cancelling())
				break;
			handle_line(flags, files[i]);
		}
	}
}

static void add_pl(unsigned int flags, const char *filename)
{
	char **files;

	files = cmus_playlist_get_files(filename);
	if (files == NULL)
		return;
	add_files(flags, files);
	free_str_array(files);
}

static void job(void *data)
{
	struct job_data *jd = data;

	switch (jd->type) {
	case JOB_URL:
		add_url(jd->flags, jd->name);
		break;
	case JOB_PL:
		add_pl(jd->flags, jd->name);
		break;
	case JOB_DIR:
		add_dir(jd->flags, jd->name);
		break;
	case JOB_FILE:
		add_file(jd->flags, jd->name);
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

static void update_playlist_job(void *data)
{
	struct update_data *d = data;
	int i;

	for (i = 0; i < d->used; i++) {
		struct track_info *ti = d->ti[i];
		struct stat s;

		/* stat follows symlinks, lstat does not */
		if (stat(ti->filename, &s) == -1) {
			d_print("removing dead file %s\n", ti->filename);
			pl_remove(ti);
		} else if (ti->mtime != s.st_mtime) {
			d_print("mtime changed: %s\n", ti->filename);
			pl_remove(ti);
			cmus_add(ti->filename);
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

	db_filename_base = xstrjoin(cmus_cache_dir, "/trackdb");
	track_db = track_db_new(db_filename_base);
	free(db_filename_base);

	worker_init();

	play_queue_init();
	return 0;
}

void cmus_exit(void)
{
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
	info = pl_set_next();
	if (info) {
		player_set_file(info->filename);
		track_info_unref(info);
	}
}

void cmus_prev(void)
{
	struct track_info *info;

	info = pl_set_prev();
	if (info) {
		player_set_file(info->filename);
		track_info_unref(info);
	}
}

void cmus_play_file(const char *filename)
{
	player_play_file(filename);
}

int cmus_add(const char *name)
{
	struct job_data *data;

	data = job_data_new(name);
	if (data == NULL)
		return -1;
	data->flags = 0;
	worker_add_job(WORKER_TYPE_PLAYLIST, job, data);
	return 0;
}

void cmus_clear_playlist(void)
{
	worker_remove_jobs(WORKER_TYPE_PLAYLIST);
	pl_clear();
}

int save_playlist_cb(void *data, struct track_info *ti)
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

int cmus_save_playlist(const char *filename)
{
	int fd, rc;

	fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (fd == -1)
		return -1;
	rc = pl_for_each(save_playlist_cb, &fd);
	close(fd);
	return rc;
}

int cmus_load_playlist(const char *name)
{
	struct job_data *data;

	worker_remove_jobs(WORKER_TYPE_PLAYLIST);
	pl_clear();

	data = xnew(struct job_data, 1);
	data->type = JOB_PL;
	data->name = xstrdup(name);
	data->flags = 0;
	worker_add_job(WORKER_TYPE_PLAYLIST, job, data);
	return 0;
}

int cmus_enqueue(const char *name, int prepend)
{
	struct job_data *data;

	data = job_data_new(name);
	if (data == NULL)
		return -1;
	data->flags = JOB_FLAG_ENQUEUE;
	if (prepend)
		data->flags |= JOB_FLAG_PREPEND;
	worker_add_job(WORKER_TYPE_PLAYQUEUE, job, data);
	return 0;
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

void cmus_update_playlist(void)
{
	struct update_data *data;

	data = xnew(struct update_data, 1);
	data->size = 0;
	data->used = 0;
	data->ti = NULL;
	pl_for_each(update_cb, data);
	worker_add_job(WORKER_TYPE_UPDATE, update_playlist_job, data);
}

struct track_info *cmus_get_track_info(const char *name)
{
	struct track_info *ti;

	if (is_url(name)) {
		ti = xnew(struct track_info, 1);
		ti->ref = 1;
		ti->filename = xstrdup(name);
		ti->comments = xnew0(struct comment, 1);
		ti->duration = -1;
		ti->mtime = -1;
		return ti;
	}
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
	if (strcasecmp(ext, "pl") == 0)
		return 1;
	if (strcasecmp(ext, "m3u") == 0)
		return 1;
	if (strcasecmp(ext, "pls") == 0)
		return 1;
	return 0;
}

char **cmus_playlist_get_files(const char *filename)
{
	const char *ext;

	ext = strrchr(filename, '.');
	if (ext != NULL) {
		ext++;
		if (strcasecmp(ext, "pls") == 0) {
			char **files;
			char *contents;
			int len;

			contents = file_get_contents(filename, &len);
			if (contents == NULL)
				return NULL;
			files = pls_get_files(contents);
			free(contents);
			return files;
		}
	}
	return file_get_lines(filename);
}
