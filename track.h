/*
 * Copyright 2006 Timo Hirvonen
 */

#ifndef TRACK_H
#define TRACK_H

#include "list.h"
#include "rbtree.h"
#include "iter.h"

struct simple_track {
	struct list_head node;
	struct rb_node tree_node;
	struct track_info *info;
	unsigned int marked : 1;
};

struct shuffle_track {
	struct simple_track simple_track;
	struct rb_node tree_node;
	double rand;
};

static inline struct track_info *shuffle_track_info(const struct shuffle_track *track)
{
	return ((struct simple_track *)track)->info;
}

static inline struct simple_track *to_simple_track(const struct list_head *item)
{
	return container_of(item, struct simple_track, node);
}

static inline struct simple_track *iter_to_simple_track(const struct iter *iter)
{
	return iter->data1;
}

static inline struct simple_track *tree_node_to_simple_track(const struct rb_node *node)
{
	return container_of(node, struct simple_track, tree_node);
}

static inline struct shuffle_track *tree_node_to_shuffle_track(const struct rb_node *node)
{
	return container_of(node, struct shuffle_track, tree_node);
}

/* NOTE: does not ref ti */
void simple_track_init(struct simple_track *track, struct track_info *ti);

/* refs ti */
struct simple_track *simple_track_new(struct track_info *ti);

int simple_track_get_prev(struct iter *);
int simple_track_get_next(struct iter *);

/* data is window */
int simple_track_search_get_current(void *data, struct iter *iter);
int simple_track_search_matches(void *data, struct iter *iter, const char *text);

struct shuffle_track *shuffle_list_get_next(struct rb_root *root, struct shuffle_track *cur,
		int (*filter)(const struct simple_track *));

struct shuffle_track *shuffle_list_get_prev(struct rb_root *root, struct shuffle_track *cur,
		int (*filter)(const struct simple_track *));

struct simple_track *simple_list_get_next(struct list_head *head, struct simple_track *cur,
		int (*filter)(const struct simple_track *));

struct simple_track *simple_list_get_prev(struct list_head *head, struct simple_track *cur,
		int (*filter)(const struct simple_track *));

void sorted_list_add_track(struct list_head *head, struct rb_root *tree_root, struct simple_track *track, const char * const *keys);

void list_add_rand(struct list_head *head, struct list_head *node, int nr);

int simple_list_for_each_marked(struct list_head *head,
		int (*cb)(void *data, struct track_info *ti), void *data, int reverse);

void shuffle_list_add(struct shuffle_track *track, struct rb_root *tree_root);
void shuffle_list_reshuffle(struct rb_root *tree_root);

#endif
