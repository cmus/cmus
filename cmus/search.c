/* 
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <search.h>
#include <xmalloc.h>

struct searchable {
	void *data;
	struct iter head;
	struct searchable_ops ops;
};

/* returns next matching track (can be current!) or NULL if not found */
static int do_search(struct searchable *s, struct iter *iter, const char *text, int direction)
{
	while (1) {
		if (s->ops.matches(s->data, iter, text))
			return 1;
		if (direction == SEARCH_FORWARD) {
			if (!s->ops.get_next(iter))
				return 0;
		} else {
			if (!s->ops.get_prev(iter))
				return 0;
		}
	}
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

	s->ops.lock(s->data);
	if (beginning) {
		/* first or last item */
		iter = s->head;
		if (dir == SEARCH_FORWARD){
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
	s->ops.unlock(s->data);
	return ret;
}

int search_next(struct searchable *s, const char *text, enum search_direction dir)
{
	struct iter iter;
	int ret;

	s->ops.lock(s->data);
	if (!s->ops.get_current(s->data, &iter)) {
		s->ops.unlock(s->data);
		return 0;
	}
	if (dir == SEARCH_FORWARD) {
		ret = s->ops.get_next(&iter);
	} else {
		ret = s->ops.get_prev(&iter);
	}
	if (ret)
		ret = do_search(s, &iter, text, dir);
	s->ops.unlock(s->data);
	return ret;
}
