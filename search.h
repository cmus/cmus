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

#ifndef _SEARCH_H
#define _SEARCH_H

#include "iter.h"

enum search_direction { SEARCH_FORWARD, SEARCH_BACKWARD };

struct searchable_ops {
	int (*get_prev)(struct iter *iter);
	int (*get_next)(struct iter *iter);
	int (*get_current)(void *data, struct iter *iter);
	int (*matches)(void *data, struct iter *iter, const char *text);
};

struct searchable;

struct searchable *searchable_new(void *data, const struct iter *head, const struct searchable_ops *ops);
void searchable_free(struct searchable *s);

int search(struct searchable *s, const char *text, enum search_direction dir, int beginning);
int search_next(struct searchable *s, const char *text, enum search_direction dir);

#endif
