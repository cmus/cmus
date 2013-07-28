/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2005 Timo Hirvonen
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

#include "search.h"
#include "editable.h"
#include "xmalloc.h"
#include "ui_curses.h"
#include "convert.h"
#include "options.h"

struct searchable {
	void *data;
	struct iter head;
	struct searchable_ops ops;
};

static void search_lock(void)
{
	editable_lock();
}

static void search_unlock(void)
{
	editable_unlock();
}

/* returns next matching track (can be current!) or NULL if not found */
static int do_u_search(struct searchable *s, struct iter *iter, const char *text, int direction)
{
	struct iter start = *iter;
	const char *msg = NULL;

	while (1) {
		if (s->ops.matches(s->data, iter, text)) {
			if (msg)
				info_msg("%s\n", msg);
			return 1;
		}
		if (direction == SEARCH_FORWARD) {
			if (!s->ops.get_next(iter)) {
				if (!wrap_search)
					return 0;
				*iter = s->head;
				if (!s->ops.get_next(iter))
					return 0;
				msg = "search hit BOTTOM, continuing at TOP";
			}
		} else {
			if (!s->ops.get_prev(iter)) {
				if (!wrap_search)
					return 0;
				*iter = s->head;
				if (!s->ops.get_prev(iter))
					return 0;
				msg = "search hit TOP, continuing at BOTTOM";
			}
		}
		if (iters_equal(iter, &start)) {
			return 0;
		}
	}
}

static int do_search(struct searchable *s, struct iter *iter, const char *text, int direction)
{
	char *u_text = NULL;
	int r;

	/* search text is always in locale encoding (because cmdline is) */
	if (!using_utf8 && utf8_encode(text, charset, &u_text) == 0)
		text = u_text;

	r = do_u_search(s, iter, text, direction);

	free(u_text);
	return r;
}

struct searchable *searchable_new(void *data, const struct iter *head, const struct searchable_ops *ops)
{
	struct searchable *s;

	s = xnew(struct searchable, 1);
	s->data = data;
	s->head = *head;
	s->ops = *ops;
	return s;
}

void searchable_free(struct searchable *s)
{
	free(s);
}

int search(struct searchable *s, const char *text, enum search_direction dir, int beginning)
{
	struct iter iter;
	int ret;

	search_lock();
	if (beginning) {
		/* first or last item */
		iter = s->head;
		if (dir == SEARCH_FORWARD) {
			ret = s->ops.get_next(&iter);
		} else {
			ret = s->ops.get_prev(&iter);
		}
	} else {
		/* selected item */
		ret = s->ops.get_current(s->data, &iter);
	}
	if (ret)
		ret = do_search(s, &iter, text, dir);
	search_unlock();
	return ret;
}

int search_next(struct searchable *s, const char *text, enum search_direction dir)
{
	struct iter iter;
	int ret;

	search_lock();
	if (!s->ops.get_current(s->data, &iter)) {
		search_unlock();
		return 0;
	}
	if (dir == SEARCH_FORWARD) {
		ret = s->ops.get_next(&iter);
	} else {
		ret = s->ops.get_prev(&iter);
	}
	if (ret)
		ret = do_search(s, &iter, text, dir);
	search_unlock();
	return ret;
}
