/*
 * Copyright 2008-2013 Various Authors
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "lib.h"
#include "editable.h"
#include "track_info.h"
#include "options.h"
#include "xmalloc.h"
#include "rbtree.h"
#include "debug.h"
#include "utils.h"
#include "ui_curses.h" /* cur_view */

#include <pthread.h>
#include <string.h>

struct editable lib_editable;
struct tree_track *lib_cur_track = NULL;
unsigned int play_sorted = 0;
enum aaa_mode aaa_mode = AAA_MODE_ALL;
/* used in ui_curses.c for status display */
char *lib_live_filter = NULL;

static struct rb_root lib_shuffle_root;
static struct expr *filter = NULL;
static int remove_from_hash = 1;

static struct expr *live_filter_expr = NULL;
static struct track_info *cur_track_ti = NULL;
static struct track_info *sel_track_ti = NULL;

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

static int hash_insert(struct track_info *ti)
{
	const char *filename = ti->filename;
	unsigned int pos = hash_str(filename) % FH_SIZE;
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
	unsigned int pos = hash_str(filename) % FH_SIZE;
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

static int is_filtered(struct track_info *ti)
{
	if (live_filter_expr && !expr_eval(live_filter_expr, ti))
		return 1;
	if (!live_filter_expr && lib_live_filter && !track_info_matches(ti, lib_live_filter, TI_MATCH_ALL))
		return 1;
	if (filter && !expr_eval(filter, ti))
		return 1;
	return 0;
}

void lib_add_track(struct track_info *ti)
{
	if (!hash_insert(ti)) {
		/* duplicate files not allowed */
		return;
	}
	if (!is_filtered(ti))
		views_add_track(ti);
}

static struct tree_track *album_first_track(const struct album *album)
{
	return to_tree_track(rb_first(&album->track_root));
}

static struct tree_track *artist_first_track(const struct artist *artist)
{
	return album_first_track(to_album(rb_first(&artist->album_root)));
}

static struct tree_track *normal_get_first(void)
{
	return artist_first_track(to_artist(rb_first(&lib_artist_root)));
}

static struct tree_track *album_last_track(const struct album *album)
{
	return to_tree_track(rb_last(&album->track_root));
}

static struct tree_track *artist_last_track(const struct artist *artist)
{
	return album_last_track(to_album(rb_last(&artist->album_root)));
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
	if (rb_next(&lib_cur_track->tree_node)) {
		/* next track of the current album */
		return to_tree_track(rb_next(&lib_cur_track->tree_node));
	}

	if (aaa_mode == AAA_MODE_ALBUM) {
		if (!repeat)
			return NULL;
		/* first track of the current album */
		return album_first_track(CUR_ALBUM);
	}

	/* not last album of the artist? */
	if (rb_next(&CUR_ALBUM->tree_node) != NULL) {
		/* first track of the next album */
		return album_first_track(to_album(rb_next(&CUR_ALBUM->tree_node)));
	}

	if (aaa_mode == AAA_MODE_ARTIST) {
		if (!repeat)
			return NULL;
		/* first track of the first album of the current artist */
		return artist_first_track(CUR_ARTIST);
	}

	/* not last artist of the library? */
	if (rb_next(&CUR_ARTIST->tree_node) != NULL) {
		/* first track of the next artist */
		return artist_first_track(to_artist(rb_next(&CUR_ARTIST->tree_node)));
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
	if (rb_prev(&lib_cur_track->tree_node)) {
		/* prev track of the album */
		return to_tree_track(rb_prev(&lib_cur_track->tree_node));
	}

	if (aaa_mode == AAA_MODE_ALBUM) {
		if (!repeat)
			return NULL;
		/* last track of the album */
		return album_last_track(CUR_ALBUM);
	}

	/* not first album of the artist? */
	if (rb_prev(&CUR_ALBUM->tree_node) != NULL) {
		/* last track of the prev album of the artist */
		return album_last_track(to_album(rb_prev(&CUR_ALBUM->tree_node)));
	}

	if (aaa_mode == AAA_MODE_ARTIST) {
		if (!repeat)
			return NULL;
		/* last track of the last album of the artist */
		return album_last_track(to_album(rb_last(&CUR_ARTIST->album_root)));
	}

	/* not first artist of the library? */
	if (rb_prev(&CUR_ARTIST->tree_node) != NULL) {
		/* last track of the last album of the prev artist */
		return artist_last_track(to_artist(rb_prev(&CUR_ARTIST->tree_node)));
	}

	if (!repeat)
		return NULL;

	/* last track */
	return artist_last_track(to_artist(rb_last(&lib_artist_root)));
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

struct track_info *lib_set_track(struct tree_track *track)
{
	struct track_info *ti = NULL;

	if (track) {
		lib_cur_track = track;
		ti = tree_track_info(track);
		track_info_ref(ti);
		if (follow) {
			tree_sel_current();
			sorted_sel_current();
		}
		all_wins_changed();
	}
	return ti;
}

struct track_info *lib_set_next(void)
{
	struct tree_track *track;

	if (rb_root_empty(&lib_artist_root)) {
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

	if (rb_root_empty(&lib_artist_root)) {
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

static struct tree_track *sorted_get_selected(void)
{
	struct iter sel;

	if (list_empty(&lib_editable.head))
		return NULL;

	window_get_sel(lib_editable.win, &sel);
	return iter_to_sorted_track(&sel);
}

struct track_info *sorted_set_selected(void)
{
	return lib_set_track(sorted_get_selected());
}

static void hash_add_to_views(void)
{
	int i;
	for (i = 0; i < FH_SIZE; i++) {
		struct fh_entry *e;

		e = ti_hash[i];
		while (e) {
			struct track_info *ti = e->ti;

			if (!is_filtered(ti))
				views_add_track(ti);
			e = e->next;
		}
	}
}

struct tree_track *lib_find_track(struct track_info *ti)
{
	struct simple_track *track;

	list_for_each_entry(track, &lib_editable.head, node) {
		if (strcmp(track->info->filename, ti->filename) == 0) {
			struct tree_track *tt = (struct tree_track *)track;
			return tt;
		}
	}
	return NULL;
}

void lib_store_cur_track(struct track_info *ti)
{
	if (cur_track_ti)
		track_info_unref(cur_track_ti);
	cur_track_ti = ti;
	track_info_ref(cur_track_ti);
}

struct track_info *lib_get_cur_stored_track(void)
{
	if (cur_track_ti && lib_find_track(cur_track_ti))
		return cur_track_ti;
	return NULL;
}

static void restore_cur_track(struct track_info *ti)
{
	struct tree_track *tt = lib_find_track(ti);
	if (tt)
		lib_cur_track = tt;
}

static int is_filtered_cb(void *data, struct track_info *ti)
{
	return is_filtered(ti);
}

static void do_lib_filter(int clear_before)
{
	/* try to save cur_track */
	if (lib_cur_track)
		lib_store_cur_track(tree_track_info(lib_cur_track));

	if (clear_before)
		d_print("filter results could grow, clear tracks and re-add (slow)\n");

	remove_from_hash = 0;
	if (clear_before) {
		editable_clear(&lib_editable);
		hash_add_to_views();
	} else
		editable_remove_matching_tracks(&lib_editable, is_filtered_cb, NULL);
	remove_from_hash = 1;

	window_changed(lib_editable.win);
	window_goto_top(lib_editable.win);
	lib_cur_win = lib_tree_win;
	window_goto_top(lib_tree_win);

	/* restore cur_track */
	if (cur_track_ti && !lib_cur_track)
		restore_cur_track(cur_track_ti);
}

static void unset_live_filter(void)
{
	free(lib_live_filter);
	lib_live_filter = NULL;
	free(live_filter_expr);
	live_filter_expr = NULL;
}

void lib_set_filter(struct expr *expr)
{
	int clear_before = lib_live_filter || filter;
	unset_live_filter();
	if (filter)
		expr_free(filter);
	filter = expr;
	do_lib_filter(clear_before);
}

static struct tree_track *get_sel_track(void)
{
	switch (cur_view) {
	case TREE_VIEW:
		return tree_get_selected();
	case SORTED_VIEW:
		return sorted_get_selected();
	}
	return NULL;
}

static void set_sel_track(struct tree_track *tt)
{
	struct iter iter;

	switch (cur_view) {
	case TREE_VIEW:
		tree_sel_track(tt);
		break;
	case SORTED_VIEW:
		sorted_track_to_iter(tt, &iter);
		window_set_sel(lib_editable.win, &iter);
		break;
	}
}

static void store_sel_track(void)
{
	struct tree_track *tt = get_sel_track();
	if (tt) {
		sel_track_ti = tree_track_info(tt);
		track_info_ref(sel_track_ti);
	}
}

static void restore_sel_track(void)
{
	if (sel_track_ti) {
		struct tree_track *tt = lib_find_track(sel_track_ti);
		if (tt) {
			set_sel_track(tt);
			track_info_unref(sel_track_ti);
			sel_track_ti = NULL;
		}
	}
}

/* determine if filter results could grow, in which case all tracks must be cleared and re-added */
static int do_clear_before(const char *str, struct expr *expr)
{
	if (!lib_live_filter)
		return 0;
	if (!str)
		return 1;
	if ((!expr && live_filter_expr) || (expr && !live_filter_expr))
		return 1;
	if (!expr || expr_is_harmless(expr))
		return !strstr(str, lib_live_filter);
	return 1;
}

void lib_set_live_filter(const char *str)
{
	int clear_before;
	struct expr *expr = NULL;

	if (strcmp0(str, lib_live_filter) == 0)
		return;

	if (str && expr_is_short(str)) {
		expr = expr_parse(str);
		if (!expr)
			return;
	}

	clear_before = do_clear_before(str, expr);

	if (!str)
		store_sel_track();

	unset_live_filter();
	lib_live_filter = str ? xstrdup(str) : NULL;
	live_filter_expr = expr;
	do_lib_filter(clear_before);

	if (expr) {
		unsigned int match_type = expr_get_match_type(expr);
		if (match_type & TI_MATCH_ALBUM)
			tree_expand_all();
		if (match_type & TI_MATCH_TITLE)
			tree_sel_first();
	} else if (str)
		tree_expand_matching(str);

	if (!str)
		restore_sel_track();
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
			if (!filtered || !filter || expr_eval(filter, e->ti))
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
