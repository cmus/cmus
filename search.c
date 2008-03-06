/* 
 * Copyright Timo Hirvonen
 */

#include "search.h"
#include "editable.h"
#include "xmalloc.h"

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
