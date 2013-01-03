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

#include "debug.h"
#include "keyval.h"
#include "xmalloc.h"

#include <strings.h>

struct keyval *keyvals_new(int num)
{
	struct keyval *c = xnew(struct keyval, num + 1);
	int i;

	for (i = 0; i <= num; i++) {
		c[i].key = NULL;
		c[i].val = NULL;
	}
	return c;
}

struct keyval *keyvals_dup(const struct keyval *keyvals)
{
	struct keyval *c;
	int i;

	for (i = 0; keyvals[i].key; i++)
		; /* nothing */
	c = xnew(struct keyval, i + 1);
	for (i = 0; keyvals[i].key; i++) {
		c[i].key = xstrdup(keyvals[i].key);
		c[i].val = xstrdup(keyvals[i].val);
	}
	c[i].key = NULL;
	c[i].val = NULL;
	return c;
}

void keyvals_free(struct keyval *keyvals)
{
	int i;

	for (i = 0; keyvals[i].key; i++) {
		free(keyvals[i].key);
		free(keyvals[i].val);
	}
	free(keyvals);
}

const char *keyvals_get_val(const struct keyval *keyvals, const char *key)
{
	int i;

	for (i = 0; keyvals[i].key; i++) {
		if (strcasecmp(keyvals[i].key, key) == 0)
			return keyvals[i].val;
	}
	return NULL;
}

void keyvals_init(struct growing_keyvals *c, const struct keyval *keyvals)
{
	int i;

	BUG_ON(c->keyvals);

	for (i = 0; keyvals[i].key; i++)
		; /* nothing */

	c->keyvals = keyvals_dup(keyvals);
	c->alloc = i;
	c->count = i;
}

void keyvals_add(struct growing_keyvals *c, const char *key, char *val)
{
	int n = c->count + 1;

	if (n > c->alloc) {
		n = (n + 3) & ~3;
		c->keyvals = xrenew(struct keyval, c->keyvals, n);
		c->alloc = n;
	}

	c->keyvals[c->count].key = xstrdup(key);
	c->keyvals[c->count].val = val;
	c->count++;
}

const char *keyvals_get_val_growing(const struct growing_keyvals *c, const char *key)
{
	int i;

	for (i = 0; i < c->count; ++i)
		if (strcasecmp(c->keyvals[i].key, key) == 0)
			return c->keyvals[i].val;

	return NULL;
}

void keyvals_terminate(struct growing_keyvals *c)
{
	int alloc = c->count + 1;

	if (alloc > c->alloc) {
		c->keyvals = xrenew(struct keyval, c->keyvals, alloc);
		c->alloc = alloc;
	}
	c->keyvals[c->count].key = NULL;
	c->keyvals[c->count].val = NULL;
}
