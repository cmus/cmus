/*
 * Copyright 2008-2013 Various Authors
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
#include "options.h"
#include "xmalloc.h"
#include "debug.h"
#include "utils.h"

#include <stdlib.h>

static void sel_changed(struct window *win)
{
	if (win->sel_changed)
		win->sel_changed();
	win->changed = 1;
}

static int selectable(struct window *win, struct iter *iter)
{
	if (win->selectable)
		return win->selectable(iter);
	return 1;
}

struct window *window_new(int (*get_prev)(struct iter *), int (*get_next)(struct iter *))
{
	struct window *win;

	win = xnew(struct window, 1);
	win->get_next = get_next;
	win->get_prev = get_prev;
	win->selectable = NULL;
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
	while (!selectable(win, &win->sel))
		win->get_next(&win->sel);
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
	struct iter iter;
	int upper_bound   = min_i(scroll_offset,  win->nr_rows/2);
	int buffer        = 0; /* rows between `old sel` and `old top` */
	int sel_up        = 0; /* selectable rows between `old sel` and `new sel` */
	int skipped       = 0; /* unselectable rows between `old sel` and `new sel` */
	int actual_offset = 0; /* rows between `new sel` and `new top` */
	int top_up        = 0; /* rows between `old top` and `new top` */

	iter = win->top;
	while (!iters_equal(&iter, &win->sel)) {
		win->get_next(&iter);
		buffer++;
	}

	iter = win->sel;
	while (sel_up < rows) {
		if (!win->get_prev(&iter)) {
			break;
		}
		if (selectable(win, &iter)) {
			sel_up++;
			win->sel = iter;
		} else {
			skipped++;
		}
	}
	/* if there is no selectable row above the current, we move win->top instead
	 * this is necessary when scroll_offset=0 to make the first album header visible */
	if (sel_up == 0) {
		skipped = 0;
		upper_bound = min_i(buffer+rows, win->nr_rows/2);
	}

	iter = win->sel;
	while (actual_offset < upper_bound) {
		if (!win->get_prev(&iter)) {
			break;
		}
		actual_offset++;
	}

	top_up = actual_offset + sel_up + skipped - buffer;
	while (top_up > 0) {
		win->get_prev(&win->top);
		top_up--;
	}

	if (sel_up > 0 || actual_offset > 0)
		sel_changed(win);
}

void window_down(struct window *win, int rows)
{
	struct iter iter;
	int upper_bound   = min_i(scroll_offset, (win->nr_rows-1)/2);
	int buffer        = 0; /* rows between `old sel` and `old bottom` */
	int sel_down      = 0; /* selectable rows between `old sel` and `new sel` */
	int skipped       = 0; /* unselectable rows between `old sel` and `new sel` */
	int actual_offset = 0; /* rows between `new sel` and `new bottom` */
	int top_down      = 0; /* rows between `old top` and `new top` */

	buffer = win->nr_rows - 1;
	iter = win->top;
	while (!iters_equal(&iter, &win->sel)) {
		win->get_next(&iter);
		buffer--;
	}

	iter = win->sel;
	while (sel_down < rows) {
		if (!win->get_next(&iter)) {
			break;
		}
		if (selectable(win, &iter)) {
			sel_down++;
			win->sel = iter;
		} else {
			skipped++;
		}
	}
	if (sel_down == 0) {
		skipped = 0;
		upper_bound = min_i(buffer+rows, (win->nr_rows-1)/2);
	}

	iter = win->sel;
	while (actual_offset < upper_bound) {
		if (!win->get_next(&iter))
			break;
		actual_offset++;
	}

	top_down = actual_offset + sel_down + skipped - buffer;
	while (top_down > 0) {
		win->get_next(&win->top);
		top_down--;
	}

	if (sel_down > 0 || actual_offset > 0)
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
	if (!win->get_next(&new) && !win->get_prev(&new)) {
		window_set_empty(win);
	}

	BUG_ON(iter->data0 != win->head.data0);
	if (iters_equal(&win->top, iter)) {
		new = *iter;
		if (win->get_next(&new)) {
			win->top = new;
		} else {
			new = *iter;
			win->get_prev(&new);
			win->top = new;
		}
	}
	if (iters_equal(&win->sel, iter)) {
		/* calculate minimal distance to next selectable */
		int down = 0;
		int up = 0;
		new = *iter;
		do {
			if (!win->get_next(&new)) {
				down = 0;
				break;
			}
			down++;
		} while (!selectable(win, &new));
		new = *iter;
		do {
			if (!win->get_prev(&new)) {
				up = 0;
				break;
			}
			up++;
		} while (!selectable(win, &new));
		new = *iter;
		if (down > 0 && (up == 0 || down <= up)) {
			do {
				win->get_next(&new);
			} while (!selectable(win, &new));
		} else if (up > 0) {
			do {
				win->get_prev(&new);
			} while (!selectable(win, &new));
		} else {
			/* no selectable item left but window not empty */
			new.data1 = new.data2 = NULL;
		}
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
	int sel_nr, top_nr, bottom_nr;
	int upper_bound;
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

	upper_bound = win->nr_rows / 2;
	if (scroll_offset < upper_bound)
		upper_bound = scroll_offset;

	if (sel_nr < top_nr + upper_bound) { /* scroll up */
		tmp = win->head;
		win->get_next(&tmp);
		if (sel_nr < upper_bound) { /* no space above */
			win->top = tmp;
		} else {
			win->top = win->sel;
			while (upper_bound > 0) {
				win->get_prev(&win->top);
				upper_bound--;
			}
		}
	} else { /* scroll down */
		upper_bound = (win->nr_rows - 1) / 2;
		if (scroll_offset < upper_bound)
			upper_bound = scroll_offset;

		tmp = win->sel;
		bottom_nr = sel_nr;
		if (sel_nr >= top_nr + win->nr_rows) { /* selected element not visible */
			while (sel_nr >= top_nr + win->nr_rows) {
				win->get_next(&win->top);
				top_nr++;
			}
		} else { /* selected element visible */
			while (bottom_nr + 1 < top_nr + win->nr_rows) {
				if (!win->get_next(&tmp)) { /* no space below */
					bottom_nr = sel_nr + upper_bound;
					break;
				}
				bottom_nr++;
			}
		}

		while (bottom_nr < sel_nr + upper_bound) {
			if (!win->get_next(&tmp))
				break;
			bottom_nr++;
			win->get_next(&win->top);
		}
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
	while (!selectable(win, &win->sel))
		win->get_next(&win->sel);
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
	while (!selectable(win, &win->sel))
		win->get_prev(&win->sel);
	if (!iters_equal(&old_sel, &win->sel))
		sel_changed(win);
}

void window_page_up(struct window *win)
{
	struct iter sel = win->sel;
	struct iter top = win->top;
	int up;

	for (up = 0; up < win->nr_rows - 1; up++) {
		if (!win->get_prev(&sel) || !win->get_prev(&top))
			break;
		if (selectable(win, &sel)) {
			win->sel = sel;
			win->top = top;
		}
	}

	sel_changed(win);
}

void window_half_page_up(struct window *win)
{
	struct iter sel = win->sel;
	struct iter top = win->top;
	int up;

	for (up = 0; up < (win->nr_rows - 1) / 2; up++) {
		if (!win->get_prev(&sel) || !win->get_prev(&top))
			break;
		if (selectable(win, &sel)) {
			win->sel = sel;
			win->top = top;
		}
	}

	sel_changed(win);
}

static struct iter window_bottom(struct window *win)
{
	struct iter bottom = win->top;
	struct iter iter = win->top;
	int down;

	for (down = 0; down < win->nr_rows - 1; down++) {
		if (!win->get_next(&iter))
			break;
		bottom = iter;
	}

	return bottom;
}

void window_page_down(struct window *win)
{
	struct iter sel = win->sel;
	struct iter bot = window_bottom(win);
	struct iter top = win->top;
	int down;

	for (down = 0; down < win->nr_rows - 1; down++) {
		if (!win->get_next(&sel) || !win->get_next(&bot))
			break;
		win->get_next(&top);
		if (selectable(win, &sel)) {
			win->sel = sel;
			win->top = top;
		}
	}

	sel_changed(win);
}

void window_half_page_down(struct window *win)
{
	struct iter sel = win->sel;
	struct iter bot = window_bottom(win);
	struct iter top = win->top;
	int down;

	for (down = 0; down < (win-> nr_rows - 1) / 2; down++) {
		if (!win->get_next(&sel) || !win->get_next(&bot))
			break;
		win->get_next(&top);
		if (selectable(win, &sel)) {
			win->sel = sel;
			win->top = top;
		}
	}

	sel_changed(win);
}


void window_scroll_down(struct window *win)
{
	struct iter bot = window_bottom(win);
	struct iter top = win->top;
	if (!win->get_next(&bot)) return;
	if (!win->get_next(&top)) return;
	if (iters_equal(&win->top, &win->sel))
		win->get_next(&win->sel);
	win->top = top;
	while (!selectable(win, &win->sel))
		win->get_next(&win->sel);
	sel_changed(win);
}

void window_scroll_up(struct window *win)
{
	struct iter top = win->top;
	if (!win->get_prev(&top)) return;
	struct iter bot = window_bottom(win);
	/* keep selected row on screen: */
	if (iters_equal(&bot, &win->sel))
		win->get_prev(&win->sel);
	win->top = top;
	while (!selectable(win, &win->sel))
		win->get_prev(&win->sel);
	sel_changed(win);
}

static void window_goto_pos(struct window *win, int pos)
{
	struct iter old_sel;
	int i;

	old_sel = win->sel;
	win->sel = win->top;
	for (i = 0; i < pos; i++)
		win->get_next(&win->sel);
	if (!iters_equal(&old_sel, &win->sel))
		sel_changed(win);
}

void window_page_top(struct window *win)
{
	window_goto_pos(win, 0);
	while (!selectable(win, &win->sel))
		win->get_next(&win->sel);
}

void window_page_bottom(struct window *win)
{
	window_goto_pos(win, win->nr_rows - 1);
	while (!selectable(win, &win->sel))
		win->get_prev(&win->sel);
}

void window_page_middle(struct window *win)
{
	window_goto_pos(win, win->nr_rows / 2);
	while (!selectable(win, &win->sel))
		win->get_next(&win->sel);
}

int window_get_nr_rows(struct window *win)
{
	return win->nr_rows;
}
