/*
 * Copyright 2004-2006 Timo Hirvonen
 */

#include "lib.h"
#include "editable.h"
#include "track_info.h"
#include "options.h"
#include "xmalloc.h"
#include "rbtree.h"
#include "debug.h"

#include <pthread.h>
#include <string.h>

struct editable lib_editable;
struct tree_track *lib_cur_track = NULL;
unsigned int play_sorted = 0;
enum aaa_mode aaa_mode = AAA_MODE_ALL;

static struct rb_root lib_shuffle_root;
static struct expr *filter = NULL;
static int remove_from_hash = 1;

const char *artist_sort_name(const struct artist *a)
{
	if (a->sort_name)
		return a->sort_name;

	if (smart_artist_sort && a->auto_sort_name)
		return a->auto_sort_name;

	return a->name;
}

static inline struct tree_track *to_sorted(const struct list_head *item)
{
	return (struct tree_track *)container_of(item, struct simple_track, node);
}

static inline void sorted_track_to_iter(struct tree_track *track, struct iter *iter)
{
	iter->data0 = &lib_editable.head;
	iter->data1 = track;
	iter->data2 = NULL;
}

static void all_wins_changed(void)
{
	lib_tree_win->changed = 1;
	lib_track_win->changed = 1;
	lib_editable.win->changed = 1;
}

static void shuffle_add(struct tree_track *track)
{
	shuffle_list_add(&track->shuffle_track, &lib_shuffle_root);
}

static void views_add_track(struct track_info *ti)
{
	struct tree_track *track = xnew(struct tree_track, 1);

	/* NOTE: does not ref ti */
	simple_track_init((struct simple_track *)track, ti);

	/* both the hash table and views have refs */
	track_info_ref(ti);

	tree_add_track(track);
	shuffle_add(track);
	editable_add(&lib_editable, (struct simple_track *)track);
}

struct fh_entry {
	struct fh_entry *next;

	/* ref count is increased when added to this hash */
	struct track_info *ti;
};

#define FH_SIZE (1024)
static struct fh_entry *ti_hash[FH_SIZE] = { NULL, };

/* this is from glib */
static unsigned int str_hash(const char *str)
{
	unsigned int hash = 0;
	int i;

	for (i = 0; str[i]; i++)
		hash = (hash << 5) - hash + str[i];
	return hash;
}

static int hash_insert(struct track_info *ti)
{
	const char *filename = ti->filename;
	unsigned int pos = str_hash(filename) % FH_SIZE;
	struct fh_entry **entryp;
	struct fh_entry *e;

	entryp = &ti_hash[pos];
	e = *entryp;
	while (e) {
		if (strcmp(e->ti->filename, filename) == 0) {
			/* found, don't insert */
			return 0;
		}
		e = e->next;
	}

	e = xnew(struct fh_entry, 1);
	track_info_ref(ti);
	e->ti = ti;
	e->next = *entryp;
	*entryp = e;
	return 1;
}

static void hash_remove(struct track_info *ti)
{
	const char *filename = ti->filename;
	unsigned int pos = str_hash(filename) % FH_SIZE;
	struct fh_entry **entryp;

	entryp = &ti_hash[pos];
	while (1) {
		struct fh_entry *e = *entryp;

		BUG_ON(e == NULL);
		if (strcmp(e->ti->filename, filename) == 0) {
			*entryp = e->next;
			track_info_unref(e->ti);
			free(e);
			break;
		}
		entryp = &e->next;
	}
}

void lib_add_track(struct track_info *ti)
{
	if (!hash_insert(ti)) {
		/* duplicate files not allowed */
		return;
	}
	if (filter == NULL || expr_eval(filter, ti))
		views_add_track(ti);
}

static struct tree_track *album_first_track(const struct album *album)
{
	return to_tree_track(album->track_head.next);
}

static struct tree_track *artist_first_track(const struct artist *artist)
{
	return album_first_track(to_album(artist->album_head.next));
}

static struct tree_track *normal_get_first(void)
{
	return artist_first_track(to_artist(lib_artist_head.next));
}

static struct tree_track *album_last_track(const struct album *album)
{
	return to_tree_track(album->track_head.prev);
}

static struct tree_track *artist_last_track(const struct artist *artist)
{
	return album_last_track(to_album(artist->album_head.prev));
}

static int aaa_mode_filter(const struct simple_track *track)
{
	const struct album *album = ((struct tree_track *)track)->album;

	if (aaa_mode == AAA_MODE_ALBUM)
		return CUR_ALBUM == album;

	if (aaa_mode == AAA_MODE_ARTIST)
		return CUR_ARTIST == album->artist;

	/* AAA_MODE_ALL */
	return 1;
}

/* set next/prev (tree) {{{ */

static struct tree_track *normal_get_next(void)
{
	if (lib_cur_track == NULL)
		return normal_get_first();

	/* not last track of the album? */
	if (lib_cur_track->node.next != &CUR_ALBUM->track_head) {
		/* next track of the current album */
		return to_tree_track(lib_cur_track->node.next);
	}

	if (aaa_mode == AAA_MODE_ALBUM) {
		if (!repeat)
			return NULL;
		/* first track of the current album */
		return album_first_track(CUR_ALBUM);
	}

	/* not last album of the artist? */
	if (CUR_ALBUM->node.next != &CUR_ARTIST->album_head) {
		/* first track of the next album */
		return album_first_track(to_album(CUR_ALBUM->node.next));
	}

	if (aaa_mode == AAA_MODE_ARTIST) {
		if (!repeat)
			return NULL;
		/* first track of the first album of the current artist */
		return artist_first_track(CUR_ARTIST);
	}

	/* not last artist of the library? */
	if (CUR_ARTIST->node.next != &lib_artist_head) {
		/* first track of the next artist */
		return artist_first_track(to_artist(CUR_ARTIST->node.next));
	}

	if (!repeat)
		return NULL;

	/* first track */
	return normal_get_first();
}

static struct tree_track *normal_get_prev(void)
{
	if (lib_cur_track == NULL)
		return normal_get_first();

	/* not first track of the album? */
	if (lib_cur_track->node.prev != &CUR_ALBUM->track_head) {
		/* prev track of the album */
		return to_tree_track(lib_cur_track->node.prev);
	}

	if (aaa_mode == AAA_MODE_ALBUM) {
		if (!repeat)
			return NULL;
		/* last track of the album */
		return to_tree_track(CUR_ALBUM->track_head.prev);
	}

	/* not first album of the artist? */
	if (CUR_ALBUM->node.prev != &CUR_ARTIST->album_head) {
		/* last track of the prev album of the artist */
		return album_last_track(to_album(CUR_ALBUM->node.prev));
	}

	if (aaa_mode == AAA_MODE_ARTIST) {
		if (!repeat)
			return NULL;
		/* last track of the last album of the artist */
		return album_last_track(to_album(CUR_ARTIST->album_head.prev));
	}

	/* not first artist of the library? */
	if (CUR_ARTIST->node.prev != &lib_artist_head) {
		/* last track of the last album of the prev artist */
		return artist_last_track(to_artist(CUR_ARTIST->node.prev));
	}

	if (!repeat)
		return NULL;

	/* last track */
	return artist_last_track(to_artist(lib_artist_head.prev));
}

/* set next/prev (tree) }}} */

void lib_reshuffle(void)
{
	shuffle_list_reshuffle(&lib_shuffle_root);
}

static void free_lib_track(struct list_head *item)
{
	struct tree_track *track = (struct tree_track *)to_simple_track(item);
	struct track_info *ti = tree_track_info(track);

	if (track == lib_cur_track)
		lib_cur_track = NULL;

	if (remove_from_hash)
		hash_remove(ti);

	rb_erase(&track->shuffle_track.tree_node, &lib_shuffle_root);
	tree_remove(track);

	track_info_unref(ti);
	free(track);
}

void lib_init(void)
{
	editable_init(&lib_editable, free_lib_track);
	tree_init();
	srand(time(NULL));
}

static struct track_info *lib_set_track(struct tree_track *track)
{
	struct track_info *ti = NULL;

	if (track) {
		lib_cur_track = track;
		ti = tree_track_info(track);
		track_info_ref(ti);
		all_wins_changed();
	}
	return ti;
}

struct track_info *lib_set_next(void)
{
	struct tree_track *track;

	if (list_empty(&lib_artist_head)) {
		BUG_ON(lib_cur_track != NULL);
		return NULL;
	}
	if (shuffle) {
		track = (struct tree_track *)shuffle_list_get_next(&lib_shuffle_root,
				(struct shuffle_track *)lib_cur_track, aaa_mode_filter);
	} else if (play_sorted) {
		track = (struct tree_track *)simple_list_get_next(&lib_editable.head,
				(struct simple_track *)lib_cur_track, aaa_mode_filter);
	} else {
		track = normal_get_next();
	}
	return lib_set_track(track);
}

struct track_info *lib_set_prev(void)
{
	struct tree_track *track;

	if (list_empty(&lib_artist_head)) {
		BUG_ON(lib_cur_track != NULL);
		return NULL;
	}
	if (shuffle) {
		track = (struct tree_track *)shuffle_list_get_prev(&lib_shuffle_root,
				(struct shuffle_track *)lib_cur_track, aaa_mode_filter);
	} else if (play_sorted) {
		track = (struct tree_track *)simple_list_get_prev(&lib_editable.head,
				(struct simple_track *)lib_cur_track, aaa_mode_filter);
	} else {
		track = normal_get_prev();
	}
	return lib_set_track(track);
}

struct track_info *sorted_set_selected(void)
{
	struct iter sel;

	if (list_empty(&lib_editable.head))
		return NULL;

	window_get_sel(lib_editable.win, &sel);
	return lib_set_track(iter_to_sorted_track(&sel));
}

void lib_set_filter(struct expr *expr)
{
	static const char *tmp_keys[1] = { NULL };
	struct track_info *cur_ti = NULL;
	const char **sort_keys;
	int i;

	/* try to save cur_track */
	if (lib_cur_track) {
		cur_ti = tree_track_info(lib_cur_track);
		track_info_ref(cur_ti);
	}

	remove_from_hash = 0;
	editable_clear(&lib_editable);
	remove_from_hash = 1;

	if (filter)
		expr_free(filter);
	filter = expr;

	/* disable sorting temporarily */
	sort_keys = lib_editable.sort_keys;
	lib_editable.sort_keys = tmp_keys;

	for (i = 0; i < FH_SIZE; i++) {
		struct fh_entry *e;

		e = ti_hash[i];
		while (e) {
			struct track_info *ti = e->ti;

			if (filter == NULL || expr_eval(filter, ti))
				views_add_track(ti);
			e = e->next;
		}
	}

	/* enable sorting */
	lib_editable.sort_keys = sort_keys;
	editable_sort(&lib_editable);

	lib_cur_win = lib_tree_win;
	window_goto_top(lib_tree_win);

	/* restore cur_track */
	if (cur_ti) {
		struct simple_track *track;

		list_for_each_entry(track, &lib_editable.head, node) {
			if (strcmp(track->info->filename, cur_ti->filename) == 0) {
				struct tree_track *tt = (struct tree_track *)track;

				lib_cur_track = tt;
				break;
			}
		}
		track_info_unref(cur_ti);
	}
}

int lib_remove(struct track_info *ti)
{
	struct simple_track *track;

	list_for_each_entry(track, &lib_editable.head, node) {
		if (track->info == ti) {
			editable_remove_track(&lib_editable, track);
			return 1;
		}
	}
	return 0;
}

void lib_clear_store(void)
{
	int i;

	for (i = 0; i < FH_SIZE; i++) {
		struct fh_entry *e, *next;

		e = ti_hash[i];
		while (e) {
			next = e->next;
			track_info_unref(e->ti);
			free(e);
			e = next;
		}
		ti_hash[i] = NULL;
	}
}

void sorted_sel_current(void)
{
	if (lib_cur_track) {
		struct iter iter;

		sorted_track_to_iter(lib_cur_track, &iter);
		window_set_sel(lib_editable.win, &iter);
	}
}

static int ti_cmp(const void *a, const void *b)
{
	const struct track_info *ai = *(const struct track_info **)a;
	const struct track_info *bi = *(const struct track_info **)b;

	return track_info_cmp(ai, bi, lib_editable.sort_keys);
}

static int do_lib_for_each(int (*cb)(void *data, struct track_info *ti), void *data, int filtered)
{
	int i, rc = 0, count = 0, size = 1024;
	struct track_info **tis;

	tis = xnew(struct track_info *, size);

	/* collect all track_infos */
	for (i = 0; i < FH_SIZE; i++) {
		struct fh_entry *e;

		e = ti_hash[i];
		while (e) {
			if (count == size) {
				size *= 2;
				tis = xrenew(struct track_info *, tis, size);
			}
			if (!filtered || filter == NULL || expr_eval(filter, e->ti))
				tis[count++] = e->ti;
			e = e->next;
		}
	}

	/* sort to speed up playlist loading */
	qsort(tis, count, sizeof(struct track_info *), ti_cmp);
	for (i = 0; i < count; i++) {
		rc = cb(data, tis[i]);
		if (rc)
			break;
	}

	free(tis);
	return rc;
}

int lib_for_each(int (*cb)(void *data, struct track_info *ti), void *data)
{
	return do_lib_for_each(cb, data, 0);
}

int lib_for_each_filtered(int (*cb)(void *data, struct track_info *ti), void *data)
{
	return do_lib_for_each(cb, data, 1);
}
