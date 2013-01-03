/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2006 <ft@bewatermyfriend.org>
 *
 * heavily based on filters.c
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

#include "help.h"
#include "window.h"
#include "search.h"
#include "misc.h"
#include "xmalloc.h"
#include "keys.h"
#include "command_mode.h"
#include "ui_curses.h"
#include "options.h"
#include "cmdline.h"

#include <stdio.h>

struct window *help_win;
struct searchable *help_searchable;

static LIST_HEAD(help_head);
static struct list_head *bound_head;
static struct list_head *bound_tail;
static struct list_head *unbound_head;
static struct list_head *unbound_tail;

static inline void help_entry_to_iter(struct help_entry *e, struct iter *iter)
{
	iter->data0 = &help_head;
	iter->data1 = e;
	iter->data2 = NULL;
}

static GENERIC_ITER_PREV(help_get_prev, struct help_entry, node)
static GENERIC_ITER_NEXT(help_get_next, struct help_entry, node)

static int help_search_get_current(void *data, struct iter *iter)
{
	return window_get_sel(help_win, iter);
}

static int help_search_matches(void *data, struct iter *iter, const char *text)
{
	int matched = 0;
	char **words = get_words(text);

	if (words[0] != NULL) {
		struct help_entry *ent;
		int i;

		ent = iter_to_help_entry(iter);
		for (i = 0; ; i++) {
			if (words[i] == NULL) {
				window_set_sel(help_win, iter);
				matched = 1;
				break;
			}
			if (ent->type == HE_TEXT) {
				if (!u_strcasestr(ent->text, words[i]))
					break;
			} else if (ent->type == HE_BOUND) {
				if (!u_strcasestr(ent->binding->cmd, words[i]) &&
					!u_strcasestr(ent->binding->key->name, words[i]))
					break;
			} else if (ent->type == HE_UNBOUND) {
				if (!u_strcasestr(ent->command->name, words[i]))
					break;
			} else if (ent->type == HE_OPTION) {
				if (!u_strcasestr(ent->option->name, words[i]))
					break;
			}
		}
	}
	free_str_array(words);
	return matched;
}

static const struct searchable_ops help_search_ops = {
	.get_prev = help_get_prev,
	.get_next = help_get_next,
	.get_current = help_search_get_current,
	.matches = help_search_matches
};

static void help_add_text(const char *s)
{
	struct help_entry *ent;
	ent = xnew(struct help_entry, 1);
	ent->type = HE_TEXT;
	ent->text = s;
	list_add_tail(&ent->node, &help_head);
}

static void help_add_defaults(void)
{
	struct cmus_opt *opt;

	help_add_text("Keybindings");
	help_add_text("-----------");
	bound_head = help_head.prev;
	help_add_text("");
	help_add_text("Unbound Commands");
	help_add_text("----------------");
	unbound_head = help_head.prev;
	help_add_text("");
	help_add_text("Options");
	help_add_text("-------");

	list_for_each_entry(opt, &option_head, node) {
		struct help_entry *ent = xnew(struct help_entry, 1);

		ent->type = HE_OPTION;
		ent->option = opt;
		list_add_tail(&ent->node, &help_head);
	}

	bound_tail = bound_head->next;
	unbound_tail = unbound_head->next;
}

void help_remove_unbound(struct command *cmd)
{
	struct help_entry *ent;
	struct iter i;
	list_for_each_entry(ent, &help_head, node) {
		if (ent->type != HE_UNBOUND)
			continue;
		if (ent->command == cmd) {
			help_entry_to_iter(ent, &i);
			window_row_vanishes(help_win, &i);
			list_del(&ent->node);
			free(ent);
			return;
		}
	}
}

static void list_add_sorted(struct list_head *new, struct list_head *head,
		struct list_head *tail,
		int (*cmp)(struct list_head *, struct list_head *))
{
	struct list_head *item = tail->prev;

	while (item != head) {
		if (cmp(new, item) >= 0)
			break;
		item = item->prev;
	}
	/* add after item */
	list_add(new, item);
}

static int bound_cmp(struct list_head *ai, struct list_head *bi)
{
	struct help_entry *a = container_of(ai, struct help_entry, node);
	struct help_entry *b = container_of(bi, struct help_entry, node);
	int ret = a->binding->ctx - b->binding->ctx;

	if (!ret)
		ret = strcmp(a->binding->key->name, b->binding->key->name);
	return ret;
}

static int unbound_cmp(struct list_head *ai, struct list_head *bi)
{
	struct help_entry *a = container_of(ai, struct help_entry, node);
	struct help_entry *b = container_of(bi, struct help_entry, node);

	return strcmp(a->command->name, b->command->name);
}

void help_add_unbound(struct command *cmd)
{
	struct help_entry *ent;

	ent = xnew(struct help_entry, 1);
	ent->type = HE_UNBOUND;
	ent->command = cmd;
	list_add_sorted(&ent->node, unbound_head, unbound_tail, unbound_cmp);
}

void help_add_all_unbound(void)
{
	int i;
	for (i = 0; commands[i].name; ++i)
		if (!commands[i].bc)
			help_add_unbound(&commands[i]);
}

void help_select(void)
{
	struct iter sel;
	struct help_entry *ent;
	char buf[512];

	if (!window_get_sel(help_win, &sel))
		return;

	ent = iter_to_help_entry(&sel);
	switch (ent->type) {
	case HE_BOUND:
		snprintf(buf, sizeof(buf), "bind -f %s %s %s",
				key_context_names[ent->binding->ctx],
				ent->binding->key->name,
				ent->binding->cmd);
		cmdline_set_text(buf);
		enter_command_mode();
		break;
	case HE_UNBOUND:
		snprintf(buf, sizeof(buf), "bind common <key> %s",
				ent->command->name);
		cmdline_set_text(buf);
		enter_command_mode();
		break;
	case HE_OPTION:
		snprintf(buf, sizeof(buf), "set %s=", ent->option->name);
		ent->option->get(ent->option->id, buf + strlen(buf));
		cmdline_set_text(buf);
		enter_command_mode();
		break;
	default:
		break;
	}
}

void help_toggle(void)
{
	struct iter sel;
	struct help_entry *ent;

	if (!window_get_sel(help_win, &sel))
		return;

	ent = iter_to_help_entry(&sel);
	switch (ent->type) {
	case HE_OPTION:
		if (ent->option->toggle) {
			ent->option->toggle(ent->option->id);
			help_win->changed = 1;
		}
		break;
	default:
		break;
	}
}

void help_remove(void)
{
	struct iter sel;
	struct help_entry *ent;

	if (!window_get_sel(help_win, &sel))
		return;

	ent = iter_to_help_entry(&sel);
	switch (ent->type) {
	case HE_BOUND:
		if (yes_no_query("Remove selected binding? [y/N]"))
			key_unbind(key_context_names[ent->binding->ctx],
					ent->binding->key->name, 0);
		break;
	default:
		break;
	}
}

void help_add_bound(const struct binding *bind)
{
	struct help_entry *ent;
	ent = xnew(struct help_entry, 1);
	ent->type = HE_BOUND;
	ent->binding = bind;
	list_add_sorted(&ent->node, bound_head, bound_tail, bound_cmp);
}

void help_remove_bound(const struct binding *bind)
{
	struct help_entry *ent;
	struct iter i;
	list_for_each_entry(ent, &help_head, node) {
		if (ent->binding == bind) {
			help_entry_to_iter(ent, &i);
			window_row_vanishes(help_win, &i);
			list_del(&ent->node);
			free(ent);
			return;
		}
	}
}

void help_init(void)
{
	struct iter iter;

	help_win = window_new(help_get_prev, help_get_next);
	window_set_contents(help_win, &help_head);
	window_changed(help_win);
	help_add_defaults();

	iter.data0 = &help_head;
	iter.data1 = NULL;
	iter.data2 = NULL;
	help_searchable = searchable_new(NULL, &iter, &help_search_ops);
}

void help_exit(void)
{
	searchable_free(help_searchable);
	window_free(help_win);
}
