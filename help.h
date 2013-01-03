/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2006 <ft@bewatermyfriend.org>
 *
 * heavily based on filters.h
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

#ifndef HELP_H
#define HELP_H

#include "list.h"
#include "window.h"
#include "search.h"
#include "keys.h"

struct help_entry {
	struct list_head node;
	enum {
		HE_TEXT,		/* text entries 	*/
		HE_BOUND,		/* bound keys		*/
		HE_UNBOUND,		/* unbound commands	*/
		HE_OPTION,
	} type;
	union {
		const char *text;			/* HE_TEXT	*/
		const struct binding *binding;		/* HE_BOUND	*/
		const struct command *command;		/* HE_UNBOUND	*/
		const struct cmus_opt *option;
	};
};

static inline struct help_entry *iter_to_help_entry(struct iter *iter)
{
	return iter->data1;
}

extern struct window *help_win;
extern struct searchable *help_searchable;

void help_select(void);
void help_toggle(void);
void help_remove(void);

void help_add_bound(const struct binding *bind);
void help_remove_bound(const struct binding *bind);
void help_remove_unbound(struct command *cmd);
void help_add_unbound(struct command *cmd);
void help_add_all_unbound(void);

void help_init(void);
void help_exit(void);

#endif /* HELP_H */
