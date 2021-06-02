/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2006 Timo Hirvonen
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

#ifndef CMUS_TRACK_H
#define CMUS_TRACK_H

#include "list.h"
#include "rbtree.h"
#include "iter.h"
#include "track_info.h"
#include "cmus.h"

struct shuffle_info {
	struct rb_node tree_node;
	struct album *album;
	double rand;
};

struct simple_track {
	struct shuffle_info shuffle_info;
	struct list_head node;
	struct rb_node tree_node;
	struct track_info *info;
	unsigned int marked : 1;
};

static inline struct simple_track *
shuffle_info_to_simple_track(struct shuffle_info *track)
{
	return container_of(track, struct simple_track, shuffle_info);
}

static inline struct track_info *shuffle_info_info(const struct shuffle_info *track)
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

static inline struct shuffle_info *tree_node_to_shuffle_info(const struct rb_node *node)
{
	return container_of(node, struct shuffle_info, tree_node);
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
int _simple_track_search_matches(struct iter *iter, const char *text);

struct shuffle_info *shuffle_list_get_next(struct rb_root *root, struct shuffle_info *cur,
		int (*filter)(const struct album *));

struct shuffle_info *shuffle_list_get_prev(struct rb_root *root, struct shuffle_info *cur,
		int (*filter)(const struct album *));

struct simple_track *simple_list_get_next(struct list_head *head, struct simple_track *cur,
		int (*filter)(const struct album *), bool allow_repeat);

struct simple_track *simple_list_get_prev(struct list_head *head, struct simple_track *cur,
		int (*filter)(const struct album *), bool allow_repeat);

void sorted_list_add_track(struct list_head *head, struct rb_root *tree_root, struct simple_track *track,
		const sort_key_t *keys, int tiebreak);
void sorted_list_remove_track(struct list_head *head, struct rb_root *tree_root, struct simple_track *track);
void sorted_list_rebuild(struct list_head *head, struct rb_root *tree_root, const sort_key_t *keys);
void rand_list_rebuild(struct list_head *head, struct rb_root *tree_root);

void list_add_rand(struct list_head *head, struct list_head *node, int nr);

int simple_list_for_each_marked(struct list_head *head, track_info_cb cb,
		void *data, int reverse);
int simple_list_for_each(struct list_head *head, track_info_cb cb,
		void *data, int reverse);

void shuffle_list_add(struct shuffle_info *track, struct rb_root *tree_root, struct album *album);
void shuffle_list_reshuffle(struct rb_root *tree_root);
void shuffle_insert(struct rb_root *root, struct shuffle_info *previous, struct shuffle_info *new);

#endif
