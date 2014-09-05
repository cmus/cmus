/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2008 Timo Hirvonen
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

#ifndef CMUS_KEYVAL_H
#define CMUS_KEYVAL_H

struct keyval {
	char *key;
	char *val;
};

struct growing_keyvals {
	struct keyval *keyvals;
	int alloc;
	int count;
};

#define GROWING_KEYVALS(name) struct growing_keyvals name = { NULL, 0, 0 }

struct keyval *keyvals_new(int num);
void keyvals_init(struct growing_keyvals *c, const struct keyval *keyvals);
void keyvals_add(struct growing_keyvals *c, const char *key, char *val);
const char *keyvals_get_val_growing(const struct growing_keyvals *c, const char *key);
void keyvals_terminate(struct growing_keyvals *c);
void keyvals_free(struct keyval *keyvals);
struct keyval *keyvals_dup(const struct keyval *keyvals);
const char *keyvals_get_val(const struct keyval *keyvals, const char *key);

#endif
