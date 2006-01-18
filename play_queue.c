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

static struct simple_track *simple_track_new(struct track_info *ti)
{
	struct simple_track *t = xnew(struct simple_track, 1);

	track_info_ref(ti);
	t->info = ti;
	return t;
}

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
}

void __play_queue_prepend(struct track_info *ti)
{
	struct simple_track *t = simple_track_new(ti);

	list_add(&t->node, &queue_head);
	window_changed(play_queue_win);
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

struct track_info *play_queue_remove(void)
{
	struct list_head *item;
	struct simple_track *t;
	struct track_info *info;
	struct iter iter;

	play_queue_lock();
	item = queue_head.next;
	if (item == &queue_head) {
		play_queue_unlock();
		return NULL;
	}

	t = to_simple_track(item);
	queue_track_to_iter(t, &iter);
	window_row_vanishes(play_queue_win, &iter);
	list_del(item);

	info = t->info;
	free(t);
	play_queue_unlock();
	return info;
}

void play_queue_delete(void)
{
	struct iter iter;

	if (window_get_sel(play_queue_win, &iter)) {
		struct simple_track *t = iter_to_simple_track(&iter);

		window_row_vanishes(play_queue_win, &iter);
		list_del(&t->node);

		simple_track_free(t);
	}
}
