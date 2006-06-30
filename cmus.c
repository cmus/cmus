#include <cmus.h>
#include <lib.h>
#include <pl.h>
#include <player.h>
#include <input.h>
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
#include "load_dir.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <ctype.h>

static pthread_mutex_t track_db_mutex = CMUS_MUTEX_INITIALIZER;
static struct track_db *track_db;
static char **playable_exts;
static const char * const playlist_exts[] = { "m3u", "pl", "pls", NULL };

#define track_db_lock() cmus_mutex_lock(&track_db_mutex)
#define track_db_unlock() cmus_mutex_unlock(&track_db_mutex)

/* add (worker job) {{{ */

struct add_data {
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
	editable_lock();
	add(ti);
	editable_unlock();
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

	editable_lock();
	add(ti);
	editable_unlock();
	track_info_unref(ti);
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

static void add_dir(add_ti_cb add, const char *dirname)
{
	struct directory dir;
	struct dir_entry **ents;
	const char *name;
	struct stat st;
	PTR_ARRAY(array);
	int i;

	if (dir_open(&dir, dirname)) {
		d_print("error: opening %s: %s\n", dirname, strerror(errno));
		return;
	}
	while ((name = dir_read(&dir, &st))) {
		struct dir_entry *ent;
		int size;

		if (name[0] == '.')
			continue;

		size = strlen(name) + 1;
		ent = xmalloc(sizeof(struct dir_entry) + size);
		ent->mode = st.st_mode;
		memcpy(ent->name, name, size);
		ptr_array_add(&array, ent);
	}
	dir_close(&dir);

	if (add == play_queue_prepend) {
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
				add_dir(add, dir.path);
			} else {
				add_file(add, dir.path);
			}
		}
		free(ents[i]);
	}
	free(ents);
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

static void do_add_job(void *data)
{
	struct add_data *jd = data;

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
}

static void free_add_job(void *data)
{
	struct add_data *jd = data;

	free(jd->name);
	free(jd);
}

/* }}} */

/* update (worker job) {{{ */

struct update_data {
	size_t size;
	size_t used;
	struct track_info **ti;
};

static void do_update_job(void *data)
{
	struct update_data *d = data;
	int i;

	for (i = 0; i < d->used; i++) {
		struct track_info *ti = d->ti[i];
		struct stat s;

		/* stat follows symlinks, lstat does not */
		if (stat(ti->filename, &s) == -1) {
			d_print("removing dead file %s\n", ti->filename);
			editable_lock();
			lib_remove(ti);
			editable_unlock();
		} else if (ti->mtime != s.st_mtime) {
			d_print("mtime changed: %s\n", ti->filename);
			editable_lock();
			lib_remove(ti);
			editable_unlock();

			cmus_add(lib_add_track, ti->filename, FILE_TYPE_FILE, JOB_TYPE_LIB);
		}
		track_info_unref(ti);
	}
}

static void free_update_job(void *data)
{
	struct update_data *d = data;

	free(d->ti);
	free(d);
}

/* }}} */

int cmus_init(void)
{
	char *db_filename_base;

	playable_exts = ip_get_supported_extensions();

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
	worker_exit();
	if (track_db_close(track_db))
		d_print("error: %s\n", strerror(errno));
}

void cmus_next(void)
{
	struct track_info *info;

	editable_lock();
	info = play_queue_remove();
	if (info == NULL) {
		if (play_library) {
			info = lib_set_next();
		} else {
			info = pl_set_next();
		}
	}
	editable_unlock();

	if (info) {
		player_set_file(info->filename);
		track_info_unref(info);
	}
}

void cmus_prev(void)
{
	struct track_info *info;

	editable_lock();
	if (play_library) {
		info = lib_set_prev();
	} else {
		info = pl_set_prev();
	}
	editable_unlock();

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
	struct add_data *data = xnew(struct add_data, 1);

	data->add = add;
	data->name = xstrdup(name);
	data->type = ft;
	worker_add_job(jt, do_add_job, free_add_job, data);
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

	editable_lock();
	lib_for_each(update_cb, data);
	editable_unlock();

	worker_add_job(JOB_TYPE_LIB, do_update_job, free_update_job, data);
}

void cmus_update_tis(struct track_info **tis, int nr)
{
	struct update_data *data;

	data = xnew(struct update_data, 1);
	data->size = nr;
	data->used = nr;
	data->ti = tis;
	worker_add_job(JOB_TYPE_LIB, do_update_job, free_update_job, data);
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

static const char *get_ext(const char *filename)
{
	const char *ext = strrchr(filename, '.');

	if (ext)
		ext++;
	return ext;
}

static int str_in_array(const char *str, const char * const * array)
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
