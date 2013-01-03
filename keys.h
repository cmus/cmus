/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2004-2006 Timo Hirvonen
 *
 * keys.[ch] by Frank Terbeck <ft@bewatermyfriend.org>
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

#ifndef KEYS_H
#define KEYS_H

#include "uchar.h"

enum key_context {
	CTX_BROWSER,
	CTX_COMMON,
	CTX_FILTERS,
	CTX_LIBRARY,
	CTX_PLAYLIST,
	CTX_QUEUE,
	CTX_SETTINGS,
};
#define NR_CTXS (CTX_SETTINGS + 1)

struct key {
	const char *name;
	int key;
	uchar ch;
};

struct binding {
	struct binding *next;
	const struct key *key;
	enum key_context ctx;
	char cmd[];
};

extern const char * const key_context_names[NR_CTXS + 1];
extern const struct key key_table[];
extern struct binding *key_bindings[NR_CTXS];

void show_binding(const char *context, const char *key);
int key_bind(const char *context, const char *key, const char *cmd, int force);
int key_unbind(const char *context, const char *key, int force);

void normal_mode_ch(uchar ch);
void normal_mode_key(int key);

#endif
