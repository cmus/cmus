/*
 * help.c, heavily based on filters.c
 *  (c) 2006, <ft@bewatermyfriend.de>
 */

#include "help.h"
#include "window.h"
#include "search.h"
#include "misc.h"
#include "xmalloc.h"
#include "keys.h"
#include "command_mode.h"

#include <ctype.h>

struct window *help_win;
struct searchable *help_searchable;

static LIST_HEAD(help_head);
static struct list_head *bound_head;
static struct list_head *unbound_head;

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
	char **words=get_words(text);
	if (words[0] != NULL) {
		struct help_entry *ent;
		int i;

		ent = iter_to_help_entry(iter);
		for (i = 0; ; i++) {
			if (words[i] == NULL) {
				window_set_sel(help_win, iter);
				matched = 1;
				break;
			} if (ent->type == HE_TEXT) {
				if (!u_strcasestr(ent->text, words[i]))
					break;
			} else if (ent->type == HE_BOUND) {
				if (!u_strcasestr(key_context_names[ent->binding->ctx], words[i])
					&& !u_strcasestr(ent->binding->cmd, words[i])
					&& !u_strcasestr(ent->binding->key->name, words[i]))
					break;
			} else if (ent->type == HE_UNBOUND) {
				if (!u_strcasestr(ent->command->name, words[i]))
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

static struct list_head *help_add_text(const char *s)
{
	struct help_entry *ent;
	ent = xnew(struct help_entry, 1);
	ent->type = HE_TEXT;
	ent->text = s;
	list_add_tail(&ent->node, &help_head);
	return(&ent->node);
}

static void help_add_defaults(void)
{
	help_add_text("Current Keybindings");
	help_add_text("-----------------------------");
	bound_head = help_add_text("");
	help_add_text("Unbound Commands");
	help_add_text("-----------------------------");
	unbound_head = help_add_text("");
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

void help_add_unbound(struct command *cmd)
{
	struct help_entry *ent;
	struct command *c = cmd;
	if (c->bc == 0) {
		ent=xnew(struct help_entry, 1);
		ent->type=HE_UNBOUND;
		ent->command=cmd;
		list_init(&ent->node);
		list_add_tail(&ent->node, unbound_head);
	}
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
	/* nothing right now */
}

void help_add_bound(const struct binding *bind)
{
	struct help_entry *ent;
	ent = xnew(struct help_entry, 1);
	ent->type = HE_BOUND;
	ent->binding = bind;
	list_add_tail(&ent->node, bound_head);
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
