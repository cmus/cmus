/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2006 Timo Hirvonen
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

#include "pl.h"
#include "editable.h"
#include "options.h"
#include "xmalloc.h"

struct editable pl_editable;
struct simple_track *pl_cur_track = NULL;

static struct rb_root pl_shuffle_root;

static void pl_free_track(struct list_head *item)
{
	struct simple_track *track = to_simple_track(item);
	struct shuffle_track *shuffle_track = (struct shuffle_track *) track;

	if (track == pl_cur_track)
		pl_cur_track = NULL;

	rb_erase(&shuffle_track->tree_node, &pl_shuffle_root);
	track_info_unref(track->info);
	free(track);
}

void pl_init(void)
{
	editable_init(&pl_editable, pl_free_track);
}

static int dummy_filter(const struct simple_track *track)
{
	return 1;
}

static struct track_info *set_track(struct simple_track *track)
{
	struct track_info *ti = NULL;

	if (track) {
		pl_cur_track = track;
		ti = track->info;
		track_info_ref(ti);
		if (follow)
			pl_sel_current();
		pl_editable.win->changed = 1;
	}
	return ti;
}

struct track_info *pl_set_next(void)
{
	struct simple_track *track;

	if (list_empty(&pl_editable.head))
		return NULL;

	if (shuffle) {
		track = (struct simple_track *)shuffle_list_get_next(&pl_shuffle_root,
				(struct shuffle_track *)pl_cur_track, dummy_filter);
	} else {
		track = simple_list_get_next(&pl_editable.head, pl_cur_track, dummy_filter);
	}
	return set_track(track);
}

struct track_info *pl_set_prev(void)
{
	struct simple_track *track;

	if (list_empty(&pl_editable.head))
		return NULL;

	if (shuffle) {
		track = (struct simple_track *)shuffle_list_get_prev(&pl_shuffle_root,
				(struct shuffle_track *)pl_cur_track, dummy_filter);
	} else {
		track = simple_list_get_prev(&pl_editable.head, pl_cur_track, dummy_filter);
	}
	return set_track(track);
}

struct track_info *pl_set_selected(void)
{
	struct iter sel;

	if (list_empty(&pl_editable.head))
		return NULL;

	window_get_sel(pl_editable.win, &sel);
	return set_track(iter_to_simple_track(&sel));
}

void pl_sel_current(void)
{
	if (pl_cur_track) {
		struct iter iter;

		editable_track_to_iter(&pl_editable, pl_cur_track, &iter);
		window_set_sel(pl_editable.win, &iter);
	}
}

void pl_add_track(struct track_info *ti)
{
	struct shuffle_track *track = xnew(struct shuffle_track, 1);

	track_info_ref(ti);
	simple_track_init((struct simple_track *)track, ti);
	shuffle_list_add(track, &pl_shuffle_root);
	editable_add(&pl_editable, (struct simple_track *)track);
}

void pl_reshuffle(void)
{
	shuffle_list_reshuffle(&pl_shuffle_root);
}

int pl_for_each(int (*cb)(void *data, struct track_info *ti), void *data)
{
	struct simple_track *track;
	int rc = 0;

	list_for_each_entry(track, &pl_editable.head, node) {
		rc = cb(data, track->info);
		if (rc)
			break;
	}
	return rc;
}
