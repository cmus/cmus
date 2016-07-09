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
#include "cmus.h"
#include "ui_curses.h"

struct editable pl_editable;
struct simple_track *pl_cur_track = NULL;
struct rb_root pl_shuffle_root;

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
	cmus_add(pl_add_track, pl_autosave_filename, FILE_TYPE_PL, JOB_TYPE_PL, 0);
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

struct track_info *pl_goto_next(void)
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

struct track_info *pl_goto_prev(void)
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

struct track_info *pl_activate_selected(void)
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

void pl_exit(void)
{
	cmus_save(pl_for_each, pl_autosave_filename);
}

struct searchable *pl_get_searchable(void)
{
	return pl_editable.searchable;
}

unsigned int pl_playing_total_time(void)
{
	return pl_editable.total_time;
}

void pl_set_nr_rows(int h)
{
	window_set_nr_rows(pl_editable.win, h);
}

int pl_needs_redraw(void)
{
	return pl_editable.win->changed;
}

void pl_invert_marks(void)
{
	editable_invert_marks(&pl_editable);
}

void pl_mark(char *arg)
{
	editable_mark(&pl_editable, arg);
}

void pl_unmark(void)
{
	editable_unmark(&pl_editable);
}

void pl_rand(void)
{
	editable_rand(&pl_editable);
}

void pl_win_mv_after(void)
{
	editable_move_after(&pl_editable);
}

void pl_win_mv_before(void)
{
	editable_move_before(&pl_editable);
}

void pl_win_remove(void)
{
	editable_remove_sel(&pl_editable);
}

void pl_win_toggle(void)
{
	editable_toggle_mark(&pl_editable);
}

void pl_win_update(void)
{
	editable_clear(&pl_editable);
	cmus_add(pl_add_track, pl_filename, FILE_TYPE_PL, JOB_TYPE_PL, 0);
}

void pl_win_next(void)
{
}
