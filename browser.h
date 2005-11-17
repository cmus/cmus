/* 
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _BROWSER_H
#define _BROWSER_H

#include <list.h>
#include <window.h>
#include <search.h>
#include <uchar.h>

struct browser_entry {
	struct list_head node;

	char *name;
	enum { BROWSER_ENTRY_DIR, BROWSER_ENTRY_FILE, BROWSER_ENTRY_PLLINE } type;
};

static inline struct browser_entry *iter_to_browser_entry(struct iter *iter)
{
	return iter->data1;
}

extern struct window *browser_win;
extern char *browser_dir;
extern struct searchable *browser_searchable;

extern void browser_init(void);
extern void browser_exit(void);
extern int browser_chdir(const char *dir);

/* bindable */
extern void browser_cd_parent(void);
extern void browser_enter(void);
extern void browser_add(void);
extern void browser_queue_append(void);
extern void browser_queue_prepend(void);
extern void browser_delete(void);
extern void browser_reload(void);
extern void browser_toggle_show_hidden(void);

#endif
