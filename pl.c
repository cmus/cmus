/*
 * Copyright 2006 Timo Hirvonen
 */

#include "pl.h"
#include "iter.h"
#include "window.h"
#include "track.h"
#include "search_mode.h"
#include "cmus.h"
#include "filters.h"
#include "expr.h"
#include "mergesort.h"
#include "xmalloc.h"

pthread_mutex_t pl_mutex = CMUS_MUTEX_INITIALIZER;
struct window *pl_win;
struct searchable *pl_searchable;

/* can be casted to struct shuffle_track */
struct simple_track *pl_cur_track = NULL;

unsigned int pl_nr_tracks = 0;
unsigned int pl_total_time = 0;
unsigned int pl_nr_marked = 0;
char **pl_sort_keys;

static LIST_HEAD(pl_sorted_head);
static LIST_HEAD(pl_shuffle_head);

static inline void pl_track_to_iter(struct simple_track *track, struct iter *iter)
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
		track = simple_list_get_next(&pl_sorted_head, pl_cur_track, dummy_filter);
	}
	if (track) {
		pl_cur_track = track;
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
		track = simple_list_get_prev(&pl_sorted_head, pl_cur_track, dummy_filter);
	}
	if (track) {
		pl_cur_track = track;
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
	pl_cur_track = iter_to_simple_track(&sel);
	ti = pl_cur_track->info;
	track_info_ref(ti);
	pl_win->changed = 1;

	pl_unlock();
	return ti;
}

static struct simple_track *pl_track_new(struct track_info *ti)
{
	struct shuffle_track *t = xnew(struct shuffle_track, 1);

	track_info_ref(ti);
	simple_track_init((struct simple_track *)t, ti);
	return (struct simple_track *)t;
}

static void pl_track_free(struct simple_track *track)
{
	track_info_unref(track->info);
	free(track);
}

static void pl_remove_and_free(struct simple_track *track)
{
	struct track_info *ti = track->info;
	struct iter iter;

	pl_track_to_iter(track, &iter);
	window_row_vanishes(pl_win, &iter);

	pl_nr_tracks--;
	pl_nr_marked -= track->marked;
	if (ti->duration != -1)
		pl_total_time -= ti->duration;
	if (pl_cur_track == track)
		pl_cur_track = NULL;

	list_del(&track->node);
	list_del(&((struct shuffle_track *)track)->node);

	pl_track_free(track);
}

static void shuffle_add(struct simple_track *track)
{
	struct shuffle_track *t = (struct shuffle_track *)track;

	shuffle_list_add_track(&pl_shuffle_head, &t->node, pl_nr_tracks);
}

void pl_add_track(struct track_info *ti)
{
	struct simple_track *track = pl_track_new(ti);

	pl_lock();
	shuffle_add(track);
	sorted_list_add_track(&pl_sorted_head, track, pl_sort_keys);
	pl_nr_tracks++;
	if (ti->duration != -1)
		pl_total_time += ti->duration;
	window_changed(pl_win);
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

static struct simple_track *get_selected(void)
{
	struct iter sel;

	if (window_get_sel(pl_win, &sel))
		return iter_to_simple_track(&sel);
	return NULL;
}

void pl_remove_sel(void)
{
	struct simple_track *t;

	pl_lock();
	if (pl_nr_marked) {
		/* treat marked tracks as selected */
		struct list_head *next, *item = pl_sorted_head.next;

		while (item != &pl_sorted_head) {
			next = item->next;
			t = to_simple_track(item);
			if (t->marked)
				pl_remove_and_free(t);
			item = next;
		}
	} else {
		t = get_selected();
		if (t)
			pl_remove_and_free(t);
	}
	pl_unlock();
}

void pl_toggle_mark(void)
{
	struct simple_track *t;

	pl_lock();
	t = get_selected();
	if (t) {
		pl_nr_marked -= t->marked;
		t->marked ^= 1;
		pl_nr_marked += t->marked;
		pl_win->changed = 1;
		window_down(pl_win, 1);
	}
	pl_unlock();
}

static void move_item(struct list_head *head, struct list_head *item)
{
	struct simple_track *t = to_simple_track(item);
	struct iter iter;

	pl_track_to_iter(t, &iter);
	window_row_vanishes(pl_win, &iter);

	list_del(item);
	list_add(item, head);
}

static void move_sel(struct list_head *after)
{
	struct simple_track *t;
	struct list_head *item, *next;
	struct iter iter;
	LIST_HEAD(tmp_head);

	if (pl_nr_marked) {
		/* collect marked */
		item = pl_sorted_head.next;
		while (item != &pl_sorted_head) {
			t = to_simple_track(item);
			next = item->next;
			if (t->marked)
				move_item(&tmp_head, item);
			item = next;
		}
	} else {
		/* collect the selected track */
		t = get_selected();
		move_item(&tmp_head, &t->node);
	}

	/* put them back to the list after @after */
	item = tmp_head.next;
	while (item != &tmp_head) {
		next = item->next;
		list_add(item, after);
		item = next;
	}

	/* select top-most of the moved tracks */
	pl_track_to_iter(to_simple_track(after->next), &iter);
	window_set_sel(pl_win, &iter);
	window_changed(pl_win);
}

static struct list_head *find_insert_after_point(struct list_head *item)
{
	if (pl_nr_marked == 0) {
		/* move the selected track down one row */
		return item->next;
	}

	/* move marked after the selected
	 *
	 * if the selected track itself is marked we find the first unmarked
	 * track (or head) before the selected one
	 */
	while (item != &pl_sorted_head) {
		struct simple_track *t = to_simple_track(item);

		if (!t->marked)
			break;
		item = item->prev;
	}
	return item;
}

static struct list_head *find_insert_before_point(struct list_head *item)
{
	item = item->prev;
	if (pl_nr_marked == 0) {
		/* move the selected track up one row */
		return item->prev;
	}

	/* move marked before the selected
	 *
	 * if the selected track itself is marked we find the first unmarked
	 * track (or head) before the selected one
	 */
	while (item != &pl_sorted_head) {
		struct simple_track *t = to_simple_track(item);

		if (!t->marked)
			break;
		item = item->prev;
	}
	return item;
}

void pl_move_after(void)
{
	struct simple_track *sel;

	pl_lock();
	if (pl_sort_keys[0] == NULL && (sel = get_selected()))
		move_sel(find_insert_after_point(&sel->node));
	pl_unlock();
}

void pl_move_before(void)
{
	struct simple_track *sel;

	pl_lock();
	if (pl_sort_keys[0] == NULL && (sel = get_selected()))
		move_sel(find_insert_before_point(&sel->node));
	pl_unlock();
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
		pl_remove_and_free(to_simple_track(item));
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

void pl_mark(const char *filter)
{
	struct expr *e = NULL;
	struct simple_track *t;

	if (filter) {
		e = parse_filter(filter);
		if (e == NULL)
			return;
	}

	pl_lock();
	list_for_each_entry(t, &pl_sorted_head, node) {
		pl_nr_marked -= t->marked;
		t->marked = 0;
		if (e == NULL || expr_eval(e, t->info)) {
			t->marked = 1;
			pl_nr_marked++;
		}
	}
	pl_win->changed = 1;
	pl_unlock();
}

void pl_unmark(void)
{
	struct simple_track *t;

	pl_lock();
	list_for_each_entry(t, &pl_sorted_head, node) {
		pl_nr_marked -= t->marked;
		t->marked = 0;
	}
	pl_win->changed = 1;
	pl_unlock();
}

void pl_invert_marks(void)
{
	struct simple_track *t;

	pl_lock();
	list_for_each_entry(t, &pl_sorted_head, node) {
		pl_nr_marked -= t->marked;
		t->marked ^= 1;
		pl_nr_marked += t->marked;
	}
	pl_win->changed = 1;
	pl_unlock();
}

int pl_for_each_sel(int (*cb)(void *data, struct track_info *ti), void *data, int reverse)
{
	int rc = 0;

	pl_lock();
	if (pl_nr_marked) {
		/* treat marked tracks as selected */
		rc = simple_list_for_each_marked(&pl_sorted_head, cb, data, reverse);
	} else {
		struct simple_track *t = get_selected();

		if (t)
			rc = cb(data, t->info);
	}
	window_down(pl_win, 1);
	pl_unlock();
	return rc;
}

int pl_for_each(int (*cb)(void *data, struct track_info *ti), void *data)
{
	struct simple_track *t;
	int rc = 0;

	pl_lock();
	list_for_each_entry(t, &pl_sorted_head, node) {
		rc = cb(data, t->info);
		if (rc)
			break;
	}
	pl_unlock();
	return rc;
}
