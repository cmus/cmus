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

#ifndef FILTERS_H
#define FILTERS_H

#include "list.h"
#include "window.h"
#include "search.h"

/* factivate foo !bar
 *
 * foo: FS_YES
 * bar: FS_NO
 * baz: FS_IGNORE
 */
enum {
	/* [ ] filter not selected */
	FS_IGNORE,
	/* [*] filter selected */
	FS_YES,
	/* [!] filter selected and inverted */
	FS_NO,
};

struct filter_entry {
	struct list_head node;

	char *name;
	char *filter;
	unsigned visited : 1;

	/* selected and activated status (FS_* enum) */
	unsigned sel_stat : 2;
	unsigned act_stat : 2;
};

static inline struct filter_entry *iter_to_filter_entry(struct iter *iter)
{
	return iter->data1;
}

extern struct window *filters_win;
extern struct searchable *filters_searchable;
extern struct list_head filters_head;

void filters_init(void);
void filters_exit(void);

/* parse filter and expand sub filters */
struct expr *parse_filter(const char *val);

/* add filter to filter list (replaces old filter with same name)
 *
 * @keyval  "name=value" where value is filter
 */
void filters_set_filter(const char *keyval);

/* set throwaway filter (not saved to the filter list)
 *
 * @val   filter or NULL to disable filtering
 */
void filters_set_anonymous(const char *val);

/* set live filter (not saved to the filter list)
 *
 * @val   filter or NULL to disable filtering
 */
void filters_set_live(const char *val);

void filters_activate_names(const char *str);

void filters_activate(void);
void filters_toggle_filter(void);
void filters_delete_filter(void);

#endif
