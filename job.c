#include "job.h"
#include "worker.h"
#include "cache.h"
#include "xmalloc.h"
#include "debug.h"
#include "load_dir.h"
#include "path.h"
#include "editable.h"
#include "play_queue.h"
#include "lib.h"
#include "utils.h"
#include "file.h"
#include "cache.h"

#include <string.h>
#include <unistd.h>
#include <errno.h>

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

static void add_file(const char *filename)
{
	struct track_info *ti;

	cache_lock();
	ti = cache_get_ti(filename);
	cache_unlock();

	if (ti)
		add_ti(ti);
}

static void add_url(const char *url)
{
	add_file(url);
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

static int points_within(const char *target, const char *root)
{
	int tlen = strlen(target);
	int rlen = strlen(root);

	if (rlen > tlen)
		return 0;
	if (strncmp(target, root, rlen))
		return 0;
	return target[rlen] == '/' || !target[rlen];
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
			if (points_within(target, root)) {
				/* symlink points withing the root */
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
				add_file(dir.path);
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

	if (is_url(line)) {
		add_url(line);
	} else {
		add_file(line);
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
		/* beautiful hack */
		reverse = jd->add == play_queue_prepend;

		cmus_playlist_for_each(buf, size, reverse, handle_line, NULL);
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
	case FILE_TYPE_PL:
		add_pl(jd->name);
		break;
	case FILE_TYPE_DIR:
		add_dir(jd->name, jd->name);
		break;
	case FILE_TYPE_FILE:
		add_file(jd->name);
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
		if (rc || ti->mtime != s.st_mtime) {
			editable_lock();
			lib_remove(ti);
			editable_unlock();

			cache_lock();
			cache_remove_ti(ti);
			cache_unlock();

			if (rc) {
				d_print("removing dead file %s\n", ti->filename);
			} else {
				d_print("mtime changed: %s\n", ti->filename);
				cmus_add(lib_add_track, ti->filename, FILE_TYPE_FILE, JOB_TYPE_LIB);
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
	struct track_info **tis;
	int i, count;

	cache_lock();
	tis = cache_refresh(&count);
	editable_lock();
	for (i = 0; i < count; i++) {
		struct track_info *new, *old = tis[i];

		if (!old)
			continue;

		new = old->next;
		if (lib_remove(old) && new)
			lib_add_track(new);
		// FIXME: other views

		track_info_unref(old);
		if (new)
			track_info_unref(new);
	}
	editable_unlock();
	cache_unlock();
	free(tis);
}

void free_update_cache_job(void *data)
{
}
