/*
 * Copyright 2004-2005 Timo Hirvonen
 *
 * keys.(c|h) by Frank Terbeck <frank.terbeck@rwth-aachen.de>
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

#ifndef KEYS_H
#define KEYS_H

#include <uchar.h>

enum key_context {
	CTX_BROWSER,
	CTX_COMMON,
	CTX_FILTERS,
	CTX_PLAY_QUEUE,
	CTX_PLAYLIST
};
#define NR_CTXS (CTX_PLAYLIST + 1)

struct key_function {
	const char *name;
	void (*func)(void);
};

struct key {
	const char *name;
	int key;
	uchar ch;
};

struct binding {
	struct binding *next;
	const struct key *key;
	const struct key_function *func;
};

extern const char * const key_context_names[NR_CTXS + 1];
extern const struct key_function *key_functions[NR_CTXS + 1];
extern const struct key key_table[];
extern struct binding *key_bindings[NR_CTXS];

void keys_init(void);
void keys_exit(void);

int key_bind(const char *context, const char *key, const char *func);
int key_unbind(const char *context, const char *key);

void normal_mode_ch(uchar ch);
void normal_mode_key(int key);

#endif
