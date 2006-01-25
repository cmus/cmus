/*
 * Copyright 2005-2006 Timo Hirvonen
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

#include "play_queue.h"
#include "window.h"
#include "track.h"
#include "search.h"
#include "list.h"
#include "xmalloc.h"

pthread_mutex_t play_queue_mutex = CMUS_MUTEX_INITIALIZER;
struct window *play_queue_win;
struct searchable *play_queue_searchable;

static LIST_HEAD(queue_head);
unsigned int pq_nr_tracks = 0;
unsigned int pq_nr_marked = 0;

static void simple_track_free(struct simple_track *track)
{
	track_info_unref(track->info);
	free(track);
}

static inline void queue_track_to_iter(struct simple_track *track, struct iter *iter)
{
	iter->data0 = &queue_head;
	iter->data1 = track;
	iter->data2 = NULL;
}

static void search_lock(void *data)
{
	play_queue_lock();
}

static void search_unlock(void *data)
{
	play_queue_unlock();
}

static const struct searchable_ops play_queue_search_ops = {
	.lock = search_lock,
	.unlock = search_unlock,
	.get_prev = simple_track_get_prev,
	.get_next = simple_track_get_next,
	.get_current = simple_track_search_get_current,
	.matches = simple_track_search_matches
};

void play_queue_init(void)
{
	struct iter iter;

	play_queue_win = window_new(simple_track_get_prev, simple_track_get_next);
	window_set_contents(play_queue_win, &queue_head);
	window_changed(play_queue_win);

	iter.data0 = &queue_head;
	iter.data1 = NULL;
	iter.data2 = NULL;
	play_queue_searchable = searchable_new(play_queue_win, &iter, &play_queue_search_ops);
}

void play_queue_exit(void)
{
	struct list_head *item;

	searchable_free(play_queue_searchable);
	item = queue_head.next;
	while (item != &queue_head) {
		struct list_head *next = item->next;
		struct simple_track *t = to_simple_track(item);

		simple_track_free(t);
		item = next;
	}
	list_init(&queue_head);
	window_free(play_queue_win);
}

void __play_queue_append(struct track_info *ti)
{
	struct simple_track *t = simple_track_new(ti);

	list_add_tail(&t->node, &queue_head);
	window_changed(play_queue_win);
	pq_nr_tracks++;
}

void __play_queue_prepend(struct track_info *ti)
{
	struct simple_track *t = simple_track_new(ti);

	list_add(&t->node, &queue_head);
	window_changed(play_queue_win);
	pq_nr_tracks++;
}

void play_queue_append(struct track_info *ti)
{
	play_queue_lock();
	__play_queue_append(ti);
	play_queue_unlock();
}

void play_queue_prepend(struct track_info *ti)
{
	play_queue_lock();
	__play_queue_prepend(ti);
	play_queue_unlock();
}

static void pq_remove(struct simple_track *track)
{
	struct iter iter;

	queue_track_to_iter(track, &iter);
	window_row_vanishes(play_queue_win, &iter);

	pq_nr_marked -= track->marked;
	pq_nr_tracks--;
	list_del(&track->node);
}

static void pq_remove_and_free(struct simple_track *track)
{
	pq_remove(track);
	simple_track_free(track);
}

struct track_info *play_queue_remove(void)
{
	struct list_head *item;
	struct simple_track *t;
	struct track_info *info;

	play_queue_lock();
	item = queue_head.next;
	if (item == &queue_head) {
		play_queue_unlock();
		return NULL;
	}

	t = to_simple_track(item);
	pq_remove(t);

	info = t->info;
	free(t);
	play_queue_unlock();
	return info;
}

static struct simple_track *pq_get_sel(void)
{
	struct iter sel;

	if (window_get_sel(play_queue_win, &sel))
		return iter_to_simple_track(&sel);
	return NULL;
}

void play_queue_remove_sel(void)
{
	struct simple_track *t;

	play_queue_lock();
	if (pq_nr_marked) {
		/* treat marked tracks as selected */
		struct list_head *next, *item = queue_head.next;

		while (item != &queue_head) {
			next = item->next;
			t = to_simple_track(item);
			if (t->marked)
				pq_remove_and_free(t);
			item = next;
		}
	} else {
		t = pq_get_sel();
		if (t)
			pq_remove_and_free(t);
	}
	play_queue_unlock();
}

void play_queue_toggle_mark(void)
{
	struct simple_track *t;

	play_queue_lock();
	t = pq_get_sel();
	if (t) {
		pq_nr_marked -= t->marked;
		t->marked ^= 1;
		pq_nr_marked += t->marked;
		play_queue_win->changed = 1;
		window_down(play_queue_win, 1);
	}
	play_queue_unlock();
}

static void move_item(struct list_head *head, struct list_head *item)
{
	struct simple_track *t = to_simple_track(item);
	struct iter iter;

	queue_track_to_iter(t, &iter);
	window_row_vanishes(play_queue_win, &iter);

	list_del(item);
	list_add(item, head);
}

static void move_sel(struct list_head *after)
{
	struct simple_track *t;
	struct list_head *item, *next;
	struct iter iter;
	LIST_HEAD(tmp_head);

	if (pq_nr_marked) {
		/* collect marked */
		item = queue_head.next;
		while (item != &queue_head) {
			t = to_simple_track(item);
			next = item->next;
			if (t->marked)
				move_item(&tmp_head, item);
			item = next;
		}
	} else {
		/* collect the selected track */
		t = pq_get_sel();
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
	queue_track_to_iter(to_simple_track(after->next), &iter);
	window_set_sel(play_queue_win, &iter);
	window_changed(play_queue_win);
}

static struct list_head *find_insert_after_point(struct list_head *item)
{
	if (pq_nr_marked == 0) {
		/* move the selected track down one row */
		return item->next;
	}

	/* move marked after the selected
	 *
	 * if the selected track itself is marked we find the first unmarked
	 * track (or head) before the selected one
	 */
	while (item != &queue_head) {
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
	if (pq_nr_marked == 0) {
		/* move the selected track up one row */
		return item->prev;
	}

	/* move marked before the selected
	 *
	 * if the selected track itself is marked we find the first unmarked
	 * track (or head) before the selected one
	 */
	while (item != &queue_head) {
		struct simple_track *t = to_simple_track(item);

		if (!t->marked)
			break;
		item = item->prev;
	}
	return item;
}

void play_queue_move_after(void)
{
	struct simple_track *sel;

	play_queue_lock();
	if ((sel = pq_get_sel()))
		move_sel(find_insert_after_point(&sel->node));
	play_queue_unlock();
}

void play_queue_move_before(void)
{
	struct simple_track *sel;

	play_queue_lock();
	if ((sel = pq_get_sel()))
		move_sel(find_insert_before_point(&sel->node));
	play_queue_unlock();
}

void play_queue_clear(void)
{
	struct list_head *item, *next;

	play_queue_lock();
	item = queue_head.next;
	while (item != &queue_head) {
		next = item->next;
		pq_remove_and_free(to_simple_track(item));
		item = next;
	}
	play_queue_unlock();
}

int play_queue_for_each_sel(int (*cb)(void *data, struct track_info *ti), void *data, int reverse)
{
	int rc = 0;

	play_queue_lock();
	if (pq_nr_marked) {
		/* treat marked tracks as selected */
		rc = simple_list_for_each_marked(&queue_head, cb, data, reverse);
	} else {
		struct simple_track *t = pq_get_sel();

		if (t)
			rc = cb(data, t->info);
	}
	window_down(play_queue_win, 1);
	play_queue_unlock();
	return rc;
}
