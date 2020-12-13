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

#include "editable.h"
#include "search.h"
#include "track.h"
#include "track_info.h"
#include "expr.h"
#include "filters.h"
#include "locking.h"
#include "mergesort.h"
#include "xmalloc.h"

static const struct searchable_ops simple_search_ops = {
	.get_prev = simple_track_get_prev,
	.get_next = simple_track_get_next,
	.get_current = simple_track_search_get_current,
	.matches = simple_track_search_matches
};

static struct simple_track *get_selected(struct editable *e)
{
	struct iter sel;

	if (window_get_sel(e->shared->win, &sel))
		return iter_to_simple_track(&sel);
	return NULL;
}

void editable_shared_init(struct editable_shared *shared,
		editable_free_track free_track)
{
	shared->win = window_new(simple_track_get_prev, simple_track_get_next);
	shared->sort_keys = xnew(sort_key_t, 1);
	shared->sort_keys[0] = SORT_INVALID;
	shared->sort_str[0] = 0;
	shared->free_track = free_track;
	shared->owner = NULL;

	struct iter iter = { 0 };
	shared->searchable = searchable_new(shared->win, &iter,
			&simple_search_ops);
}

void editable_init(struct editable *e, struct editable_shared *shared,
		int take_ownership)
{
	list_init(&e->head);
	e->tree_root = RB_ROOT;
	e->nr_tracks = 0;
	e->nr_marked = 0;
	e->total_time = 0;
	e->shared = shared;


	if (take_ownership)
		editable_take_ownership(e);
}

static int editable_owns_shared(struct editable *e)
{
	return e->shared->owner == e;
}

void editable_take_ownership(struct editable *e)
{
	if (!editable_owns_shared(e)) {
		e->shared->owner = e;
		window_set_contents(e->shared->win, &e->head);
		e->shared->win->changed = 1;

		struct iter iter = { .data0 = &e->head };
		searchable_set_head(e->shared->searchable, &iter);
	}
}

static void do_editable_add(struct editable *e, struct simple_track *track, int tiebreak)
{
	sorted_list_add_track(&e->head, &e->tree_root, track,
			e->shared->sort_keys, tiebreak);
	e->nr_tracks++;
	if (track->info->duration != -1)
		e->total_time += track->info->duration;
	if (editable_owns_shared(e))
		window_changed(e->shared->win);
}

void editable_add(struct editable *e, struct simple_track *track)
{
	do_editable_add(e, track, +1);
}

void editable_add_before(struct editable *e, struct simple_track *track)
{
	do_editable_add(e, track, -1);
}

void editable_remove_track(struct editable *e, struct simple_track *track)
{
	struct track_info *ti = track->info;
	struct iter iter;

	editable_track_to_iter(e, track, &iter);
	if (editable_owns_shared(e))
		window_row_vanishes(e->shared->win, &iter);

	e->nr_tracks--;
	e->nr_marked -= track->marked;
	if (ti->duration != -1)
		e->total_time -= ti->duration;

	sorted_list_remove_track(&e->head, &e->tree_root, track);
	e->shared->free_track(e, &track->node);
}

void editable_remove_sel(struct editable *e)
{
	struct simple_track *t;

	if (e->nr_marked) {
		/* treat marked tracks as selected */
		struct list_head *next, *item = e->head.next;

		while (item != &e->head) {
			next = item->next;
			t = to_simple_track(item);
			if (t->marked)
				editable_remove_track(e, t);
			item = next;
		}
	} else {
		t = get_selected(e);
		if (t)
			editable_remove_track(e, t);
	}
}

void editable_sort(struct editable *e)
{
	if (e->nr_tracks <= 1)
		return;
	sorted_list_rebuild(&e->head, &e->tree_root, e->shared->sort_keys);

	if (editable_owns_shared(e)) {
		window_changed(e->shared->win);
		window_goto_top(e->shared->win);
	}
}

void editable_shared_set_sort_keys(struct editable_shared *shared,
		sort_key_t *keys)
{
	free(shared->sort_keys);
	shared->sort_keys = keys;
}

void editable_toggle_mark(struct editable *e)
{
	struct simple_track *t;

	t = get_selected(e);
	if (t) {
		e->nr_marked -= t->marked;
		t->marked ^= 1;
		e->nr_marked += t->marked;
		if (editable_owns_shared(e)) {
			e->shared->win->changed = 1;
			window_down(e->shared->win, 1);
		}
	}
}

static void move_item(struct editable *e, struct list_head *head, struct list_head *item)
{
	struct simple_track *t = to_simple_track(item);
	struct iter iter;

	editable_track_to_iter(e, t, &iter);
	if (editable_owns_shared(e))
		window_row_vanishes(e->shared->win, &iter);

	list_del(item);
	list_add(item, head);
}

static void reset_tree(struct editable *e)
{
	struct simple_track *old, *first_track;

	old = tree_node_to_simple_track(rb_first(&e->tree_root));
	first_track = to_simple_track(e->head.next);
	if (old != first_track) {
		rb_replace_node(&old->tree_node, &first_track->tree_node, &e->tree_root);
		RB_CLEAR_NODE(&old->tree_node);
	}
}

static void move_sel(struct editable *e, struct list_head *after)
{
	struct simple_track *t;
	struct list_head *item, *next;
	struct iter iter;
	LIST_HEAD(tmp_head);

	if (e->nr_marked) {
		/* collect marked */
		item = e->head.next;
		while (item != &e->head) {
			t = to_simple_track(item);
			next = item->next;
			if (t->marked)
				move_item(e, &tmp_head, item);
			item = next;
		}
	} else {
		/* collect the selected track */
		t = get_selected(e);
		if (t)
			move_item(e, &tmp_head, &t->node);
	}

	/* put them back to the list after @after */
	item = tmp_head.next;
	while (item != &tmp_head) {
		next = item->next;
		list_add(item, after);
		item = next;
	}
	reset_tree(e);

	/* select top-most of the moved tracks */
	editable_track_to_iter(e, to_simple_track(after->next), &iter);

	if (editable_owns_shared(e)) {
		window_changed(e->shared->win);
		window_set_sel(e->shared->win, &iter);
	}
}

static struct list_head *find_insert_after_point(struct editable *e, struct list_head *item)
{
	if (e->nr_marked == 0) {
		/* move the selected track down one row */
		return item->next;
	}

	/* move marked after the selected
	 *
	 * if the selected track itself is marked we find the first unmarked
	 * track (or head) before the selected one
	 */
	while (item != &e->head) {
		struct simple_track *t = to_simple_track(item);

		if (!t->marked)
			break;
		item = item->prev;
	}
	return item;
}

static struct list_head *find_insert_before_point(struct editable *e, struct list_head *item)
{
	item = item->prev;
	if (e->nr_marked == 0) {
		/* move the selected track up one row */
		return item->prev;
	}

	/* move marked before the selected
	 *
	 * if the selected track itself is marked we find the first unmarked
	 * track (or head) before the selected one
	 */
	while (item != &e->head) {
		struct simple_track *t = to_simple_track(item);

		if (!t->marked)
			break;
		item = item->prev;
	}
	return item;
}

void editable_move_after(struct editable *e)
{
	struct simple_track *sel;

	if (e->nr_tracks <= 1 || e->shared->sort_keys[0] != SORT_INVALID)
		return;

	sel = get_selected(e);
	if (sel)
		move_sel(e, find_insert_after_point(e, &sel->node));
}

void editable_move_before(struct editable *e)
{
	struct simple_track *sel;

	if (e->nr_tracks <= 1 || e->shared->sort_keys[0] != SORT_INVALID)
		return;

	sel = get_selected(e);
	if (sel)
		move_sel(e, find_insert_before_point(e, &sel->node));
}

void editable_clear(struct editable *e)
{
	struct list_head *item, *tmp;

	list_for_each_safe(item, tmp, &e->head)
		editable_remove_track(e, to_simple_track(item));
}

void editable_remove_matching_tracks(struct editable *e,
		int (*cb)(void *data, struct track_info *ti), void *data)
{
	struct list_head *item, *tmp;

	list_for_each_safe(item, tmp, &e->head) {
		struct simple_track *t = to_simple_track(item);
		if (cb(data, t->info))
			editable_remove_track(e, t);
	}
}

void editable_mark(struct editable *e, const char *filter)
{
	struct expr *expr = NULL;
	struct simple_track *t;

	if (filter) {
		expr = parse_filter(filter);
		if (expr == NULL)
			return;
	}

	list_for_each_entry(t, &e->head, node) {
		e->nr_marked -= t->marked;
		t->marked = 0;
		if (expr == NULL || expr_eval(expr, t->info)) {
			t->marked = 1;
			e->nr_marked++;
		}
	}

	if (editable_owns_shared(e))
		e->shared->win->changed = 1;
}

void editable_unmark(struct editable *e)
{
	struct simple_track *t;

	list_for_each_entry(t, &e->head, node) {
		e->nr_marked -= t->marked;
		t->marked = 0;
	}

	if (editable_owns_shared(e))
		e->shared->win->changed = 1;
}

void editable_invert_marks(struct editable *e)
{
	struct simple_track *t;

	list_for_each_entry(t, &e->head, node) {
		e->nr_marked -= t->marked;
		t->marked ^= 1;
		e->nr_marked += t->marked;
	}

	if (editable_owns_shared(e))
		e->shared->win->changed = 1;
}

int _editable_for_each_sel(struct editable *e, track_info_cb cb, void *data,
		int reverse)
{
	int rc = 0;

	if (e->nr_marked) {
		/* treat marked tracks as selected */
		rc = simple_list_for_each_marked(&e->head, cb, data, reverse);
	} else {
		struct simple_track *t = get_selected(e);

		if (t)
			rc = cb(data, t->info);
	}
	return rc;
}

int editable_for_each_sel(struct editable *e, track_info_cb cb, void *data,
		int reverse, int advance)
{
	int rc;

	rc = _editable_for_each_sel(e, cb, data, reverse);
	if (advance && e->nr_marked == 0 && editable_owns_shared(e))
		window_down(e->shared->win, 1);
	return rc;
}

int editable_for_each(struct editable *e, track_info_cb cb, void *data,
		int reverse)
{
	return simple_list_for_each(&e->head, cb, data, reverse);
}

void editable_update_track(struct editable *e, struct track_info *old, struct track_info *new)
{
	struct list_head *item, *tmp;
	int changed = 0;

	list_for_each_safe(item, tmp, &e->head) {
		struct simple_track *track = to_simple_track(item);
		if (track->info == old) {
			if (new) {
				track_info_unref(old);
				track_info_ref(new);
				track->info = new;
			} else {
				editable_remove_track(e, track);
			}
			changed = 1;
		}
	}
	if (editable_owns_shared(e))
		e->shared->win->changed |= changed;
}

void editable_rand(struct editable *e)
{
	if (e->nr_tracks <=1)
		return;
	rand_list_rebuild(&e->head, &e->tree_root);

	if (editable_owns_shared(e)) {
		window_changed(e->shared->win);
		window_goto_top(e->shared->win);
	}
}

int editable_empty(struct editable *e)
{
	return list_empty(&e->head);
}
