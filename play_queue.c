/*
 * Copyright 2008-2011 Various Authors
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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

	list_add_tail(&t->node, &pq_editable.head);
	pq_editable.nr_tracks++;
	if (t->info->duration != -1)
		pq_editable.total_time += t->info->duration;
	window_changed(pq_editable.win);
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

static struct track_info *pq_remove_track(struct list_head *item)
{
	struct simple_track *t;
	struct track_info *info;
	struct iter iter;

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

struct track_info *play_queue_remove(void)
{
	return pq_remove_track(pq_editable.head.next);
}

int play_queue_for_each(int (*cb)(void *data, struct track_info *ti), void *data)
{
	struct simple_track *track;
	int rc = 0;

	list_for_each_entry(track, &pq_editable.head, node) {
		rc = cb(data, track->info);
		if (rc)
			break;
	}
	return rc;
}

void play_queue_update_track(struct track_info *old, struct track_info *new)
{
	struct list_head *item, *tmp;
	int changed = 0;

	list_for_each_safe(item, tmp, &pq_editable.head) {
		struct simple_track *track = to_simple_track(item);
		if (track->info == old) {
			if (new) {
				track_info_unref(old);
				track_info_ref(new);
				track->info = new;
			} else {
				pq_remove_track(item);
			}
			changed = 1;
		}
	}
	if (changed)
		window_changed(pq_editable.win);
}
