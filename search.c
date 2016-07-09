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

static int advance(struct searchable *s, struct iter *iter,
		enum search_direction dir, int *wrapped)
{
	if (dir == SEARCH_FORWARD) {
		if (!s->ops.get_next(iter)) {
			if (!wrap_search)
				return 0;
			*iter = s->head;
			if (!s->ops.get_next(iter))
				return 0;
			*wrapped = 1;
		}
	} else {
		if (!s->ops.get_prev(iter)) {
			if (!wrap_search)
				return 0;
			*iter = s->head;
			if (!s->ops.get_prev(iter))
				return 0;
			*wrapped = 1;
		}
	}
	return 1;
}

/* returns next matching item or NULL if not found
 * result can be the current item unless skip_current is set */
static int do_u_search(struct searchable *s, struct iter *iter, const char *text,
		enum search_direction dir, int skip_current)
{
	struct iter start;
	int wrapped = 0;

	if (skip_current && !advance(s, iter, dir, &wrapped))
		return 0;

	start = *iter;
	while (1) {
		if (s->ops.matches(s->data, iter, text)) {
			if (wrapped)
				info_msg(dir == SEARCH_FORWARD ?
					 "search hit BOTTOM, continuing at TOP" :
					 "search hit TOP, continuing at BOTTOM");
			return 1;
		}
		if (!advance(s, iter, dir, &wrapped) || iters_equal(iter, &start))
			return 0;
	}
}

static int do_search(struct searchable *s, struct iter *iter, const char *text,
		enum search_direction dir, int skip_current)
{
	char *u_text = NULL;
	int r;

	/* search text is always in locale encoding (because cmdline is) */
	if (!using_utf8 && utf8_encode(text, charset, &u_text) == 0)
		text = u_text;

	r = do_u_search(s, iter, text, dir, skip_current);

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

void searchable_set_head(struct searchable *s, const struct iter *head)
{
	s->head = *head;
}

int search(struct searchable *s, const char *text, enum search_direction dir, int beginning)
{
	struct iter iter;
	int ret;

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
		ret = do_search(s, &iter, text, dir, 0);
	return ret;
}

int search_next(struct searchable *s, const char *text, enum search_direction dir)
{
	struct iter iter;
	int ret;

	if (!s->ops.get_current(s->data, &iter)) {
		return 0;
	}
	ret = do_search(s, &iter, text, dir, 1);
	return ret;
}
