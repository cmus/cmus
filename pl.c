/*
 * Copyright 2006 Timo Hirvonen
 */

#include "pl.h"
#include "iter.h"
#include "window.h"
#include "track.h"
/* #include "search.h" */
#include "search_mode.h"
#include "cmus.h"
#include "mergesort.h"
#include "xmalloc.h"

pthread_mutex_t pl_mutex = CMUS_MUTEX_INITIALIZER;
struct window *pl_win;
struct searchable *pl_searchable;
struct pl_track *pl_cur_track = NULL;
unsigned int pl_nr_tracks = 0;
unsigned int pl_total_time = 0;
unsigned int pl_nr_marked = 0;
char **pl_sort_keys;

static LIST_HEAD(pl_sorted_head);
static LIST_HEAD(pl_shuffle_head);

static inline struct pl_track *to_track(const struct list_head *item)
{
	return (struct pl_track *)container_of(item, struct simple_track, node);
}

static inline struct pl_track *to_shuffle(const struct list_head *item)
{
	return (struct pl_track *)container_of(item, struct shuffle_track, node);
}

static inline void pl_track_to_iter(struct pl_track *track, struct iter *iter)
{
	iter->data0 = &pl_sorted_head;
	iter->data1 = track;
	iter->data2 = NULL;
}

static void search_lock(void *data)
{
	pl_lock();
}

static void search_unlock(void *data)
{
	pl_unlock();
}

static const struct searchable_ops pl_search_ops = {
	.lock = search_lock,
	.unlock = search_unlock,
	.get_prev = simple_track_get_prev,
	.get_next = simple_track_get_next,
	.get_current = simple_track_search_get_current,
	.matches = simple_track_search_matches
};

void pl_init(void)
{
	struct iter iter;

	pl_win = window_new(simple_track_get_prev, simple_track_get_next);
	window_set_contents(pl_win, &pl_sorted_head);

	iter.data0 = &pl_sorted_head;
	iter.data1 = NULL;
	iter.data2 = NULL;
	pl_searchable = searchable_new(pl_win, &iter, &pl_search_ops);

	pl_sort_keys = xnew(char *, 1);
	pl_sort_keys[0] = NULL;
}

static int dummy_filter(const struct simple_track *track)
{
	return 1;
}

struct track_info *pl_set_next(void)
{
	struct simple_track *track;
	struct track_info *ti = NULL;

	pl_lock();
	if (list_empty(&pl_sorted_head)) {
		BUG_ON(pl_cur_track != NULL);
		pl_unlock();
		return NULL;
	}
	if (shuffle) {
		track = (struct simple_track *)shuffle_list_get_next(&pl_shuffle_head,
				(struct shuffle_track *)pl_cur_track, dummy_filter);
	} else {
		track = simple_list_get_next(&pl_sorted_head,
				(struct simple_track *)pl_cur_track, dummy_filter);
	}
	if (track) {
		pl_cur_track = (struct pl_track *)track;
		ti = track->info;

		track_info_ref(ti);
		pl_win->changed = 1;
	}
	pl_unlock();
	return ti;
}

struct track_info *pl_set_prev(void)
{
	struct simple_track *track;
	struct track_info *ti = NULL;

	pl_lock();
	if (list_empty(&pl_sorted_head)) {
		BUG_ON(pl_cur_track != NULL);
		pl_unlock();
		return NULL;
	}
	if (shuffle) {
		track = (struct simple_track *)shuffle_list_get_prev(&pl_shuffle_head,
				(struct shuffle_track *)pl_cur_track, dummy_filter);
	} else {
		track = simple_list_get_prev(&pl_sorted_head,
				(struct simple_track *)pl_cur_track, dummy_filter);
	}
	if (track) {
		pl_cur_track = (struct pl_track *)track;
		ti = track->info;

		track_info_ref(ti);
		pl_win->changed = 1;
	}
	pl_unlock();
	return ti;
}

struct track_info *pl_set_selected(void)
{
	struct track_info *ti;
	struct iter sel;

	pl_lock();
	if (list_empty(&pl_sorted_head)) {
		BUG_ON(pl_cur_track != NULL);
		pl_unlock();
		return NULL;
	}

	window_get_sel(pl_win, &sel);
	pl_cur_track = iter_to_pl_track(&sel);
	ti = ((struct simple_track *)pl_cur_track)->info;
	track_info_ref(ti);
	pl_win->changed = 1;

	pl_unlock();
	return ti;
}

static struct pl_track *pl_track_new(struct track_info *ti)
{
	struct pl_track *t = xnew(struct pl_track, 1);

	track_info_ref(ti);
	simple_track_init((struct simple_track *)t, ti);

	t->marked = 0;
	return t;
}

static void pl_track_free(struct pl_track *track)
{
	track_info_unref(track->shuffle_track.simple_track.info);
	free(track);
}

static void remove_and_free(struct pl_track *track)
{
	struct track_info *ti = pl_track_info(track);
	struct iter iter;

	pl_track_to_iter(track, &iter);
	window_row_vanishes(pl_win, &iter);

	pl_nr_tracks--;
	pl_nr_marked -= track->marked;
	if (ti->duration != -1)
		pl_total_time -= ti->duration;

	list_del(&((struct simple_track *)track)->node);
	list_del(&((struct shuffle_track *)track)->node);

	pl_track_free(track);
}

static void shuffle_add(struct pl_track *track)
{
	shuffle_list_add_track(&pl_shuffle_head, &track->shuffle_track.node, pl_nr_tracks);
}

void pl_add_track(struct track_info *ti)
{
	struct pl_track *track = pl_track_new(ti);

	pl_lock();
	shuffle_add(track);
	sorted_list_add_track(&pl_sorted_head, (struct simple_track *)track, pl_sort_keys);
	pl_unlock();
}

static int pl_view_cmp(const struct list_head *a_head, const struct list_head *b_head)
{
	return simple_track_cmp(a_head, b_head, pl_sort_keys);
}

static void sort_sorted_list(void)
{
	list_mergesort(&pl_sorted_head, pl_view_cmp);
}

void pl_set_sort_keys(char **keys)
{
	pl_lock();
	free_str_array(pl_sort_keys);
	pl_sort_keys = keys;
	sort_sorted_list();
	window_changed(pl_win);
	window_goto_top(pl_win);
	pl_unlock();
}

static struct pl_track *get_selected(void)
{
	struct iter sel;

	if (window_get_sel(pl_win, &sel))
		return iter_to_pl_track(&sel);
	return NULL;
}

void pl_remove_sel(void)
{
	struct pl_track *t = get_selected();

	if (t)
		remove_and_free(t);
}

void pl_toggle_mark(void)
{
	struct pl_track *t = get_selected();

	if (t) {
		t->marked ^= 1;
		pl_win->changed = 1;
	}
}

void pl_sel_current(void)
{
	pl_lock();
	if (pl_cur_track) {
		struct iter iter;

		pl_track_to_iter(pl_cur_track, &iter);
		window_set_sel(pl_win, &iter);
	}
	pl_unlock();
}

void pl_clear(void)
{
	struct list_head *item, *next;

	pl_lock();
	item = pl_sorted_head.next;
	while (item != &pl_sorted_head) {
		next = item->next;
		remove_and_free(to_track(item));
		item = next;
	}
	pl_unlock();
}

void pl_reshuffle(void)
{
	pl_lock();
	reshuffle(&pl_shuffle_head);
	pl_unlock();
}
