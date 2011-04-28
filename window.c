/*
 * Copyright 2008-2011 Various Authors
 * Copyright 2004-2005 Timo Hirvonen
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

#include "window.h"
#include "xmalloc.h"
#include "debug.h"

#include <stdlib.h>

static void sel_changed(struct window *win)
{
	if (win->sel_changed)
		win->sel_changed();
	win->changed = 1;
}

struct window *window_new(int (*get_prev)(struct iter *), int (*get_next)(struct iter *))
{
	struct window *win;

	win = xnew(struct window, 1);
	win->get_next = get_next;
	win->get_prev = get_prev;
	win->sel_changed = NULL;
	win->nr_rows = 1;
	win->changed = 1;
	iter_init(&win->head);
	iter_init(&win->top);
	iter_init(&win->sel);
	return win;
}

void window_free(struct window *win)
{
	free(win);
}

void window_set_empty(struct window *win)
{
	iter_init(&win->head);
	iter_init(&win->top);
	iter_init(&win->sel);
	sel_changed(win);
}

void window_set_contents(struct window *win, void *head)
{
	struct iter first;

	win->head.data0 = head;
	win->head.data1 = NULL;
	win->head.data2 = NULL;
	first = win->head;
	win->get_next(&first);
	win->top = first;
	win->sel = first;
	sel_changed(win);
}

void window_set_nr_rows(struct window *win, int nr_rows)
{
	if (nr_rows < 1)
		return;
	win->nr_rows = nr_rows;
	window_changed(win);
	win->changed = 1;
}

void window_up(struct window *win, int rows)
{
	int i;

	for (i = 0; i < rows; i++) {
		struct iter prev = win->sel;

		if (!win->get_prev(&prev))
			break;
		if (iters_equal(&win->sel, &win->top))
			win->top = prev;
		win->sel = prev;
	}
	if (i)
		sel_changed(win);
}

void window_down(struct window *win, int rows)
{
	struct iter iter;
	int delta, sel_down, top_down;

	/* distance between top and sel */
	delta = 0;
	iter = win->top;
	while (!iters_equal(&iter, &win->sel)) {
		win->get_next(&iter);
		delta++;
	}

	for (sel_down = 0; sel_down < rows; sel_down++) {
		iter = win->sel;
		if (!win->get_next(&iter))
			break;
		win->sel = iter;
	}

	top_down = sel_down - (win->nr_rows - delta - 1);
	while (top_down > 0) {
		win->get_next(&win->top);
		top_down--;
	}
	if (sel_down)
		sel_changed(win);
}

/*
 * minimize number of empty lines visible
 * make sure selection is visible
 */
void window_changed(struct window *win)
{
	struct iter iter;
	int delta, rows;

	if (iter_is_null(&win->head)) {
		BUG_ON(!iter_is_null(&win->top));
		BUG_ON(!iter_is_null(&win->sel));
		return;
	}
	BUG_ON(iter_is_null(&win->top));
	BUG_ON(iter_is_null(&win->sel));

	/* make sure top and sel point to real row if possible */
	if (iter_is_head(&win->top)) {
		win->get_next(&win->top);
		win->sel = win->top;
		sel_changed(win);
		return;
	}

	/* make sure the selected row is visible */

	/* get distance between top and sel */
	delta = 0;
	iter = win->top;
	while (!iters_equal(&iter, &win->sel)) {
		if (!win->get_next(&iter)) {
			/* sel < top, scroll up until top == sel */
			while (!iters_equal(&win->top, &win->sel))
				win->get_prev(&win->top);
			goto minimize;
		}
		delta++;
	}

	/* scroll down until sel is visible */
	while (delta > win->nr_rows - 1) {
		win->get_next(&win->top);
		delta--;
	}
minimize:
	/* minimize number of empty lines shown */
	iter = win->top;
	rows = 1;
	while (rows < win->nr_rows) {
		if (!win->get_next(&iter))
			break;
		rows++;
	}
	while (rows < win->nr_rows) {
		iter = win->top;
		if (!win->get_prev(&iter))
			break;
		win->top = iter;
		rows++;
	}
	win->changed = 1;
}

void window_row_vanishes(struct window *win, struct iter *iter)
{
	struct iter new = *iter;

	BUG_ON(iter->data0 != win->head.data0);
	if (!win->get_next(&new)) {
		new = *iter;
		win->get_prev(&new);
	}
	if (iters_equal(&win->top, iter))
		win->top = new;
	if (iters_equal(&win->sel, iter)) {
		win->sel = new;
		sel_changed(win);
	}
	win->changed = 1;
}

int window_get_top(struct window *win, struct iter *iter)
{
	*iter = win->top;
	return !iter_is_empty(iter);
}

int window_get_sel(struct window *win, struct iter *iter)
{
	*iter = win->sel;
	return !iter_is_empty(iter);
}

int window_get_prev(struct window *win, struct iter *iter)
{
	return win->get_prev(iter);
}

int window_get_next(struct window *win, struct iter *iter)
{
	return win->get_next(iter);
}

void window_set_sel(struct window *win, struct iter *iter)
{
	int sel_nr, top_nr;
	struct iter tmp;

	BUG_ON(iter_is_empty(&win->top));
	BUG_ON(iter_is_empty(iter));
	BUG_ON(iter->data0 != win->head.data0);

	if (iters_equal(&win->sel, iter))
		return;
	win->sel = *iter;

	tmp = win->head;
	win->get_next(&tmp);
	top_nr = 0;
	while (!iters_equal(&tmp, &win->top)) {
		win->get_next(&tmp);
		top_nr++;
	}

	tmp = win->head;
	win->get_next(&tmp);
	sel_nr = 0;
	while (!iters_equal(&tmp, &win->sel)) {
		BUG_ON(!win->get_next(&tmp));
		sel_nr++;
	}

	if (sel_nr < top_nr)
		win->top = win->sel;
	while (sel_nr - top_nr >= win->nr_rows) {
		win->get_next(&win->top);
		top_nr++;
	}
	sel_changed(win);
}

void window_goto_top(struct window *win)
{
	struct iter old_sel;

	old_sel = win->sel;
	win->sel = win->head;
	win->get_next(&win->sel);
	win->top = win->sel;
	if (!iters_equal(&old_sel, &win->sel))
		sel_changed(win);
}

void window_goto_bottom(struct window *win)
{
	struct iter old_sel;
	int count;

	old_sel = win->sel;
	win->sel = win->head;
	win->get_prev(&win->sel);
	win->top = win->sel;
	count = win->nr_rows - 1;
	while (count) {
		struct iter iter = win->top;

		if (!win->get_prev(&iter))
			break;
		win->top = iter;
		count--;
	}
	if (!iters_equal(&old_sel, &win->sel))
		sel_changed(win);
}

void window_page_up(struct window *win)
{
	window_up(win, win->nr_rows - 1);
}

void window_page_down(struct window *win)
{
	window_down(win, win->nr_rows - 1);
}

int window_get_nr_rows(struct window *win)
{
	return win->nr_rows;
}
