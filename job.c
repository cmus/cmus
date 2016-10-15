/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2008 Timo Hirvonen
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

#include "utils.h"
#include "job.h"
#include "worker.h"
#include "cache.h"
#include "xmalloc.h"
#include "debug.h"
#include "load_dir.h"
#include "path.h"
#include "editable.h"
#include "pl.h"
#include "play_queue.h"
#include "lib.h"
#include "utils.h"
#include "file.h"
#include "cache.h"
#include "player.h"
#include "discid.h"
#include "xstrjoin.h"
#include "ui_curses.h"
#include "cue_utils.h"

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

enum job_result_var {
	JOB_RES_ADD,
	JOB_RES_UPDATE,
	JOB_RES_UPDATE_CACHE,
	JOB_RES_PL_DELETE,
};

enum update_kind {
	UPDATE_NONE = 0,
	UPDATE_REMOVE = 1,
	UPDATE_MTIME_CHANGED = 2,
};

struct job_result {
	struct list_head node;

	enum job_result_var var;
	union {
		struct {
			add_ti_cb add_cb;
			size_t add_num;
			struct track_info **add_ti;
			void *add_opaque;
		};
		struct {
			size_t update_num;
			struct track_info **update_ti;
			enum update_kind *update_kind;
		};
		struct {
			size_t update_cache_num;
			struct track_info **update_cache_ti;
		};
		struct {
			void (*pl_delete_cb)(struct playlist *);
			struct playlist *pl_delete_pl;
		};
	};
};

int job_fd;
static int job_fd_priv;

static LIST_HEAD(job_result_head);
static pthread_mutex_t job_mutex = CMUS_MUTEX_INITIALIZER;

#define TI_CAP 32
static struct track_info **ti_buffer;
static size_t ti_buffer_fill;
static struct add_data *jd;

#define job_lock() cmus_mutex_lock(&job_mutex)
#define job_unlock() cmus_mutex_unlock(&job_mutex)

void job_init(void)
{
	init_pipes(&job_fd, &job_fd_priv);

	worker_init();
}

void job_exit(void)
{
	worker_remove_jobs_by_type(JOB_TYPE_ANY);
	worker_exit();

	close(job_fd);
	close(job_fd_priv);
}

static void job_push_result(struct job_result *res)
{
	job_lock();
	list_add_tail(&res->node, &job_result_head);
	job_unlock();

	notify_via_pipe(job_fd_priv);
}

static struct job_result *job_pop_result(void)
{
	struct job_result *res = NULL;

	job_lock();
	if (!list_empty(&job_result_head)) {
		struct list_head *item = job_result_head.next;
		list_del(item);
		res = container_of(item, struct job_result, node);
	}
	job_unlock();

	return res;
}

static void flush_ti_buffer(void)
{
	struct job_result *res = xnew(struct job_result, 1);

	res->var = JOB_RES_ADD;
	res->add_cb = jd->add;
	res->add_num = ti_buffer_fill;
	res->add_ti = ti_buffer;
	res->add_opaque = jd->opaque;

	job_push_result(res);

	ti_buffer_fill = 0;
	ti_buffer = NULL;
}

static void add_ti(struct track_info *ti)
{
	if (ti_buffer_fill == TI_CAP)
		flush_ti_buffer();
	if (!ti_buffer)
		ti_buffer = xnew(struct track_info *, TI_CAP);
	ti_buffer[ti_buffer_fill++] = ti;
}

static int add_file_cue(const char *filename);

static void add_file(const char *filename, int force)
{
	struct track_info *ti;

	if (!is_cue_url(filename)) {
		if (force || lookup_cache_entry(filename, hash_str(filename)) == NULL) {
			int done = add_file_cue(filename);
			if (done)
				return;
		}
	}

	cache_lock();
	ti = cache_get_ti(filename, force);
	cache_unlock();

	if (ti)
		add_ti(ti);
}

static int add_file_cue(const char *filename)
{
	int n_tracks;
	char *url;
	char *cue_filename;

	cue_filename = associated_cue(filename);
	if (cue_filename == NULL)
		return 0;

	n_tracks = cue_get_ntracks(cue_filename);
	if (n_tracks <= 0) {
		free(cue_filename);
		return 0;
	}

	for (int i = 1; i <= n_tracks; ++i) {
		url = construct_cue_url(cue_filename, i);
		add_file(url, 0);
		free(url);
	}

	free(cue_filename);
	return 1;
}

static void add_url(const char *url)
{
	add_file(url, 0);
}

static void add_cdda(const char *url)
{
	char *disc_id = NULL;
	int start_track = 1, end_track = -1;

	parse_cdda_url(url, &disc_id, &start_track, &end_track);

	if (end_track != -1) {
		int i;
		for (i = start_track; i <= end_track; i++) {
			char *new_url = gen_cdda_url(disc_id, i, -1);
			add_file(new_url, 0);
			free(new_url);
		}
	} else
		add_file(url, 0);
	free(disc_id);
}

static int dir_entry_cmp(const void *ap, const void *bp)
{
	struct dir_entry *a = *(struct dir_entry **)ap;
	struct dir_entry *b = *(struct dir_entry **)bp;

	return strcmp(a->name, b->name);
}

static int dir_entry_cmp_reverse(const void *ap, const void *bp)
{
	struct dir_entry *a = *(struct dir_entry **)ap;
	struct dir_entry *b = *(struct dir_entry **)bp;

	return strcmp(b->name, a->name);
}

static int points_within_and_visible(const char *target, const char *root)
{
	int tlen = strlen(target);
	int rlen = strlen(root);

	if (rlen > tlen)
		return 0;
	if (strncmp(target, root, rlen))
		return 0;
	if (target[rlen] != '/' && target[rlen] != '\0')
		return 0;
	/* assume the path is normalized */
	if (strstr(target + rlen, "/."))
		return 0;

	return 1;
}

static void add_dir(const char *dirname, const char *root)
{
	struct directory dir;
	struct dir_entry **ents;
	const char *name;
	PTR_ARRAY(array);
	int i;

	if (dir_open(&dir, dirname)) {
		d_print("error: opening %s: %s\n", dirname, strerror(errno));
		return;
	}
	while ((name = dir_read(&dir))) {
		struct dir_entry *ent;
		int size;

		if (name[0] == '.')
			continue;

		if (dir.is_link) {
			char buf[1024];
			char *target;
			int rc = readlink(dir.path, buf, sizeof(buf));

			if (rc < 0 || rc == sizeof(buf))
				continue;
			buf[rc] = 0;
			target = path_absolute_cwd(buf, dirname);
			if (points_within_and_visible(target, root)) {
				d_print("%s -> %s points within %s. ignoring\n",
						dir.path, target, root);
				free(target);
				continue;
			}
			free(target);
		}

		size = strlen(name) + 1;
		ent = xmalloc(sizeof(struct dir_entry) + size);
		ent->mode = dir.st.st_mode;
		memcpy(ent->name, name, size);
		ptr_array_add(&array, ent);
	}
	dir_close(&dir);

	if (jd->add == play_queue_prepend) {
		ptr_array_sort(&array, dir_entry_cmp_reverse);
	} else {
		ptr_array_sort(&array, dir_entry_cmp);
	}
	ents = array.ptrs;
	for (i = 0; i < array.count; i++) {
		if (!worker_cancelling()) {
			/* abuse dir.path because
			 *  - it already contains dirname + '/'
			 *  - it is guaranteed to be large enough
			 */
			int len = strlen(ents[i]->name);

			memcpy(dir.path + dir.len, ents[i]->name, len + 1);
			if (S_ISDIR(ents[i]->mode)) {
				add_dir(dir.path, root);
			} else {
				add_file(dir.path, 0);
			}
		}
		free(ents[i]);
	}
	free(ents);
}

static int handle_line(void *data, const char *line)
{
	if (worker_cancelling())
		return 1;

	if (is_http_url(line) || is_cue_url(line)) {
		add_url(line);
	} else {
		char *absolute = path_absolute_cwd(line, data);
		add_file(absolute, 0);
		free(absolute);
	}

	return 0;
}

static void add_pl(const char *filename)
{
	char *buf;
	ssize_t size;
	int reverse;

	buf = mmap_file(filename, &size);
	if (size == -1)
		return;

	if (buf) {
		char *cwd = xstrjoin(filename, "/..");
		/* beautiful hack */
		reverse = jd->add == play_queue_prepend;

		cmus_playlist_for_each(buf, size, reverse, handle_line, cwd);
		free(cwd);
		munmap(buf, size);
	}
}

static void do_add_job(void *data)
{
	jd = data;
	switch (jd->type) {
	case FILE_TYPE_URL:
		add_url(jd->name);
		break;
	case FILE_TYPE_CDDA:
		add_cdda(jd->name);
		break;
	case FILE_TYPE_PL:
		add_pl(jd->name);
		break;
	case FILE_TYPE_DIR:
		add_dir(jd->name, jd->name);
		break;
	case FILE_TYPE_FILE:
		add_file(jd->name, jd->force);
		break;
	case FILE_TYPE_INVALID:
		break;
	}
	if (ti_buffer)
		flush_ti_buffer();
	jd = NULL;
}

static void free_add_job(void *data)
{
	struct add_data *d = data;
	free(d->name);
	free(d);
}

static void job_handle_add_result(struct job_result *res)
{
	for (size_t i = 0; i < res->add_num; i++) {
		res->add_cb(res->add_ti[i], res->add_opaque);
		track_info_unref(res->add_ti[i]);
	}

	free(res->add_ti);
}

void job_schedule_add(int type, struct add_data *data)
{
	worker_add_job(type | JOB_TYPE_ADD, do_add_job, free_add_job, data);
}

static void do_update_job(void *data)
{
	struct update_data *d = data;
	int i;
	enum update_kind *kind = xnew(enum update_kind, d->used);
	struct job_result *res;

	for (i = 0; i < d->used; i++) {
		struct track_info *ti = d->ti[i];
		struct stat s;
		int rc;

		rc = stat(ti->filename, &s);
		if (rc || d->force || ti->mtime != s.st_mtime || ti->duration == 0) {
			kind[i] = UPDATE_NONE;
			if (!is_cue_url(ti->filename) && !is_http_url(ti->filename) && rc)
				kind[i] |= UPDATE_REMOVE;
			else if (ti->mtime != s.st_mtime)
				kind[i] |= UPDATE_MTIME_CHANGED;
		} else {
			track_info_unref(ti);
			d->ti[i] = NULL;
		}
	}

	res = xnew(struct job_result, 1);

	res->var = JOB_RES_UPDATE;
	res->update_num = d->used;
	res->update_ti = d->ti;
	res->update_kind = kind;

	job_push_result(res);

	d->ti = NULL;
}

static void free_update_job(void *data)
{
	struct update_data *d = data;

	if (d->ti) {
		for (size_t i = 0; i < d->used; i++)
			track_info_unref(d->ti[i]);
		free(d->ti);
	}
	free(d);
}

static void job_handle_update_result(struct job_result *res)
{
	for (size_t i = 0; i < res->update_num; i++) {
		struct track_info *ti = res->update_ti[i];
		int force;

		if (!ti)
			continue;

		lib_remove(ti);

		cache_lock();
		cache_remove_ti(ti);
		cache_unlock();

		if (res->update_kind[i] & UPDATE_REMOVE) {
			d_print("removing dead file %s\n", ti->filename);
		} else {
			if (res->update_kind[i] & UPDATE_MTIME_CHANGED)
				d_print("mtime changed: %s\n", ti->filename);
			force = ti->duration == 0;
			cmus_add(lib_add_track, ti->filename, FILE_TYPE_FILE,
					JOB_TYPE_LIB, force, NULL);
		}

		track_info_unref(ti);
	}

	free(res->update_kind);
	free(res->update_ti);
}

void job_schedule_update(struct update_data *data)
{
	worker_add_job(JOB_TYPE_LIB | JOB_TYPE_UPDATE, do_update_job,
			free_update_job, data);
}

static void do_update_cache_job(void *data)
{
	struct update_cache_data *d = data;
	int count;
	struct track_info **tis;
	struct job_result *res;

	cache_lock();
	tis = cache_refresh(&count, d->force);
	cache_unlock();

	res = xnew(struct job_result, 1);
	res->var = JOB_RES_UPDATE_CACHE;
	res->update_cache_ti = tis;
	res->update_cache_num = count;
	job_push_result(res);
}

static void free_update_cache_job(void *data)
{
	free(data);
}

static void job_handle_update_cache_result(struct job_result *res)
{
	for (size_t i = 0; i < res->update_cache_num; i++) {
		struct track_info *new, *old = res->update_cache_ti[i];

		if (!old)
			continue;

		new = old->next;
		if (lib_remove(old) && new)
			lib_add_track(new, NULL);
		pl_update_track(old, new);
		editable_update_track(&pq_editable, old, new);
		if (player_info.ti == old && new) {
			track_info_ref(new);
			player_file_changed(new);
		}

		track_info_unref(old);
		if (new)
			track_info_unref(new);
	}
	free(res->update_cache_ti);
}

void job_schedule_update_cache(int type, struct update_cache_data *data)
{
	worker_add_job(type | JOB_TYPE_UPDATE_CACHE, do_update_cache_job,
			free_update_cache_job, data);
}

static void do_pl_delete_job(void *data)
{
	/*
	 * If PL jobs are canceled this function won't run. Hence we push the
	 * result in the free function.
	 */
}

static void free_pl_delete_job(void *data)
{
	struct pl_delete_data *pdd = data;
	struct job_result *res;

	res = xnew(struct job_result, 1);
	res->var = JOB_RES_PL_DELETE;
	res->pl_delete_cb = pdd->cb;
	res->pl_delete_pl = pdd->pl;
	job_push_result(res);

	free(pdd);
}

static void job_handle_pl_delete_result(struct job_result *res)
{
	res->pl_delete_cb(res->pl_delete_pl);
}

void job_schedule_pl_delete(struct pl_delete_data *data)
{
	worker_add_job(JOB_TYPE_PL | JOB_TYPE_DELETE, do_pl_delete_job,
			free_pl_delete_job, data);
}

static void job_handle_result(struct job_result *res)
{
	switch (res->var) {
	case JOB_RES_ADD:
		job_handle_add_result(res);
		break;
	case JOB_RES_UPDATE:
		job_handle_update_result(res);
		break;
	case JOB_RES_UPDATE_CACHE:
		job_handle_update_cache_result(res);
		break;
	case JOB_RES_PL_DELETE:
		job_handle_pl_delete_result(res);
		break;
	}
	free(res);
}

void job_handle(void)
{
	clear_pipe(job_fd, -1);

	struct job_result *res;
	while ((res = job_pop_result()))
		job_handle_result(res);
}
