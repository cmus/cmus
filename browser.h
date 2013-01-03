/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2004 Timo Hirvonen
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

#ifndef _BROWSER_H
#define _BROWSER_H

#include "list.h"
#include "window.h"
#include "search.h"

struct browser_entry {
	struct list_head node;

	enum { BROWSER_ENTRY_DIR, BROWSER_ENTRY_FILE, BROWSER_ENTRY_PLLINE } type;
	char name[];
};

static inline struct browser_entry *iter_to_browser_entry(struct iter *iter)
{
	return iter->data1;
}

extern struct window *browser_win;
extern char *browser_dir;
extern struct searchable *browser_searchable;

void browser_init(void);
void browser_exit(void);
int browser_chdir(const char *dir);
char *browser_get_sel(void);
void browser_up(void);
void browser_enter(void);
void browser_delete(void);
void browser_reload(void);
void browser_toggle_show_hidden(void);

#endif
