/*
 * Copyright 2005-2006 Timo Hirvonen
 */

#include "play_queue.h"
#include "editable.h"
#include "track.h"
#include "xmalloc.h"

struct editable pq_editable;

static void pq_free_track(struct list_head *item)
{
	struct simple_track *track = to_simple_track(item);

	track_info_unref(track->info);
	free(track);
}

void play_queue_init(void)
{
	editable_init(&pq_editable, pq_free_track);
}

void play_queue_append(struct track_info *ti)
{
	struct simple_track *t = simple_track_new(ti);

	editable_add(&pq_editable, t);
}

void play_queue_prepend(struct track_info *ti)
{
	struct simple_track *t = simple_track_new(ti);

	list_add(&t->node, &pq_editable.head);
	pq_editable.nr_tracks++;
	if (t->info->duration != -1)
		pq_editable.total_time += t->info->duration;
	window_changed(pq_editable.win);
}

struct track_info *play_queue_remove(void)
{
	struct list_head *item;
	struct simple_track *t;
	struct track_info *info;
	struct iter iter;

	item = pq_editable.head.next;
	if (item == &pq_editable.head)
		return NULL;

	t = to_simple_track(item);

	editable_track_to_iter(&pq_editable, t, &iter);
	window_row_vanishes(pq_editable.win, &iter);

	pq_editable.nr_marked -= t->marked;
	pq_editable.nr_tracks--;
	list_del(&t->node);

	info = t->info;
	free(t);
	return info;
}
