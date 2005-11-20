/* 
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _WINDOW_H
#define _WINDOW_H

#include <iter.h>

/*
 * window contains list of rows
 * - the list is double linked circular list
 * - head contains no data. head is not a real row
 * - head->prev gets the last row (or head if the list is empty) in the list
 * - head->next gets the first row (or head if the list is empty) in the list
 *
 * get_prev(&iter) always stores prev row to the iter
 * get_next(&iter) always stores next row to the iter
 *
 * these return 1 if the new row is real row (not head), 0 otherwise
 *
 * sel_changed callback is called if not NULL and selection has changed
 */

struct window {
	/* head of the row list */
	struct iter head;

	/* top row */
	struct iter top;

	/* selected row */
	struct iter sel;

	/* window height */
	int nr_rows;

	unsigned changed : 1;

	/* return 1 if got next/prev, otherwise 0 */
	int (*get_prev)(struct iter *iter);
	int (*get_next)(struct iter *iter);
	void (*sel_changed)(void);
};

extern struct window *window_new(int (*get_prev)(struct iter *), int (*get_next)(struct iter *));
extern void window_free(struct window *win);
extern void window_set_empty(struct window *win);
extern void window_set_contents(struct window *win, void *head);

/* call this after rows were added to window or order of rows was changed.
 * top and sel MUST point to valid rows (or window must be empty, but then
 * why do you want to call this function :)).
 *
 * if you remove row from window then call window_row_vanishes BEFORE removing
 * the row instead of this function.
 */
extern void window_changed(struct window *win);

/* call this BEFORE row is removed from window */
extern void window_row_vanishes(struct window *win, struct iter *iter);

extern int window_get_top(struct window *win, struct iter *iter);
extern int window_get_sel(struct window *win, struct iter *iter);
extern int window_get_prev(struct window *win, struct iter *iter);
extern int window_get_next(struct window *win, struct iter *iter);

/* set selected row */
extern void window_set_sel(struct window *win, struct iter *iter);

extern void window_set_nr_rows(struct window *win, int nr_rows);
extern void window_up(struct window *win, int rows);
extern void window_down(struct window *win, int rows);
extern void window_goto_top(struct window *win);
extern void window_goto_bottom(struct window *win);
extern void window_page_up(struct window *win);
extern void window_page_down(struct window *win);

extern int window_get_nr_rows(struct window *win);

#endif
