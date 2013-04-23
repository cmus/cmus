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
#ifdef HAVE_CONFIG
#include "config/cue.h"
#endif
#ifdef CONFIG_CUE
#include "cue_utils.h"
#endif

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

static struct track_info *ti_buffer[32];
static int ti_buffer_fill;
static struct add_data *jd;

static void flush_ti_buffer(void)
{
	int i;

	editable_lock();
	for (i = 0; i < ti_buffer_fill; i++) {
		jd->add(ti_buffer[i]);
		track_info_unref(ti_buffer[i]);
	}
	editable_unlock();
	ti_buffer_fill = 0;
}

static void add_ti(struct track_info *ti)
{
	if (ti_buffer_fill == N_ELEMENTS(ti_buffer))
		flush_ti_buffer();
	ti_buffer[ti_buffer_fill++] = ti;
}

#ifdef CONFIG_CUE
static int add_file_cue(const char *filename);
#endif

static void add_file(const char *filename, int force)
{
	struct track_info *ti;

#ifdef CONFIG_CUE
	if (!is_cue_url(filename)) {
		if (force || lookup_cache_entry(filename, hash_str(filename)) == NULL) {
			int done = add_file_cue(filename);
			if (done)
				return;
		}
	}
#endif

	cache_lock();
	ti = cache_get_ti(filename, force);
	cache_unlock();

	if (ti)
		add_ti(ti);
}

#ifdef CONFIG_CUE
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
#endif

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
	int size, reverse;

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

void do_add_job(void *data)
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
	if (ti_buffer_fill)
		flush_ti_buffer();
	jd = NULL;
}

void free_add_job(void *data)
{
	struct add_data *d = data;
	free(d->name);
	free(d);
}

void do_update_job(void *data)
{
	struct update_data *d = data;
	int i;

	for (i = 0; i < d->used; i++) {
		struct track_info *ti = d->ti[i];
		struct stat s;
		int rc;

		/* stat follows symlinks, lstat does not */
		rc = stat(ti->filename, &s);
		if (rc || d->force || ti->mtime != s.st_mtime || ti->duration == 0) {
			int force = ti->duration == 0;
			editable_lock();
			lib_remove(ti);
			editable_unlock();

			cache_lock();
			cache_remove_ti(ti);
			cache_unlock();

			if (!is_cue_url(ti->filename) && !is_http_url(ti->filename) && rc) {
				d_print("removing dead file %s\n", ti->filename);
			} else {
				if (ti->mtime != s.st_mtime)
					d_print("mtime changed: %s\n", ti->filename);
				cmus_add(lib_add_track, ti->filename, FILE_TYPE_FILE, JOB_TYPE_LIB, force);
			}
		}
		track_info_unref(ti);
	}
}

void free_update_job(void *data)
{
	struct update_data *d = data;

	free(d->ti);
	free(d);
}

void do_update_cache_job(void *data)
{
	struct update_cache_data *d = data;
	struct track_info **tis;
	int i, count;

	cache_lock();
	tis = cache_refresh(&count, d->force);
	editable_lock();
	player_info_lock();
	for (i = 0; i < count; i++) {
		struct track_info *new, *old = tis[i];

		if (!old)
			continue;

		new = old->next;
		if (lib_remove(old) && new)
			lib_add_track(new);
		editable_update_track(&pl_editable, old, new);
		editable_update_track(&pq_editable, old, new);
		if (player_info.ti == old && new) {
			track_info_ref(new);
			player_file_changed(new);
		}

		track_info_unref(old);
		if (new)
			track_info_unref(new);
	}
	player_info_unlock();
	editable_unlock();
	cache_unlock();
	free(tis);
}

void free_update_cache_job(void *data)
{
	free(data);
}
