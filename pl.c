/*
 * Copyright 2006 Timo Hirvonen
 */

#include "pl.h"
#include "editable.h"
#include "options.h"
#include "xmalloc.h"

struct editable pl_editable;
struct simple_track *pl_cur_track = NULL;

static LIST_HEAD(pl_shuffle_head);

static void pl_free_track(struct list_head *item)
{
	struct simple_track *track = to_simple_track(item);

	if (track == pl_cur_track)
		pl_cur_track = NULL;

	list_del(&((struct shuffle_track *)track)->node);
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

struct track_info *pl_set_next(void)
{
	struct simple_track *track;
	struct track_info *ti = NULL;

	if (list_empty(&pl_editable.head))
		return NULL;

	if (shuffle) {
		track = (struct simple_track *)shuffle_list_get_next(&pl_shuffle_head,
				(struct shuffle_track *)pl_cur_track, dummy_filter);
	} else {
		track = simple_list_get_next(&pl_editable.head, pl_cur_track, dummy_filter);
	}
	if (track) {
		pl_cur_track = track;
		ti = track->info;

		track_info_ref(ti);
		pl_editable.win->changed = 1;
	}
	return ti;
}

struct track_info *pl_set_prev(void)
{
	struct simple_track *track;
	struct track_info *ti = NULL;

	if (list_empty(&pl_editable.head))
		return NULL;

	if (shuffle) {
		track = (struct simple_track *)shuffle_list_get_prev(&pl_shuffle_head,
				(struct shuffle_track *)pl_cur_track, dummy_filter);
	} else {
		track = simple_list_get_prev(&pl_editable.head, pl_cur_track, dummy_filter);
	}
	if (track) {
		pl_cur_track = track;
		ti = track->info;

		track_info_ref(ti);
		pl_editable.win->changed = 1;
	}
	return ti;
}

struct track_info *pl_set_selected(void)
{
	struct track_info *ti;
	struct iter sel;

	if (list_empty(&pl_editable.head))
		return NULL;

	window_get_sel(pl_editable.win, &sel);
	pl_cur_track = iter_to_simple_track(&sel);
	ti = pl_cur_track->info;
	track_info_ref(ti);
	pl_editable.win->changed = 1;
	return ti;
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
	shuffle_list_add_track(&pl_shuffle_head, &track->node, pl_editable.nr_tracks);
	editable_add(&pl_editable, (struct simple_track *)track);
}

void pl_reshuffle(void)
{
	reshuffle(&pl_shuffle_head);
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
