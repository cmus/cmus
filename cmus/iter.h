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

#ifndef _ITER_H
#define _ITER_H

#include <stdlib.h>

struct iter {
	/* this usually points to the list head */
	void *data0;

	/* these point to the list item, for simple lists data2 is usually NULL */
	void *data1;
	void *data2;
};

static inline void iter_init(struct iter *iter)
{
	iter->data0 = NULL;
	iter->data1 = NULL;
	iter->data2 = NULL;
}

static inline void iter_head(struct iter *iter)
{
	iter->data1 = NULL;
	iter->data2 = NULL;
}

static inline int iters_equal(struct iter *a, struct iter *b)
{
	return a->data0 == b->data0 &&
		a->data1 == b->data1 &&
		a->data2 == b->data2;
}

static inline int iter_is_head(struct iter *iter)
{
	return iter->data0 != NULL &&
		iter->data1 == NULL &&
		iter->data2 == NULL;
}

static inline int iter_is_null(struct iter *iter)
{
	return iter->data0 == NULL &&
		iter->data1 == NULL &&
		iter->data2 == NULL;
}

static inline int iter_is_empty(struct iter *iter)
{
	return iter->data0 == NULL || (iter->data1 == NULL && iter->data2 == NULL);
}

#endif
