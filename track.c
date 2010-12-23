/*
 * Copyright 2006 Timo Hirvonen
 */

#include "track.h"
#include "iter.h"
#include "track_info.h"
#include "search_mode.h"
#include "window.h"
#include "options.h"
#include "comment.h"
#include "uchar.h"
#include "xmalloc.h"

#include <string.h>

void simple_track_init(struct simple_track *track, struct track_info *ti)
{
	track->info = ti;
	track->marked = 0;
	RB_CLEAR_NODE(&track->tree_node);
}

struct simple_track *simple_track_new(struct track_info *ti)
{
	struct simple_track *t = xnew(struct simple_track, 1);

	track_info_ref(ti);
	simple_track_init(t, ti);
	return t;
}

GENERIC_ITER_PREV(simple_track_get_prev, struct simple_track, node)
GENERIC_ITER_NEXT(simple_track_get_next, struct simple_track, node)

int simple_track_search_get_current(void *data, struct iter *iter)
{
	return window_get_sel(data, iter);
}

int simple_track_search_matches(void *data, struct iter *iter, const char *text)
{
	unsigned int flags = TI_MATCH_TITLE;
	struct simple_track *track = iter_to_simple_track(iter);

	if (!search_restricted)
		flags |= TI_MATCH_ARTIST | TI_MATCH_ALBUM | TI_MATCH_ALBUMARTIST;

	if (!track_info_matches(track->info, text, flags))
		return 0;

	window_set_sel(data, iter);
	return 1;
}

struct shuffle_track *shuffle_list_get_next(struct rb_root *root, struct shuffle_track *cur,
		int (*filter)(const struct simple_track *))
{
	struct rb_node *node;

	if (!cur)
		return tree_node_to_shuffle_track(rb_first(root));

	node = rb_next(&cur->tree_node);
again:
	while (node) {
		struct shuffle_track *track = tree_node_to_shuffle_track(node);

		if (filter((struct simple_track *)track))
			return track;
		node = rb_next(node);
	}
	if (repeat) {
		if (auto_reshuffle)
			shuffle_list_reshuffle(root);
		node = rb_first(root);
		goto again;
	}
	return NULL;
}

struct shuffle_track *shuffle_list_get_prev(struct rb_root *root, struct shuffle_track *cur,
		int (*filter)(const struct simple_track *))
{
	struct rb_node *node;

	if (!cur)
		return tree_node_to_shuffle_track(rb_last(root));

	node = rb_prev(&cur->tree_node);
again:
	while (node) {
		struct shuffle_track *track = tree_node_to_shuffle_track(node);

		if (filter((struct simple_track *)track))
			return track;
		node = rb_prev(node);
	}
	if (repeat) {
		if (auto_reshuffle)
			shuffle_list_reshuffle(root);
		node = rb_last(root);
		goto again;
	}
	return NULL;
}

struct simple_track *simple_list_get_next(struct list_head *head, struct simple_track *cur,
		int (*filter)(const struct simple_track *))
{
	struct list_head *item;

	if (cur == NULL)
		return to_simple_track(head->next);

	item = cur->node.next;
again:
	while (item != head) {
		struct simple_track *track = to_simple_track(item);

		if (filter(track))
			return track;
		item = item->next;
	}
	item = head->next;
	if (repeat)
		goto again;
	return NULL;
}

struct simple_track *simple_list_get_prev(struct list_head *head, struct simple_track *cur,
		int (*filter)(const struct simple_track *))
{
	struct list_head *item;

	if (cur == NULL)
		return to_simple_track(head->next);

	item = cur->node.prev;
again:
	while (item != head) {
		struct simple_track *track = to_simple_track(item);

		if (filter(track))
			return track;
		item = item->prev;
	}
	item = head->prev;
	if (repeat)
		goto again;
	return NULL;
}

void sorted_list_add_track(struct list_head *head, struct rb_root *tree_root, struct simple_track *track, const char * const *keys)
{
	struct rb_node **new = &(tree_root->rb_node), *parent = NULL, *curr, *prev;

	/* try to locate track in tree */
	while (*new) {
		// why not move this outside of loop?
		const struct simple_track *a = to_simple_track(&track->node);
		const struct simple_track *b = tree_node_to_simple_track(*new);
		int result = track_info_cmp(a->info, b->info, keys);

		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			break;
	}

	/* only add to tree if not there already */
	if (!(*new)) {
		rb_link_node(&track->tree_node, parent, new);
		curr = *new;
		rb_insert_color(&track->tree_node, tree_root);
	} else
		curr = *new;

	/* locate previous list item or use list head */
	prev = rb_prev(curr);
	if (prev) {
		struct simple_track *prev_track = tree_node_to_simple_track(prev);
		list_add(&track->node, &prev_track->node);
	} else
		list_add_tail(&track->node, head);
}

static int compare_rand(const struct rb_node *a, const struct rb_node *b)
{
	struct shuffle_track *tr_a = tree_node_to_shuffle_track(a);
	struct shuffle_track *tr_b = tree_node_to_shuffle_track(b);

	if (tr_a->rand < tr_b->rand)
		return -1;
	if (tr_a->rand > tr_b->rand)
		return +1;

	return 0;
}

static void shuffle_track_init(struct shuffle_track *track)
{
	track->rand = rand() / ((double) RAND_MAX + 1);
}

void shuffle_list_add(struct shuffle_track *track, struct rb_root *tree_root)
{
	struct rb_node **new = &(tree_root->rb_node), *parent = NULL;

	shuffle_track_init(track);

	/* try to locate track in tree */
	while (*new) {
		int result = compare_rand(&track->tree_node, *new);

		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else {
			/* very unlikely, try again! */
			shuffle_list_add(track, tree_root);
			return;
		}
	}

	rb_link_node(&track->tree_node, parent, new);
	rb_insert_color(&track->tree_node, tree_root);
}

void shuffle_list_reshuffle(struct rb_root *tree_root)
{
	struct rb_node *node, *tmp;
	struct rb_root tmptree = RB_ROOT;

	rb_for_each_safe(node, tmp, tree_root) {
		struct shuffle_track *track = tree_node_to_shuffle_track(node);
		rb_erase(node, tree_root);
		shuffle_list_add(track, &tmptree);
	}

	tree_root->rb_node = tmptree.rb_node;
}

/* expensive */
void list_add_rand(struct list_head *head, struct list_head *node, int nr)
{
	struct list_head *item;
	int pos;

	pos = rand() % (nr + 1);
	item = head;
	if (pos <= nr / 2) {
		while (pos) {
			item = item->next;
			pos--;
		}
		/* add after item */
		list_add(node, item);
	} else {
		pos = nr - pos;
		while (pos) {
			item = item->prev;
			pos--;
		}
		/* add before item */
		list_add_tail(node, item);
	}
}

int simple_list_for_each_marked(struct list_head *head,
		int (*cb)(void *data, struct track_info *ti), void *data, int reverse)
{
	struct simple_track *t;
	int rc = 0;

	if (reverse) {
		list_for_each_entry_reverse(t, head, node) {
			if (t->marked) {
				rc = cb(data, t->info);
				if (rc)
					break;
			}
		}
	} else {
		list_for_each_entry(t, head, node) {
			if (t->marked) {
				rc = cb(data, t->info);
				if (rc)
					break;
			}
		}
	}
	return rc;
}
